// main/demo_setup.c —— 配网 / 联网状态页(可视化自主配网,不插电脑)。
//
// 背景:日历页不想把 WiFi 写死(用户各有各的网络),因此把“联网信息从哪里来”
// 抽到 app_net 统一模块。本页给出三种在设备上自取的方式:
//   1. STA:   用 NVS 里已保存的 WiFi 自动连接 + SNTP 校时;
//   2. SoftAP:设备开热点,手机连上后访问 192.168.4.1 填 WiFi(兜底);
//   3. BLE:   手机/NFC 客户端经蓝牙下发 WiFi 与当前时间。
//
// 交互(长按 OK 返回菜单由 main.c 统一拦截,本页只收其余事件):
//   上/下 短按  在 STA / SoftAP / BLE 之间切换
//   确定 短按   启动当前选中模式(重复按会在 STA 下重连)
//   任意键       即时点亮背光(若已自动熄灭)
#include "demo.h"
#include "bsp_display.h"
#include "ui_pixel.h"
#include "app_net.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "lvgl.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "demo_setup";

typedef enum {
    MODE_STA = 0,
    MODE_SOFTAP,
    MODE_BLE,
    MODE_COUNT,
} setup_mode_t;

static const char *MODE_NAMES[MODE_COUNT] = {
    "WiFi: auto join",
    "SoftAP: config page",
    "BLE: provision",
};

static lv_obj_t       *s_scr;
static lv_obj_t       *s_status;            // 顶部状态条
static lv_obj_t       *s_card[MODE_COUNT];
static lv_timer_t     *s_timer;
static setup_mode_t    s_sel;
static bool            s_busy;

// 联网/配网是慢(且深栈)操作:按键回调运行在 esp_timer 任务(栈很小),直接在里面跑
// esp_wifi_init/httpd_start/NimBLE 会栈溢出崩溃。故交给独立高栈 worker 任务执行,
// 按键回调只投递请求即返回。这也符合"网络慢操作必须放入工作任务"的规范。
#define NET_WORKER_STACK  8192
#define NET_WORKER_PRIO   5

static TaskHandle_t       s_worker;
static SemaphoreHandle_t  s_work;            // 投递信号:有请求(或退出)
static SemaphoreHandle_t  s_ack;             // worker 处理完一次请求后的回执
static setup_mode_t       s_req_mode;
static volatile bool      s_exit;

// 供电醒屏与空闲背光:沿用日历页的续航策略。
#define IDLE_BL_OFF_SECS 30

static uint32_t s_idle_secs;

static void menu_refresh(void)
{
    uint32_t fg = UI_INK;
    if (s_sel == MODE_STA  && net_state_get() == NET_STA_ONLINE)  fg = UI_GRASS_DARK;
    if (s_sel == MODE_SOFTAP && net_state_get() == NET_AP)         fg = UI_GRASS_DARK;
    if (s_sel == MODE_BLE   && net_state_get() == NET_BLE)          fg = UI_GRASS_DARK;

    for (int i = 0; i < MODE_COUNT; i++) {
        ui_pixel_set_selected(s_card[i], i == (int)s_sel, true);
    }
    (void)fg;
}

static void refresh_status(void)
{
    if (!s_status) return;
    bool creds = net_have_creds();
    lv_label_set_text_fmt(s_status, "%s   creds:%s",
                          net_state_str(net_state_get()),
                          creds ? "yes" : "no");
}

static void tick(lv_timer_t *t)
{
    (void)t;
    s_idle_secs++;
    if (s_idle_secs == IDLE_BL_OFF_SECS) {
        bsp_display_backlight(0);
        ESP_LOGI(TAG, "空闲 %us,背光已熄灭", (unsigned)IDLE_BL_OFF_SECS);
    }
    // 每 500ms 刷新状态,让“连接中”→“已上线”可见
    if (s_idle_secs % 1 == 0) refresh_status();
}

// 联网/配网 worker:独立高栈任务,执行实际的 net_*_start,避免在按键(esp_timer)
// 回调里跑深栈调用。UI 刷新需持有 LVGL 锁。
static void net_worker(void *arg)
{
    (void)arg;
    for (;;) {
        if (!xSemaphoreTake(s_work, portMAX_DELAY)) continue;
        if (s_exit) break;                       // 页面退出,尽快收尾

        s_busy = true;
        switch (s_req_mode) {
        case MODE_STA:    net_sta_start();    break;
        case MODE_SOFTAP: net_softap_start(); break;
        case MODE_BLE:    net_ble_start();    break;
        default: break;
        }

        if (bsp_lvgl_lock(500)) {
            refresh_status();
            menu_refresh();
            bsp_lvgl_unlock();
        }
        s_busy = false;
        xSemaphoreGive(s_ack);
    }
    xSemaphoreGive(s_ack);                       // 退出确认,让 exit 安全删除本任务
    vTaskDelete(NULL);
}

