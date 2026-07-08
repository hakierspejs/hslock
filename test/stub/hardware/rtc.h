#ifndef HSLOCK_TEST_STUB_HARDWARE_RTC_H
#define HSLOCK_TEST_STUB_HARDWARE_RTC_H
/* Host stub for the Pico SDK "hardware/rtc.h" - datetime_t + no-op RTC. */
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

static inline void rtc_init(void) {
}
static inline bool rtc_get_datetime(datetime_t *t) {
    (void)t;
    return false;
}
static inline bool rtc_set_datetime(const datetime_t *t) {
    (void)t;
    return true;
}

#endif
