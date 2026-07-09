#ifndef STUB_PICO_STDLIB_H
#define STUB_PICO_STDLIB_H

/*
 * Host stub for <pico/stdlib.h>: the GPIO, stdio-init and timing shims the
 * firmware pulls in. Timing lives in <pico/time.h>, as on real hardware.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "pico/time.h"

typedef unsigned int uint;

#define GPIO_OUT 1
#define GPIO_IN  0

#define PICO_OK            0
#define PICO_ERROR_TIMEOUT (-1)

#ifndef PICO_FLASH_SIZE_BYTES
#define PICO_FLASH_SIZE_BYTES (2 * 1024 * 1024)
#endif

void gpio_init(uint pin);
void gpio_set_dir(uint pin, bool out);
void gpio_put(uint pin, bool value);
bool gpio_get(uint pin);
void gpio_pull_up(uint pin);

void stdio_init_all(void);
int  getchar_timeout_us(uint32_t timeout_us);

static inline void tight_loop_contents(void) {
}

#endif
