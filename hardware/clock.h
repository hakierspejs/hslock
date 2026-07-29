#ifndef CLOCK_H
#define CLOCK_H
#include "hardware/rtc.h"
#include <stdbool.h>

// Reads the RTC. Returns false (leaving *out_unix_time untouched) if the RTC
// has never been set - epoch 0 (1970-01-01 00:00:00) is a legitimate time and
// must not be conflated with "unset".
bool clock_get_unix_time(uint32_t *out_unix_time);

void clock_set_from_unix_time(uint32_t unix_time);

#endif