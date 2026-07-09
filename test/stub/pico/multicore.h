#ifndef STUB_PICO_MULTICORE_H
#define STUB_PICO_MULTICORE_H

/* Host stub for <pico/multicore.h>: core-1 launch and inter-core FIFO. */

#include <stdint.h>

void     multicore_lockout_victim_init(void);
void     multicore_launch_core1(void (*entry)(void));
void     multicore_fifo_push_blocking(uint32_t data);
uint32_t multicore_fifo_pop_blocking(void);

#endif
