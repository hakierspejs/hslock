#ifndef CLOCK_H
#define CLOCK_H
#include "hardware/rtc.h"
#include <time.h>

uint32_t clock_get_unix_time(void);

#endif