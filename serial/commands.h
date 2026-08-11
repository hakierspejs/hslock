#ifndef COMMANDS_H
#define COMMANDS_H

#include <stdbool.h>

// Admin sessions auto-expire after this many microseconds of inactivity. The
// deadline is armed on login and refreshed by every successful admin command,
// so an unattended-but-still-connected host cannot keep one login alive forever.
#define ADMIN_IDLE_TIMEOUT_US (5ULL * 60 * 1000000ULL)

// Dispatch a parsed command line
void commands_dispatch(int argc, char **argv);

// Returns true if currently in admin mode
bool commands_is_admin(void);

// Enter (or renew) admin mode and (re)arm the idle-timeout deadline. Called by
// cmd_login on every successful grant and by the dispatcher on admin activity.
void commands_admin_grant(void);

// Called by console on USB disconnect - auto-logout
void commands_on_disconnect(void);

#endif
