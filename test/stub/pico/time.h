#ifndef HSLOCK_TEST_STUB_PICO_TIME_H
#define HSLOCK_TEST_STUB_PICO_TIME_H
/* Host stub for "pico/time.h" - opaque absolute_time_t + no-op timers. */
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    int64_t _us;
} absolute_time_t;

static inline absolute_time_t make_timeout_time_ms(uint32_t ms) {
    absolute_time_t t = {(int64_t)ms};
    return t;
}
static inline bool time_reached(absolute_time_t t) {
    (void)t;
    return true;
}

#endif
