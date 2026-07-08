#ifndef HSLOCK_TEST_STUB_PICO_MULTICORE_H
#define HSLOCK_TEST_STUB_PICO_MULTICORE_H
/* Host stub for "pico/multicore.h" - no-op core1 / FIFO shims. */
#include <stdint.h>

static inline void multicore_lockout_victim_init(void) {
}
static inline void multicore_fifo_push_blocking(uint32_t v) {
    (void)v;
}
static inline uint32_t multicore_fifo_pop_blocking(void) {
    return 0;
}
static inline void multicore_launch_core1(void (*entry)(void)) {
    (void)entry;
}

#endif
