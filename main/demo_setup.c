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

static void run_selected(void)
{
    s_busy = true;
    switch (s_sel) {
    case MODE_STA:
        net_sta_start();
        break;
    case MODE_SOFTAP:
        net_softap_start();
        break;
    case MODE_BLE:
        net_ble_start();
        break;
    default:
        break;
    }
    s_busy = false;
    refresh_status();
    menu_refresh();
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

    ui_pixel_mascot_create(s_scr, 101, 246);
    s_timer = lv_timer_create(tick, 500, NULL);
    lv_screen_load(s_scr);
}

void demo_setup_exit(void)
{
    if (s_timer) { lv_timer_delete(s_timer); s_timer = NULL; }
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