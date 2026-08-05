#ifndef HSLOCK_DEBUG_H
#define HSLOCK_DEBUG_H

// Compile-time gate for diagnostic console output.
//
// Some diagnostics leak security-relevant information over the USB console
// (key-id enumeration, holder names, per-grant occupancy). They are useful
// during development but must NOT be present in a shipped build, where anyone
// with a cable could passively read a live occupancy/attendance log.
//
// DBG(...) expands to printf(...) only when HSLOCK_DEBUG is a non-zero value;
// otherwise it expands to nothing (arguments are NOT evaluated). Define
// HSLOCK_DEBUG=1 (e.g. via -DHSLOCK_DEBUG=1) to re-enable the diagnostics.
// The shipped build leaves it at the default of 0.

#include <stdio.h>

#ifndef HSLOCK_DEBUG
#define HSLOCK_DEBUG 0
#endif

#if HSLOCK_DEBUG
#define DBG(...) printf(__VA_ARGS__)
#else
#define DBG(...) ((void)0)
#endif

#endif // HSLOCK_DEBUG_H
