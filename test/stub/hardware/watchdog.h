#ifndef HSLOCK_TEST_STUB_HARDWARE_WATCHDOG_H
#define HSLOCK_TEST_STUB_HARDWARE_WATCHDOG_H
/* Host stub for the Pico SDK "hardware/watchdog.h" - no-op reboot. */
#include <stdint.h>

static inline void watchdog_reboot(uint32_t pc, uint32_t sp, uint32_t delay_ms) {
    (void)pc;
    (void)sp;
    (void)delay_ms;
}

#endif
