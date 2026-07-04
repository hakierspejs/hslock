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

void clock_set_from_unix_time(uint32_t unix_time) {
     time_t t = (time_t)unix_time;
    struct tm *utc = gmtime(&t);

    datetime_t dt = {
        .year  = utc->tm_year + 1900,
        .month = utc->tm_mon  + 1,
        .day   = utc->tm_mday,
        .dotw  = utc->tm_wday,
        .hour  = utc->tm_hour,
        .min   = utc->tm_min,
        .sec   = utc->tm_sec,
    };

    rtc_set_datetime(&dt);
}
