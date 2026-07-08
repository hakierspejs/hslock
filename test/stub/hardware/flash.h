#ifndef HSLOCK_TEST_STUB_HARDWARE_FLASH_H
#define HSLOCK_TEST_STUB_HARDWARE_FLASH_H
/*
 * Host stub for the Pico SDK "hardware/flash.h". Supplies the flash geometry
 * macros and no-op erase/program shims that storage/storage.c references, so
 * the file COMPILES natively for coverage (.gcno at true 0%). Never linked or
 * executed - the values only need to be well-formed, not hardware-accurate.
 */
#include <stddef.h>
#include <stdint.h>

// Flash geometry (RP2040 QSPI NOR).
#define FLASH_PAGE_SIZE   256u
#define FLASH_SECTOR_SIZE 4096u
#define FLASH_BLOCK_SIZE  65536u

// Execute-in-place window base and total flash size (Pico W: 2 MB).
#ifndef XIP_BASE
#define XIP_BASE 0x10000000u
#endif
#ifndef PICO_FLASH_SIZE_BYTES
#define PICO_FLASH_SIZE_BYTES (2u * 1024u * 1024u)
#endif

// RAM-resident attribute for flash-locked callbacks (no-op on host).
#ifndef __no_inline_not_in_flash_func
#define __no_inline_not_in_flash_func(name) name
#endif

static inline void flash_range_erase(uint32_t flash_offs, size_t count) {
    (void)flash_offs;
    (void)count;
}
static inline void flash_range_program(uint32_t flash_offs, const uint8_t *data, size_t count) {
    (void)flash_offs;
    (void)data;
    (void)count;
}

#endif
