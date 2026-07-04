#ifndef COMMANDS_HANDLERS_H
#define COMMANDS_HANDLERS_H

#include <stdbool.h>

// Shared admin state - defined in commands.c
extern bool admin_mode;

// System
void cmd_status(int argc, char **argv);
void cmd_get_time(int argc, char **argv);
void cmd_test(int argc, char **argv);
void cmd_login(int argc, char **argv);
void cmd_logout(int argc, char **argv);
void cmd_reboot(int argc, char **argv);

// Network
void cmd_sync_ntp(int argc, char **argv);
void cmd_set_wifi(int argc, char **argv);

// Keys
void cmd_list_keys(int argc, char **argv);
void cmd_get_key(int argc, char **argv);
void cmd_get_key_secret(int argc, char **argv);
void cmd_add_key(int argc, char **argv);
void cmd_rename_key(int argc, char **argv);
void cmd_enable_key(int argc, char **argv);
void cmd_disable_key(int argc, char **argv);
void cmd_delete_key(int argc, char **argv);
void cmd_set_key_admin(int argc, char **argv);
void cmd_unset_key_admin(int argc, char **argv);

// Backup
void cmd_export_keys(int argc, char **argv);
void cmd_import_keys(int argc, char **argv);

#endif
