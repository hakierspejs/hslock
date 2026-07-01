#ifndef COMMANDS_H
#define COMMANDS_H

#include <stdbool.h>

// Dispatch a parsed command line
void commands_dispatch(int argc, char **argv);

// Returns true if currently in admin mode
bool commands_is_admin(void);

// Called by console on USB disconnect — auto-logout
void commands_on_disconnect(void);

#endif
