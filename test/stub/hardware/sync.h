#ifndef STUB_HARDWARE_SYNC_H
#define STUB_HARDWARE_SYNC_H

/* Host stub for <hardware/sync.h>: on-device this is the ARM CMSIS compiler
 * intrinsic (a real DMB instruction); the whole-codebase coverage build
 * compiles main.c/core1.c's cross-core mailbox path but never links or runs
 * it on a single host thread, so a no-op is enough to type-check. */

static inline void __dmb(void) {
}

/* Host stubs for the RP2040 interrupt-disable critical section used by
 * network/ntp.c's 64-bit torn-read guard (M10b). On-device these save/restore
 * PRIMASK around a brief window; the coverage build compiles ntp.c but never
 * runs it, so type-checking no-ops suffice. */
#include <stdint.h>

static inline uint32_t save_and_disable_interrupts(void) {
    return 0;
}

static inline void restore_interrupts(uint32_t status) {
    (void)status;
}

#endif
