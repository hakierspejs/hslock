#ifndef HSLOCK_TEST_STUB_HARDWARE_SYNC_H
#define HSLOCK_TEST_STUB_HARDWARE_SYNC_H
/*
 * Host stub for the Pico SDK "hardware/sync.h" - no-op interrupt save/restore.
 * Present only so first-party firmware .c files that include it COMPILE for the
 * coverage build; never linked into a running harness.
 */
#include <stdint.h>

static inline uint32_t save_and_disable_interrupts(void) {
    return 0;
}
static inline void restore_interrupts(uint32_t status) {
    (void)status;
}
static inline void __dmb(void) {
}

#endif
