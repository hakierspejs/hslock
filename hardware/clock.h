#ifndef CLOCK_H
#define CLOCK_H
#include "hardware/rtc.h"
#include <time.h>

uint32_t clock_get_unix_time(void);

void clock_set_from_unix_time(uint32_t unix_time);

#endif