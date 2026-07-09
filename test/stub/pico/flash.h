#ifndef STUB_PICO_FLASH_H
#define STUB_PICO_FLASH_H

/*
 * Host stub for <pico/flash.h>: flash_safe_execute plus the "keep this code
 * out of flash" attribute, which is a no-op on the host.
 */

#include <stdint.h>

#define __no_inline_not_in_flash_func(name) name

int flash_safe_execute(void (*func)(void *), void *param, uint32_t enter_exit_timeout_ms);

#endif
