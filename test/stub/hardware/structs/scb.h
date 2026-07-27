#ifndef STUB_HARDWARE_STRUCTS_SCB_H
#define STUB_HARDWARE_STRUCTS_SCB_H

/* Host stub for <hardware/structs/scb.h>: just enough of the Cortex-M0+ System
 * Control Block for cmd_reboot's direct system-reset write
 * (hw_set_bits(&scb_hw->aircr, ...SYSRESETREQ...)). On-device scb_hw is a fixed
 * MMIO block and hw_set_bits/the AIRCR bit come in transitively via pico-sdk;
 * the whole-codebase coverage build compiles this path but never links or runs
 * it, so a pointer-to-fixed-address (the SDK's own idiom) plus a no-op
 * hw_set_bits are enough to type-check. */

#include <stdint.h>

typedef struct {
    volatile uint32_t aircr;
} scb_hw_t;

#define scb_hw ((scb_hw_t *)0)

#define M0PLUS_AIRCR_SYSRESETREQ_BITS 0x04u

static inline void hw_set_bits(volatile uint32_t *addr, uint32_t mask) {
    *addr |= mask;
}

#endif
