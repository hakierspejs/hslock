#include "clock.h"

uint32_t clock_get_unix_time(void) {
    datetime_t dt;
    if (!rtc_get_datetime(&dt))
        return 0;

    // Days per month (non-leap year)
    static const int days_in_month[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

    // Count days from 1970 to current date
    uint32_t days = 0;

    // Full years since 1970
    for (int y = 1970; y < dt.year; y++) {
        bool leap = (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
        days += leap ? 366 : 365;
    }

    // Full months this year
    bool this_leap = (dt.year % 4 == 0 && dt.year % 100 != 0) || (dt.year % 400 == 0);
    for (int m = 1; m < dt.month; m++) {
        days += days_in_month[m - 1];
        if (m == 2 && this_leap)
            days++;
    }

    // Days this month (minus 1 since today isn't complete)
    days += dt.day - 1;

    return days * 86400 + dt.hour * 3600 + dt.min * 60 + dt.sec;
}

void clock_set_from_unix_time(uint32_t unix_time) {
    time_t     t   = (time_t)unix_time;
    struct tm *utc = gmtime(&t);

    datetime_t dt = {
        .year  = utc->tm_year + 1900,
        .month = utc->tm_mon + 1,
        .day   = utc->tm_mday,
        .dotw  = utc->tm_wday,
        .hour  = utc->tm_hour,
        .min   = utc->tm_min,
        .sec   = utc->tm_sec,
    };

    rtc_set_datetime(&dt);
}
