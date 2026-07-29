#ifndef CORE1_H
#define CORE1_H

#include <stdint.h>

void main1(void);

// Core 1 liveness heartbeat. Core 1 bumps this on every keypad-loop iteration
// (and while it waits on a door verdict); core 0 watches it in
// watchdog_feed_core0() and only pets the (system-wide) RP2040 watchdog when it
// has advanced. That way a wedged core 1 also lets the watchdog fire, instead
// of core 1 blindly feeding it while core 0 is stuck (ISSUES.md H4).
extern volatile uint32_t core1_heartbeat;

// Pet the hardware watchdog, but ONLY if core 1's heartbeat advanced since the
// last call. Must be called from core 0 exclusively - from the main service
// loop and from any long core-0 poll (e.g. ntp_sync). Never call from core 1.
void watchdog_feed_core0(void);

#endif
