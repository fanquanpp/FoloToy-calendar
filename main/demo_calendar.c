// main/demo_calendar.c —— 日历页：公历月历 + 倒计时 + 纪念日 + 目标高亮。
//
// 设计要点（对齐仓库像素风 / 纯逻辑与 UI 分层 / 续航调优）：
//   1. 日期计算全部走 calendar_logic(纯 C,无堆无锁),本文件只做 UI 与交互;
//   2. 无网络 / 无硬件 RTC:采用“运行日期”方案,开机把基准序列日写入 NVS,
//      之后按 esp_timer 单调时钟推进,跨重启延续,离线可用、零网络功耗;
//   3. 续航:空闲 30s 自动熄灭背光(任意按键点亮),刷新用“低频”定时器,
//      “今天是哪天”每 5s 才重算一次,避免持续重绘耗电。
//
// 交互(长按 OK 返回菜单由 main.c 统一拦截,本页只收到其余事件):
//   上/下 短按       前一个月 / 后一个月(浏览)
//   上   长按       倒计时目标 -1 天
//   下   长按       倒计时目标 +1 天
//   确定 短按       倒计时目标 = 今天,并跳到今天所在月
//   确定 双击       纪念日 = 今天(存月/日,用于每年该月该日标红与判断)
//   任意键          即时点亮背光(若已自动熄灭)
#include "demo.h"
#include "bsp_display.h"
#include "ui_pixel.h"
#include "calendar_logic.h"
#include "app_net.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "lvgl.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static const char *TAG = "demo_cal";

// 运行日期的初始基准(仅 NVS 为空时用):取一个贴近现实的固定日期。
#define BASE_INIT_Y 2026u
#define BASE_INIT_M 8u
#define BASE_INIT_D 25u

// 续航参数
#define IDLE_BL_OFF_SECS  30   // 无按键 30s 后熄灭背光
#define TODAY_REFRESH_MS  5000 // 每 5s 重算一次“今天/倒计时”
#define NVS_REFRESH_SECS  3600 // 每小时把运行日期基准落盘一次

// 几何(240x320;标题栏约 8..41px,草地从 286 起)
#define GRID_ROWS 6
#define GRID_COLS 7
#define CELL_W    30
#define CELL_H    22
#define GRID_X    15   // 网格左边距,使 7*30 在 240 宽内居中
#define GRID_Y    90
#define ROW_PITCH (CELL_H + 1)
#define INFO_Y    230
#define INFO_H    52

static const char *MONTHS[12] = {
    "JAN", "FEB", "MAR", "APR", "MAY", "JUN",
    "JUL", "AUG", "SEP", "OCT", "NOV", "DEC",
};
static const char *WDAY[7] = { "S", "M", "T", "W", "T", "F", "S" };

static bool        s_nvs_ok;
static lv_obj_t   *s_scr;
static lv_obj_t   *s_header;             // "AUG 2026"
static lv_obj_t   *s_weekrow[7];
static lv_obj_t   *s_cells[GRID_ROWS][GRID_COLS];
static lv_obj_t   *s_cell_lab[GRID_ROWS][GRID_COLS];
static lv_obj_t   *s_info;               // 底部(倒计时/纪念日)
static lv_timer_t *s_timer;

static uint16_t  s_view_y;               // 当前浏览的年
static uint8_t   s_view_m;               // 当前浏览的月
static uint16_t  s_today_y;              // “今天”
static uint8_t   s_today_m;
static uint8_t   s_today_d;
static int64_t   s_today_days;
static int64_t   s_tgt_days;             // 倒计时目标(序列日,权威值)
static uint16_t  s_anniv_m;              // 纪念日(月/日,0 = 未设置)
static uint8_t   s_anniv_d;

static uint32_t  s_idle_secs;
static int32_t   s_last_persist;         // 距上次基准落盘的秒数

// 运行日期:NVS 基准 + 单调时钟推进。
static int64_t s_base_days;
static int64_t s_base_uptime_us;

static int64_t today_now(void)
{
    int64_t now = esp_timer_get_time();
    return s_base_days + (now - s_base_uptime_us) / (INT64_C(24) * 3600 * 1000 * 1000);
}

static void refresh_today(void)
{
    int64_t d = today_now();
    if (d == s_today_days) return;       // 天没变,不重算也不碰 LVGL
    s_today_days = d;
    cal_days_to_date(d, &s_today_y, &s_today_m, &s_today_d);
}

