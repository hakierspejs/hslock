#ifndef HSLOCK_TEST_STUB_PICO_STDIO_USB_H
#define HSLOCK_TEST_STUB_PICO_STDIO_USB_H
/* Host stub for "pico/stdio_usb.h" - USB CDC never connected on host. */
#include <stdbool.h>

static inline bool stdio_usb_connected(void) {
    return false;
}

#endif
