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
    static const int days_in_month[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

    uint32_t remaining = unix_time;
    uint32_t secs      = remaining % 60;
    remaining /= 60;
    uint32_t mins = remaining % 60;
    remaining /= 60;
    uint32_t hours = remaining % 24;
    remaining /= 24;

    uint32_t year = 1970;
    while (true) {
        bool     leap         = (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
        uint32_t days_in_year = leap ? 366 : 365;
        if (remaining < days_in_year)
            break;
        remaining -= days_in_year;
        year++;
    }

    bool     leap  = (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
    uint32_t month = 1;
    while (true) {
        uint32_t dim = days_in_month[month - 1];
        if (month == 2 && leap)
            dim++;
        if (remaining < dim)
            break;
        remaining -= dim;
        month++;
    }

    uint32_t day = remaining + 1;

    uint32_t y = year, m = month;
    if (m < 3) {
        m += 12;
        y--;
    }
    uint32_t dotw = (day + (13 * (m + 1) / 5) + y + y / 4 - y / 100 + y / 400) % 7;
    dotw          = (dotw + 6) % 7;

    datetime_t dt = {
        .year  = (int16_t)year,
        .month = (int8_t)month,
        .day   = (int8_t)day,
        .dotw  = (int8_t)dotw,
        .hour  = (int8_t)hours,
        .min   = (int8_t)mins,
        .sec   = (int8_t)secs,
    };

    rtc_set_datetime(&dt);
}