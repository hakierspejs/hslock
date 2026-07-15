#ifndef STUB_HARDWARE_RTC_H
#define STUB_HARDWARE_RTC_H

/* Host stub for <hardware/rtc.h>: the RP2040 RTC datetime type and API. */

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    int16_t year;
    int8_t  month;
    int8_t  day;
    int8_t  dotw;
    int8_t  hour;
    int8_t  min;
    int8_t  sec;
} datetime_t;

void rtc_init(void);
bool rtc_get_datetime(datetime_t *t);
bool rtc_set_datetime(const datetime_t *t);

#endif
