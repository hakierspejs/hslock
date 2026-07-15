#ifndef STUB_HARDWARE_FLASH_H
#define STUB_HARDWARE_FLASH_H

/*
 * Host stub for <hardware/flash.h>: flash geometry and the raw
 * program/erase primitives storage.c drives through flash_safe_execute.
 */

#include <stddef.h>
#include <stdint.h>

#define FLASH_SECTOR_SIZE 4096u
#define FLASH_PAGE_SIZE   256u

#ifndef XIP_BASE
#define XIP_BASE 0x10000000u
#endif

void flash_range_program(uint32_t flash_offs, const uint8_t *data, size_t count);
void flash_range_erase(uint32_t flash_offs, size_t count);

#endif