// ---------- NVS ----------
static void nvs_ensure(void)
{
    if (s_nvs_ok) return;
    nvs_flash_init();                    // 重复调用失败无碍,nvs_open 仍可用
    s_nvs_ok = true;
}

static bool nvs_load(void)
{
    nvs_handle_t h;
    if (nvs_open("cal", NVS_READONLY, &h) != ESP_OK) return false;
    bool have = false;
    int64_t base_days = 0, base_upt = 0, tgt = 0;
    int32_t anniv = 0;
    if (nvs_get_i64(h, "base_days", &base_days) == ESP_OK &&
        nvs_get_i64(h, "base_upt", &base_upt) == ESP_OK) {
        s_base_days = base_days;
        s_base_uptime_us = base_upt;
        have = true;
    }
    if (nvs_get_i64(h, "tgt_days", &tgt) == ESP_OK && tgt != 0) s_tgt_days = tgt;
    if (nvs_get_i32(h, "anniv", &anniv) == ESP_OK && anniv > 0) {
        s_anniv_m = (uint16_t)(anniv / 100);
        s_anniv_d = (uint8_t)(anniv % 100);
    }
    nvs_close(h);
    return have;
}

static void nvs_persist(void)
{
    if (!s_nvs_ok) return;
    nvs_handle_t h;
    if (nvs_open("cal", NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_i64(h, "tgt_days", s_tgt_days);
    nvs_set_i32(h, "anniv", (int32_t)s_anniv_m * 100 + s_anniv_d);
    // 把“当前时刻”固化成新基准,减小累计计时带来的长期漂移
    nvs_set_i64(h, "base_days", today_now());
    nvs_set_i64(h, "base_upt", 0);
    nvs_commit(h);
    nvs_close(h);
}

static void init_running_date(void)
{
    nvs_ensure();
    if (nvs_load()) {
        // 已有基准:把 base_uptime 统一成“本次开机单调时刻”,today 不变
        int64_t cur = today_now();
        s_base_uptime_us = esp_timer_get_time();
        s_base_days = cur;
    } else {
        s_base_days = cal_date_to_days(BASE_INIT_Y, BASE_INIT_M, BASE_INIT_D);
        s_base_uptime_us = esp_timer_get_time();
        nvs_persist();                   // 首次开机即落盘,为后续跨重启铺路
    }
    refresh_today();
    if (s_tgt_days == 0) s_tgt_days = s_today_days;   // 默认目标 = 今天
}

static void  nvs_persist(void);
static void  render_view(void);

// 网络校时回调(由 app_net 在配网/SNTP 成功后调用):以真实日期重设运行基准,
// 让“今天”立即变成接收到的日期(优先于硬编码初值)。
static void cal_on_sync(uint16_t year, uint8_t month, uint8_t day)
{
    if (year < 1000 || year > 9999 || month < 1 || month > 12 || day < 1)
        return;
    s_base_days = cal_date_to_days(year, month, day);
    s_base_uptime_us = esp_timer_get_time();
    refresh_today();
    s_view_y = s_today_y;
    s_view_m = s_today_m;
    nvs_persist();                                // 立即落盘,跨重启延续校准结果
    render_view();
    ESP_LOGI(TAG, "校时回调:今天已设为 %04u-%02u-%02u",
             (unsigned)year, (unsigned)month, (unsigned)day);
}

// ---------- 绘制 ----------
static void set_cell(int row, int col, const char *text, uint32_t bg, uint32_t fg)
{
    lv_obj_set_style_bg_opa(s_cells[row][col], LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(s_cells[row][col], lv_color_hex(bg), 0);
    lv_label_set_text(s_cell_lab[row][col], text ? text : " ");
    lv_obj_set_style_text_color(s_cell_lab[row][col], lv_color_hex(fg), 0);
}

static void set_cell_blank(int row, int col)
{
    lv_obj_set_style_bg_opa(s_cells[row][col], LV_OPA_TRANSP, 0);
    lv_label_set_text(s_cell_lab[row][col], " ");
}

static void render_grid(void)
{
    static char   num[4];
    uint8_t first = cal_first_weekday(s_view_y, s_view_m);
    uint8_t dim   = cal_days_in_month(s_view_y, s_view_m);
    uint16_t ty; uint8_t tm, td;
    cal_days_to_date(s_tgt_days, &ty, &tm, &td);

    bool in_today_month = (s_view_y == s_today_y && s_view_m == s_today_m);
    bool anniv_today    = (s_anniv_m > 0 && s_today_m == s_anniv_m && s_today_d == s_anniv_d);
    bool in_anniv_month = (s_anniv_m > 0 && s_view_m == s_anniv_m);

    for (int r = 0; r < GRID_ROWS; r++) {
        for (int c = 0; c < GRID_COLS; c++) {
            int idx = r * GRID_COLS + c;
            int day = idx - (int)first + 1;
            if (day < 1 || day > (int)dim) { set_cell_blank(r, c); continue; }

            snprintf(num, sizeof(num), "%d", day);
            uint32_t bg = UI_PAPER, fg = UI_INK;

            if (s_view_y == ty && s_view_m == tm && (uint8_t)day == td) {
                bg = UI_YELLOW;                       // 倒计时目标高亮
            } else if (in_today_month && (uint8_t)day == s_today_d) {
                bg = anniv_today ? UI_ORANGE : UI_SKY; // 今天(是纪念日则染橙)
            } else if (in_anniv_month && (uint8_t)day == s_anniv_d) {
                fg = UI_RED;                          // 纪念日数字标红
            }
            set_cell(r, c, num, bg, fg);
        }
    }
}

static void render_info(void)
{
    static char buf[64];
    int64_t delta = s_tgt_days - s_today_days;
    uint16_t ty; uint8_t tm, td;
    cal_days_to_date(s_tgt_days, &ty, &tm, &td);

    int len = 0;
    if (delta >= 0) len = snprintf(buf, sizeof(buf), "D-%lld  %04u-%02u-%02u",
                                    (long long)delta, (unsigned)ty,
                                    (unsigned)tm, (unsigned)td);
    else            len = snprintf(buf, sizeof(buf), "D+%lld  %04u-%02u-%02u",
                                    (long long)-delta, (unsigned)ty,
                                    (unsigned)tm, (unsigned)td);

    if (s_anniv_m > 0) {
        int used = snprintf(buf + (size_t)len, sizeof(buf) - (size_t)len,
                            "  ANNIV %02u-%02u", (unsigned)s_anniv_m, (unsigned)s_anniv_d);
        if (len + used + 6 < (int)sizeof(buf) && len > 0)
            memmove(buf, buf, 0);                     /* 占位(避免 NUL-truncated 告警) */
    }
    lv_label_set_text(s_info, buf);
}

static void update_header(void)
{
    static char buf[16];
    snprintf(buf, sizeof(buf), "%s %04u", MONTHS[s_view_m - 1], (unsigned)s_view_y);
    lv_label_set_text(s_header, buf);
}

static void render_view(void)
{
    update_header();
    render_grid();
    render_info();
}

static void step_month(int delta)
{
    int m = (int)s_view_m + delta;
    int y = (int)s_view_y;
    if (m < 1)          { m = 12; y--; }
    else if (m > 12)    { m = 1;  y++; }
    if (y < 1)          { y = 1;  m = 1; }
    else if (y > 9999)  { y = 9999; m = 12; }
    s_view_m = (uint8_t)m;
    s_view_y = (uint16_t)y;
    render_view();
}

// ---------- 定时器(低频刷新 + 空闲背光) ----------
static void tick(lv_timer_t *t)
{
    (void)t;
    s_idle_secs++;
    if (s_idle_secs == IDLE_BL_OFF_SECS) {           // 仅一次,避免反复写背光
        bsp_display_backlight(0);
        // 熄屏后即暂停本定时器:不再周期重绘/落盘,让 esp_pm 能进入深度 light sleep,
        // 把空闲功耗压到读取状态的最低档。有按键(wake_display)才恢复并补绘。
        lv_timer_pause(s_timer);
        ESP_LOGI(TAG, "空闲 %us,背光熄灭,已暂停刷新定时器", (unsigned)IDLE_BL_OFF_SECS);
    }

    if (s_idle_secs % (TODAY_REFRESH_MS / 1000) == 0) {
        refresh_today();                             // 天没变则内部短路
        render_view();                               // 天/倒计时/纪念日刷新
    }
    if ((uint32_t)(s_idle_secs - s_last_persist) >= NVS_REFRESH_SECS) {
        nvs_persist();
        s_last_persist = (int32_t)s_idle_secs;
    }
}

// 任意按键都视为活跃:重置空闲计时、恢复(若已暂停)空闲刷定时器并把背光点亮。
static void wake_display(void)
{
    s_idle_secs = 0;
    s_last_persist = 0;
    if (s_timer && lv_timer_get_paused(s_timer)) {
        lv_timer_resume(s_timer);                    // 恢复刷新,并补一次“今天/倒计时”重算
        refresh_today();
        render_view();
    }
    bsp_display_backlight(100);
}

// ---------- Demo 接口 ----------
void demo_calendar_enter(void)
{
    net_cal_set_cb(cal_on_sync);                 // 订阅网络校时(配网/SNTP 成功后触发)
    init_running_date();
    refresh_today();

    s_view_y = s_today_y;
    s_view_m = s_today_m;

    s_scr = ui_pixel_screen_create("CALENDAR");

    s_header = ui_pixel_label(s_scr, "", &lv_font_montserrat_20, UI_INK);
    lv_obj_align(s_header, LV_ALIGN_TOP_MID, 0, 48);

    for (int i = 0; i < 7; i++) {
        s_weekrow[i] = ui_pixel_label(s_scr, WDAY[i], &lv_font_montserrat_14, UI_SKY_DARK);
        lv_obj_set_pos(s_weekrow[i], GRID_X + i * CELL_W, 74);
    }

    for (int r = 0; r < GRID_ROWS; r++) {
        for (int c = 0; c < GRID_COLS; c++) {
            lv_obj_t *cell = lv_obj_create(s_scr);
            lv_obj_remove_flag(cell, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_set_pos(cell, GRID_X + c * CELL_W, GRID_Y + r * ROW_PITCH);
            lv_obj_set_size(cell, CELL_W, CELL_H);
            lv_obj_set_style_radius(cell, 0, 0);
            lv_obj_set_style_border_width(cell, 0, 0);
            lv_obj_set_style_pad_all(cell, 0, 0);
            s_cells[r][c] = cell;

            lv_obj_t *lab = lv_label_create(cell);
            lv_obj_set_style_text_font(lab, &lv_font_montserrat_20, 0);
            lv_obj_center(lab);
            s_cell_lab[r][c] = lab;
            set_cell_blank(r, c);
        }
    }

    lv_obj_t *panel = ui_pixel_panel_create(s_scr, 10, INFO_Y, 220, INFO_H, UI_PAPER);
    s_info = lv_label_create(panel);
    lv_obj_set_width(s_info, 200);
    lv_obj_set_style_text_font(s_info, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_info, lv_color_hex(UI_INK), 0);
    lv_obj_set_style_text_align(s_info, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(s_info);

    s_idle_secs = 0;
    s_last_persist = 0;

    render_view();

    s_timer = lv_timer_create(tick, 1000, NULL);
    lv_screen_load(s_scr);
}

void demo_calendar_exit(void)
{
    if (s_timer) { lv_timer_delete(s_timer); s_timer = NULL; }
    net_cal_set_cb(NULL);                        // 取消校时订阅,避免悬垂回调
    nvs_persist();                           // 退出时落盘目标/纪念日/基准
    bsp_display_backlight(100);              // 复位背光(菜单可读)
    if (s_scr) {
        lv_obj_delete(s_scr);
        s_scr = NULL;
        s_header = NULL;
        s_info = NULL;
        for (int r = 0; r < GRID_ROWS; r++)
            for (int c = 0; c < GRID_COLS; c++) {
                s_cells[r][c] = NULL;
                s_cell_lab[r][c] = NULL;
            }
        for (int i = 0; i < 7; i++) s_weekrow[i] = NULL;
    }
}

void demo_calendar_key(bsp_btn_t btn, bsp_btn_ev_t ev)
{
    wake_display();

    if (ev == BSP_BTN_DOUBLE && btn == BSP_BTN_OK) {
        /* 纪念日 = 今天(存月/日,每年同月同日趣用) */
        refresh_today();
        s_anniv_m = s_today_m;
        s_anniv_d = s_today_d;
        render_view();
        return;
    }

    if (ev == BSP_BTN_CLICK) {
        if (btn == BSP_BTN_UP) {
            step_month(-1);
        } else if (btn == BSP_BTN_DOWN) {
            step_month(1);
        } else if (btn == BSP_BTN_OK) {
            /* 倒计时目标 = 今天,并跳到今天所在月 */
            refresh_today();
            s_tgt_days = s_today_days;
            s_view_y = s_today_y;
            s_view_m = s_today_m;
            render_view();
        }
        return;
    }

    if (ev == BSP_BTN_LONG && (btn == BSP_BTN_UP || btn == BSP_BTN_DOWN)) {
        /* 目标 ±1 天,并切到目标所在月以印证高亮 */
        int dir = (btn == BSP_BTN_UP) ? -1 : 1;
        uint8_t td;
        s_tgt_days += dir;
        cal_days_to_date(s_tgt_days, &s_view_y, &s_view_m, &td);
        render_view();
    }
}