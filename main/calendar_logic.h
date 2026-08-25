// main/calendar_logic.h —— 公历日期纯逻辑(可在固件与宿主测试中共用)。
//
// 与 ESP-IDF / LVGL 无关,只依赖 stdint/stdbool,保证:
//   1. 可独立做宿主单元测试;
//   2. 无堆无锁,可在任意任务上下文调用。
#pragma once

#include <stdbool.h>
#include <stdint.h>

// 公历闰年判定(格里高利历)。年份适用范围偏差只在闰日上,普通日期 1..9999 均可。
bool cal_is_leap(uint16_t year);

// 某年某月(1..12)的天数(2 月按闰年处理)。
uint8_t cal_days_in_month(uint16_t year, uint8_t month);

// 把公历日期折算为自 1970-01-01(星期四)起的天数(序列日)。
// 结果可正可负;此函数是其余计算的地基。
int64_t cal_date_to_days(uint16_t year, uint8_t month, uint8_t day);

// 从序列日折算回日期(cal_date_to_days 的逆运算)。
void cal_days_to_date(int64_t days, uint16_t *year, uint8_t *month,
                      uint8_t *day);

// 返回某年某月 1 号是星期几:0=周日,1=周一,...6=周六。
uint8_t cal_first_weekday(uint16_t year, uint8_t month);

// 两日期相差的天数 = date2 的序列日 - date1 的序列日(带符号)。
// 同日返回 0;date2 晚于 date1 返回正数。用于“距离目标还有几天”。
int64_t cal_days_between(uint16_t y1, uint8_t m1, uint8_t d1,
                         uint16_t y2, uint8_t m2, uint8_t d2);

// 把日期平移 delta 天(负数为往前),结果写回。
// y/m/d 作为指针传入,允许 1..9999 年。
void cal_add_days(uint16_t *year, uint8_t *month, uint8_t *day, int delta);