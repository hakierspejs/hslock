#ifndef HSLOCK_TEST_STUB_PICO_FLASH_H
#define HSLOCK_TEST_STUB_PICO_FLASH_H
/*
 * Host stub for the Pico SDK "pico/flash.h" - flash_safe_execute() shim used by
 * storage/storage.c to run erase/program callbacks with the other core paused.
 * On the host it just invokes the callback so the file COMPILES for coverage;
 * never linked into a running harness.
 */
#include <stdint.h>

#ifndef PICO_OK
#define PICO_OK 0
#endif

static inline int flash_safe_execute(void (*func)(void *), void *param, uint32_t timeout_ms) {
    (void)func;
    (void)param;
    (void)timeout_ms;
    return PICO_OK;
}

#endif