static void run_selected(void)
{
    // 只投递不执行:真正启动交给 net_worker(高栈任务),本回调立即返回,不阻塞、
    // 不长时间占用 LVGL 锁。
    s_req_mode = s_sel;
    s_busy = true;
    if (s_worker) xSemaphoreGive(s_work);
}

void demo_setup_enter(void)
{
    s_scr = ui_pixel_screen_create("SETUP");

    lv_obj_t *top = ui_pixel_panel_create(s_scr, 14, 50, 212, 44, UI_PAPER);
    s_status = lv_label_create(top);
    lv_obj_set_width(s_status, 190);
    lv_obj_set_style_text_font(s_status, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_status, lv_color_hex(UI_INK), 0);
    lv_obj_align(s_status, LV_ALIGN_TOP_LEFT, 4, 4);
    refresh_status();

    for (int i = 0; i < MODE_COUNT; i++) {
        s_card[i] = ui_pixel_panel_create(s_scr, 14, 104 + i * 52, 212, 44, UI_PAPER);
        lv_obj_t *label = lv_label_create(s_card[i]);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(label, lv_color_hex(UI_INK), 0);
        lv_label_set_text(label, MODE_NAMES[i]);
        lv_obj_center(label);
    }
    s_sel = MODE_STA;
    menu_refresh();
    s_busy = false;
    s_idle_secs = 0;
    s_exit = false;

    // 联网 worker 信号量:创建一次、跨页面复用。退出时【不得】删除——否则 worker
    // 若仍在对已删除信号量取/给,会触发 UAF 崩溃(实测 Store access fault @ line 108 /
    // xQueueGenericSend assert @ line 127)。进入时排空历史信号,保证干净握手。
    if (!s_work) s_work = xSemaphoreCreateBinary();
    if (!s_ack)  s_ack  = xSemaphoreCreateBinary();
    while (xSemaphoreTake(s_ack, 0) == pdTRUE) {}   // 排空历史回执
    while (xSemaphoreTake(s_work, 0) == pdTRUE) {}  // 排空历史请求
    s_worker = NULL;
    if (s_work && s_ack) {
        if (xTaskCreate(net_worker, "net_worker", NET_WORKER_STACK, NULL,
                        NET_WORKER_PRIO, &s_worker) != pdPASS) {
            s_worker = NULL;
        }
    }

    ui_pixel_mascot_create(s_scr, 101, 246);
    s_timer = lv_timer_create(tick, 500, NULL);
    lv_screen_load(s_scr);
}

void demo_setup_exit(void)
{
    if (s_timer) { lv_timer_delete(s_timer); s_timer = NULL; }

    // 先让 worker 收尾当前请求并退出,再释放网络资源,避免并发使用 WiFi/NimBLE。
    // 注意:
    //   1) worker 在收尾后会自我删除(vTaskDelete(NULL)),此处不能再对其句柄调用
    //      vTaskDelete,否则访问已释放 TCB 会崩溃;
    //   2) s_work/s_ack 信号量必须跨页面复用、退出时【不得】删除——否则 worker 若仍
    //      在运行会对已删除信号量取/给 → UAF 崩溃。故这里只等 ack 握手完成、置空句柄,
    //      由下次进入时排空并重建 worker。
    if (s_worker) {
        s_exit = true;
        xSemaphoreGive(s_work);
        xSemaphoreTake(s_ack, pdMS_TO_TICKS(2000));   // 等 worker 结束当前操作并释放自身
        s_worker = NULL;
    }

    net_stop_all();                     // 释放 WiFi/NimBLE/HTTP,静音优先
    bsp_display_backlight(100);
    if (s_scr) {
        lv_obj_delete(s_scr);
        s_scr = NULL;
        s_status = NULL;
        for (int i = 0; i < MODE_COUNT; i++) s_card[i] = NULL;
    }
}

void demo_setup_key(bsp_btn_t btn, bsp_btn_ev_t ev)
{
    s_idle_secs = 0;
    bsp_display_backlight(100);

    if (s_busy) return;
    if (ev != BSP_BTN_CLICK) return;

    if (btn == BSP_BTN_UP || btn == BSP_BTN_DOWN) {
        s_sel = (setup_mode_t)((s_sel + 1) % MODE_COUNT);
        menu_refresh();
    } else if (btn == BSP_BTN_OK) {
        run_selected();
    }
}