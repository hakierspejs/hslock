#ifndef STUB_PICO_TIME_H
#define STUB_PICO_TIME_H

/*
 * Host stub for <pico/time.h>: sleeps and the absolute-time helpers used by
 * the keypad debouncer and the NTP poll loop.
 */

#include <stdbool.h>
#include <stdint.h>

typedef uint64_t absolute_time_t;

void sleep_ms(uint32_t ms);
void sleep_us(uint64_t us);

absolute_time_t make_timeout_time_ms(uint32_t ms);
bool            time_reached(absolute_time_t t);
uint64_t        time_us_64(void);

/* Timer alarms, used by the non-blocking latch (hardware/latch.c). */
typedef int32_t alarm_id_t;
typedef int64_t (*alarm_callback_t)(alarm_id_t id, void *user_data);

alarm_id_t add_alarm_in_ms(uint32_t ms, alarm_callback_t callback, void *user_data,
                           bool fire_if_past);
bool       cancel_alarm(alarm_id_t alarm_id);

#endif
