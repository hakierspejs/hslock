#ifndef HSLOCK_TEST_STUB_PICO_STDLIB_H
#define HSLOCK_TEST_STUB_PICO_STDLIB_H
/*
 * Host stub for the Pico SDK "pico/stdlib.h". Provides just enough no-op GPIO,
 * timing and stdio shims for first-party firmware .c files to COMPILE natively
 * so the coverage build can emit a .gcno for each (making untested files show
 * up at their true 0%). These are never linked into a running harness.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

typedef unsigned int uint;

#define GPIO_OUT           1
#define GPIO_IN            0
#define PICO_ERROR_TIMEOUT (-1)

static inline void gpio_init(uint pin) {
    (void)pin;
}
static inline void gpio_set_dir(uint pin, int out) {
    (void)pin;
    (void)out;
}
static inline void gpio_put(uint pin, bool value) {
    (void)pin;
    (void)value;
}
static inline int gpio_get(uint pin) {
    (void)pin;
    return 1;
}
static inline void gpio_pull_up(uint pin) {
    (void)pin;
}
static inline void sleep_ms(uint32_t ms) {
    (void)ms;
}
static inline void sleep_us(uint64_t us) {
    (void)us;
}
static inline void stdio_init_all(void) {
}
static inline int getchar_timeout_us(uint32_t timeout) {
    (void)timeout;
    return PICO_ERROR_TIMEOUT;
}
static inline void tight_loop_contents(void) {
}

#endif
