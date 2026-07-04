#include "clock.h"

uint32_t clock_get_unix_time(void) {
    datetime_t dt;
    if (!rtc_get_datetime(&dt)) return 0;

    struct tm t = {
        .tm_year  = dt.year - 1900,
        .tm_mon   = dt.month - 1,
        .tm_mday  = dt.day,
        .tm_hour  = dt.hour,
        .tm_min   = dt.min,
        .tm_sec   = dt.sec,
        .tm_isdst = 0,
    };
    return (uint32_t)mktime(&t);
}