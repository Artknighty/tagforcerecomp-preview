/* libstory: the story calendar. The date record (Date, in the save data) is stored as a count of 15-minute steps; these functions turn it into the month,
 * day, hour, weekday and the days left until the tournament, and answer "is today between these dates". */
#include "../common/swap.h"
#include "story_data.h"

#define MODULE "libstory"

namespace {
using namespace story;

constexpr int32_t kMonthsPerYear = 8;
constexpr int32_t kMonthDays[kMonthsPerYear] = {31, 30, 31, 31, 30, 31, 30, 31};   /* the story's May .. December */
constexpr int32_t kFirstTickMinutes = 345;       /* the first step of a day is at 05:45 */
constexpr int32_t kTournamentDay = 92;           /* the day of the year of the tournament, counting from 1 */
constexpr int32_t kFirstCalendarMonth = 5;       /* the story's month 1 is May */
constexpr int32_t kWeekdayOfDay[7] = {1, 2, 3, 4, 5, 6, 0};   /* the weekday of the days 1 .. 7 of the year */

Date &currentDate(SwapCtx &x) { return x.at<Date>(datum<uint32_t>(x, kDatePointer)); }

/* ---- FUN_08f04f98(date): the day of the year, 1-based (the lengths of the months before this one, plus the day) ---- */
int32_t dayOfYear(const Date &date) {
    int32_t days = 0;
    for (int32_t month = 1; month < date.month && month <= kMonthsPerYear; month++) days += kMonthDays[month - 1];
    return days + date.dayOfMonth;
}
void swap_DayOfYear(SwapCtx &x) { x.ret((uint32_t)dayOfYear(x.at<Date>(x.arg(0)))); }
SwapReg reg_DayOfYear({"libstory.DayOfYear", MODULE, offsetOf(0x08F04F98), swap_DayOfYear, true});

/* ---- FUN_08f04988: the day of the year of the story's date ---- */
void swap_CurrentDayOfYear(SwapCtx &x) { x.ret((uint32_t)dayOfYear(currentDate(x))); }
SwapReg reg_CurrentDayOfYear({"libstory.CurrentDayOfYear", MODULE, offsetOf(0x08F04988), swap_CurrentDayOfYear, true});

/* ---- the fields of the date, one function each (FUN_08f04c6c .. FUN_08f04e8c) ---- */
void setCalendarMonth(Date &date, int32_t fallback) { date.calendarMonth = date.month >= 1 && date.month <= kMonthsPerYear ? date.month + kFirstCalendarMonth - 1 : fallback; }
void setHour(Date &date) { const int32_t minutes = (date.tick - 1) * 15 + kFirstTickMinutes; date.hour = minutes == 0 ? 0 : minutes / 60; }
void setMinute(Date &date, int32_t fallback) {
    int32_t step = (date.tick - 1) & 3;
    if (date.tick - 1 < 0 && step != 0) step -= 4;
    date.minute = step == 3 ? 30 : step == 2 ? 15 : step == 1 ? 0 : step == 0 ? 45 : fallback;
}
void setWeekday(Date &date) {
    const int32_t index = (dayOfYear(date) - 1) % 7;
    date.weekday = index >= 0 && index < 7 ? kWeekdayOfDay[index] : 1;
}
void setDaysLeft(Date &date) { const int32_t left = kTournamentDay - (dayOfYear(date) - 1); date.daysLeft = left < 0 ? 0 : left; }

void regions_Date(SwapCtx &x, Regions &out) { out.push_back({x.arg(0), sizeof(Date)}); }
void swap_SetCalendarMonth(SwapCtx &x) { setCalendarMonth(x.at<Date>(x.arg(0)), (int32_t)x.arg(2)); }
SwapReg reg_SetCalendarMonth({"libstory.SetCalendarMonth", MODULE, offsetOf(0x08F04C6C), swap_SetCalendarMonth, false, regions_Date});
void swap_SetHour(SwapCtx &x) { setHour(x.at<Date>(x.arg(0))); }
SwapReg reg_SetHour({"libstory.SetHour", MODULE, offsetOf(0x08F04D58), swap_SetHour, false, regions_Date});
void swap_SetMinute(SwapCtx &x) { setMinute(x.at<Date>(x.arg(0)), (int32_t)x.arg(2)); }
SwapReg reg_SetMinute({"libstory.SetMinute", MODULE, offsetOf(0x08F04DA0), swap_SetMinute, false, regions_Date});
void swap_SetWeekday(SwapCtx &x) { setWeekday(x.at<Date>(x.arg(0))); }
SwapReg reg_SetWeekday({"libstory.SetWeekday", MODULE, offsetOf(0x08F04DF0), swap_SetWeekday, false, regions_Date});
void swap_SetDaysLeft(SwapCtx &x) { setDaysLeft(x.at<Date>(x.arg(0))); }
SwapReg reg_SetDaysLeft({"libstory.SetDaysLeft", MODULE, offsetOf(0x08F04E8C), swap_SetDaysLeft, false, regions_Date});

/* ---- FUN_08f04b44(date): works the month, day and the other fields out of the step count in date.ticks ---- */
void fillDate(Date &date) {
    const int32_t tick = (date.ticks - 1) % kTicksPerDay;
    int32_t day = date.ticks - (tick + 1);
    if (0 < day) day /= kTicksPerDay;
    int32_t month = 1, monthDays = 0;
    for (;;) {
        if (month >= 1 && month <= kMonthsPerYear) monthDays = kMonthDays[month - 1];   /* past the eighth month the last length stays */
        if (day < monthDays) {
            date.month = month;
            date.dayOfMonth = day + 1;
            date.tick = tick + 1;
            setCalendarMonth(date, monthDays);
            setHour(date);
            setMinute(date, 0);
            setWeekday(date);
            setDaysLeft(date);
            return;
        }
        day -= monthDays;
        month++;
    }
}
void swap_FillDate(SwapCtx &x) { fillDate(x.at<Date>(x.arg(0))); }
SwapReg reg_FillDate({"libstory.FillDate", MODULE, offsetOf(0x08F04B44), swap_FillDate, false, regions_Date});

/* ---- FUN_08f04ec8(date): the value of the special day (a holiday) table for this date: the first entry of this month whose day is this day or 0 (any) ---- */
int32_t holidayValue(SwapCtx &x, const Date &date) {
    const int32_t *entry = (const int32_t *)x.mem(guest(x, kHolidayTable));
    int32_t index = 0;
    while (!(entry[0] == date.month && (entry[1] == date.dayOfMonth || entry[1] == 0))) { entry += 4; index++; }
    return ((const int32_t *)x.mem(guest(x, kHolidayTable)))[index * 4 + 2];
}
void swap_HolidayValue(SwapCtx &x) { x.ret((uint32_t)holidayValue(x, x.at<Date>(x.arg(0)))); }
SwapReg reg_HolidayValue({"libstory.HolidayValue", MODULE, offsetOf(0x08F04EC8), swap_HolidayValue, true});

/* ---- FUN_08f03fb8(days): the holiday value of the date `days` days after the story's date ---- */
void swap_HolidayValueAfter(SwapCtx &x) {
    const Date &today = currentDate(x);
    Date later = {};
    later.ticks = today.ticks + (int32_t)x.arg(0) * kTicksPerDay;
    later.month = today.month;
    later.dayOfMonth = today.dayOfMonth;
    later.tick = today.tick;
    fillDate(later);
    x.ret((uint32_t)holidayValue(x, later));
}
SwapReg reg_HolidayValueAfter({"libstory.HolidayValueAfter", MODULE, offsetOf(0x08F03FB8), swap_HolidayValueAfter, true});

/* ---- FUN_08f04994(fromMonth, fromDay, toMonth, toDay): is today between the two dates (all four 0 = always) ---- */
void swap_TodayInRange(SwapCtx &x) {
    const int32_t fromMonth = (int32_t)x.arg(0), fromDay = (int32_t)x.arg(1), toMonth = (int32_t)x.arg(2), toDay = (int32_t)x.arg(3);
    const Date &today = currentDate(x);
    if (fromMonth == 0 && fromDay == 0 && toMonth == 0 && toDay == 0) { x.ret(1); return; }
    const int32_t now = today.month * 100 + today.dayOfMonth;
    x.ret(now < fromMonth * 100 + fromDay || toMonth * 100 + toDay < now ? 0 : 1);
}
SwapReg reg_TodayInRange({"libstory.TodayInRange", MODULE, offsetOf(0x08F04994), swap_TodayInRange, true});

/* ---- FUN_08f04a7c: has the day of the year changed since the last time this was asked? (remembers it) ---- */
void swap_DayChanged(SwapCtx &x) {
    int32_t &last = x.at<int32_t>(datum<uint32_t>(x, kLastDayPointer));
    const int32_t today = dayOfYear(currentDate(x));
    const bool changed = last != today;
    if (changed) last = today;
    x.ret(changed ? 1 : 0);
}
void regions_DayChanged(SwapCtx &x, Regions &out) { out.push_back({datum<uint32_t>(x, kLastDayPointer), 4}); }
SwapReg reg_DayChanged({"libstory.DayChanged", MODULE, offsetOf(0x08F04A7C), swap_DayChanged, false, regions_DayChanged});

/* ---- the current date's fields (FUN_08f03f48 / 03f58 / 03f78), and two questions about the date ---- */
void swap_CurrentTick(SwapCtx &x) { x.ret((uint32_t)currentDate(x).tick); }
SwapReg reg_CurrentTick({"libstory.CurrentTick", MODULE, offsetOf(0x08F03F48), swap_CurrentTick, true});
void swap_CurrentMonth(SwapCtx &x) { x.ret((uint32_t)currentDate(x).month); }
SwapReg reg_CurrentMonth({"libstory.CurrentMonth", MODULE, offsetOf(0x08F03F58), swap_CurrentMonth, true});
void swap_CurrentDay(SwapCtx &x) { x.ret((uint32_t)currentDate(x).dayOfMonth); }
SwapReg reg_CurrentDay({"libstory.CurrentDay", MODULE, offsetOf(0x08F03F78), swap_CurrentDay, true});

/* FUN_08f0405c: 0 from month 1 day 24 until month 2 day 23 (the first break of the story), else 1 */
void swap_OutsideFirstBreak(SwapCtx &x) {
    const Date &date = currentDate(x);
    uint32_t result = 1;
    if (date.month == 1) { if (0x17 < date.dayOfMonth) result = 0; }
    else if (date.month == 2 && date.dayOfMonth < 0x18) result = 0;
    x.ret(result);
}
SwapReg reg_OutsideFirstBreak({"libstory.OutsideFirstBreak", MODULE, offsetOf(0x08F0405C), swap_OutsideFirstBreak, true});

/* FUN_08f040b4: 1 from month 2 day 24 on (after the first break) */
void swap_AfterFirstBreak(SwapCtx &x) {
    const Date &date = currentDate(x);
    uint32_t result = 0;
    if (date.month == 2) { if (0x17 < date.dayOfMonth) result = 1; }
    else if (2 < date.month) result = 1;
    x.ret(result);
}
SwapReg reg_AfterFirstBreak({"libstory.AfterFirstBreak", MODULE, offsetOf(0x08F040B4), swap_AfterFirstBreak, true});

}  // namespace
