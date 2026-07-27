#ifndef STUB_HARDWARE_WATCHDOG_H
#define STUB_HARDWARE_WATCHDOG_H

/* Host stub for <hardware/watchdog.h>: the reboot entry point. */

#include <stdbool.h>
#include <stdint.h>

void watchdog_reboot(uint32_t pc, uint32_t sp, uint32_t delay_ms);
void watchdog_enable(uint32_t delay_ms, bool pause_on_debug);

#endif
