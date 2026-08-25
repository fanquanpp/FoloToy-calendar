// tests/test_calendar_logic.c —— 公历日期纯逻辑宿主单元测试。
// 由 validate.sh --static 编译为本地可执行文件并运行(与 ui_pixel 测试同理)。
#include <assert.h>
#include <stdint.h>
#include "calendar_logic.h"

static void test_leap(void)
{
    assert(cal_is_leap(2000));
    assert(cal_is_leap(2024));
    assert(cal_is_leap(1996));
    assert(!cal_is_leap(1900));
    assert(!cal_is_leap(2023));
    assert(!cal_is_leap(2026));
}

static void test_months(void)
{
    assert(cal_days_in_month(2024, 2) == 29);   // 闰年 2 月
    assert(cal_days_in_month(2023, 2) == 28);   // 平年 2 月
    assert(cal_days_in_month(2000, 2) == 29);   // 百年闰年 2 月
    assert(cal_days_in_month(1900, 2) == 28);   // 百年非闰 2 月
    assert(cal_days_in_month(2026, 1) == 31);
    assert(cal_days_in_month(2026, 4) == 30);
    assert(cal_days_in_month(2026, 6) == 30);
    assert(cal_days_in_month(2026, 12) == 31);
}

static void test_epoch(void)
{
    assert(cal_date_to_days(1970, 1, 1) == 0);       // 基准日
    assert(cal_date_to_days(2000, 1, 1) == 10957);   // 天文纪元锚点

    uint16_t y; uint8_t m, d;
    cal_days_to_date(0, &y, &m, &d);
    assert(y == 1970 && m == 1 && d == 1);
    cal_days_to_date(10957, &y, &m, &d);
    assert(y == 2000 && m == 1 && d == 1);
}

static void test_roundtrip(void)
{
    /* 多点正反换同在 1..9999 年上的若干代表性日期上自洽 */
    static const uint16_t Y[] = { 1, 1970, 1972, 1999, 2000, 2024, 2026, 2099, 9999 };
    for (size_t i = 0; i < sizeof(Y) / sizeof(Y[0]); i++) {
        for (uint8_t m = 1; m <= 12; m++) {
            uint8_t dim = cal_days_in_month(Y[i], m);
            uint8_t day = (uint8_t)((dim > 1) ? (dim / 2 + 1) : 1);  /* 取月中日 */
            int64_t d = cal_date_to_days(Y[i], m, day);

            uint16_t y; uint8_t mm, dd;
            cal_days_to_date(d, &y, &mm, &dd);
            assert(y == Y[i] && mm == m && dd == day);
        }
    }
}

static void test_weekday(void)
{
    /* 1970-01-01 是星期四 => 0=周日 下为 4 */
    assert(cal_first_weekday(1970, 1) == 4);
    /* 2000-01-01 是星期六 => 6 */
    assert(cal_first_weekday(2000, 1) == 6);
    /* 2023-01-01 是星期日 => 0 */
    assert(cal_first_weekday(2023, 1) == 0);
    /* 2026-01-01 是星期四 => 4(可在线校验) */
    assert(cal_first_weekday(2026, 1) == 4);
}

static void test_between(void)
{
    assert(cal_days_between(2026, 8, 25, 2026, 8, 25) == 0);
    assert(cal_days_between(2026, 8, 25, 2026, 8, 26) == 1);
    assert(cal_days_between(2026, 9, 1, 2026, 8, 31) == -1);
    assert(cal_days_between(2024, 2, 28, 2024, 3, 1) == 2);   // 越过闰日
    assert(cal_days_between(2026, 1, 1, 2027, 1, 1) == 365);  // 平年整年
}

static void test_add_days(void)
{
    uint16_t y; uint8_t m, d;

    y = 2024; m = 2; d = 28; cal_add_days(&y, &m, &d, 1);
    assert(y == 2024 && m == 2 && d == 29);   // 生成闰日

    y = 2023; m = 2; d = 28; cal_add_days(&y, &m, &d, 1);
    assert(y == 2023 && m == 3 && d == 1);    // 平年跨月

    y = 2026; m = 12; d = 31; cal_add_days(&y, &m, &d, 1);
    assert(y == 2027 && m == 1 && d == 1);    // 跨年

    y = 2026; m = 1; d = 1; cal_add_days(&y, &m, &d, -1);
    assert(y == 2025 && m == 12 && d == 31);  // 回跨年

    y = 1970; m = 1; d = 1; cal_add_days(&y, &m, &d, 1000000);  // 大跨度
    int64_t back = cal_date_to_days(y, m, d);
    assert(back == 1000000);
}

int main(void)
{
    test_leap();
    test_months();
    test_epoch();
    test_roundtrip();
    test_weekday();
    test_between();
    test_add_days();
    return 0;
}