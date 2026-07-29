#ifndef STUB_HARDWARE_SYNC_H
#define STUB_HARDWARE_SYNC_H

/* Host stub for <hardware/sync.h>: on-device this is the ARM CMSIS compiler
 * intrinsic (a real DMB instruction); the whole-codebase coverage build
 * compiles main.c/core1.c's cross-core mailbox path but never links or runs
 * it on a single host thread, so a no-op is enough to type-check. */

static inline void __dmb(void) {
}

#endif
