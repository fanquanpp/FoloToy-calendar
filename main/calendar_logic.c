// main/calendar_logic.c —— 公历日期纯逻辑实现。
//
// 核心采用「历法序列日」算法(days_from_civil / civil_from_days),
// 以 1970-01-01(星期四)为基准,把日期折算为连续整数天数,再在其上做
// 差值、平移,最后换算回日期。该算法对 1..9999 年通用且无需循环。
#include "calendar_logic.h"

// 自 1970-01-01 起的历法序列日(days_from_civil)。
static int64_t days_from_civil(int y, unsigned m, unsigned d)
{
    y -= (int)(m <= 2);
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);              /* [0,399]  */
    const unsigned doy =
        (153u * (unsigned)(m + (m > 2 ? -3 : 9)) + 2u) / 5u + d - 1u; /* [0,365] */
    const unsigned doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;   /* [0,146096] */
    return (int64_t)era * 146097LL + (int64_t)doe - 719468LL;
}

// 序列日反算回日期(civil_from_days)。
static void civil_from_days(int64_t z, int *y, unsigned *m, unsigned *d)
{
    z += 719468;
    const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe =
        (unsigned)(z - era * 146097);                              /* [0,146096] */
    const unsigned yoe =
        (doe - doe / 1460u + doe / 36524u - doe / 146096u) / 365u; /* [0,399]   */
    const int yy = (int)yoe + (int)(era * 400);
    const unsigned doy =
        doe - (365u * yoe + yoe / 4u - yoe / 100u);                /* [0,365]   */
    const unsigned mp = (5u * doy + 2u) / 153u;                    /* [0,11]    */
    const unsigned dd = doy - (153u * mp + 2u) / 5u + 1u;          /* [1,31]    */
    const unsigned mm = mp < 10u ? mp + 3u : mp - 9u;              /* [1,12]    */
    *y = yy + (int)(mm <= 2);
    *m = mm;
    *d = dd;
}

bool cal_is_leap(uint16_t year)
{
    return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

uint8_t cal_days_in_month(uint16_t year, uint8_t month)
{
    static const uint8_t T[12] = { 31, 28, 31, 30, 31, 30,
                                   31, 31, 30, 31, 30, 31 };
    uint8_t n = T[(unsigned)(month - 1) % 12u];
    if (month == 2 && cal_is_leap(year)) n++;   /* 闰年 2 月 29 天 */
    return n;
}

int64_t cal_date_to_days(uint16_t year, uint8_t month, uint8_t day)
{
    return days_from_civil((int)year, (unsigned)month, (unsigned)day);
}

void cal_days_to_date(int64_t days, uint16_t *year, uint8_t *month,
                      uint8_t *day)
{
    int y;
    unsigned m, d;
    civil_from_days(days, &y, &m, &d);
    *year = (uint16_t)y;
    *month = (uint8_t)m;
    *day = (uint8_t)d;
}

uint8_t cal_first_weekday(uint16_t year, uint8_t month)
{
    /* 1970-01-01 为星期四 => 0=周日 下索引为 4。 */
    int64_t days = days_from_civil((int)year, (unsigned)month, 1u);
    return (uint8_t)(((4 + days) % 7 + 7) % 7);
}

int64_t cal_days_between(uint16_t y1, uint8_t m1, uint8_t d1,
                         uint16_t y2, uint8_t m2, uint8_t d2)
{
    return days_from_civil((int)y2, (unsigned)m2, (unsigned)d2) -
           days_from_civil((int)y1, (unsigned)m1, (unsigned)d1);
}

void cal_add_days(uint16_t *year, uint8_t *month, uint8_t *day, int delta)
{
    int64_t days = days_from_civil((int)*year, (unsigned)*month,
                                   (unsigned)*day) + delta;
    int y;
    unsigned m, d;
    civil_from_days(days, &y, &m, &d);
    *year = (uint16_t)y;
    *month = (uint8_t)m;
    *day = (uint8_t)d;
}