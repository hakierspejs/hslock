#ifndef CONSOLE_H
#define CONSOLE_H

#include <stdbool.h>

// Call once at startup
void console_init(void);

// Call in core0 main loop — non-blocking
void console_task(void);

#endif
