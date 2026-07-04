#include "commands.h"
#include "commands_handlers.h"
#include "hardware/buzzer.h"
#include "pico/stdlib.h"
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

// ---------------------------------------------------------------------------
// Admin state
// ---------------------------------------------------------------------------

bool admin_mode = false;

bool commands_is_admin(void) {
    return admin_mode;
}

void commands_on_disconnect(void) {
    if (admin_mode) {
        admin_mode = false;
        printf("[console] USB disconnected — admin session ended\r\n");
    }
}

// ---------------------------------------------------------------------------
// Command handler type
// ---------------------------------------------------------------------------

typedef struct {
    const char *name;
    bool        requires_admin;
    int         min_args;
    int         max_args;
    const char *usage;
    const char *description;
    void      (*handler)(int argc, char **argv);
} command_t;

// ---------------------------------------------------------------------------
// Command table
// ---------------------------------------------------------------------------

static const command_t COMMANDS[] = {
    {"help",              false,  0,  0,   "help",                          "Print available commands"},
    {"?",                 false,  0,  0,   "?",                             "Alias for help"},
    {"status",            false,  0,  0,   "status",                        "Show system status"},
    {"test",              false,  0,  0,   "test",                          "Test LED, buzzer and latch"},
    {"login",             false,  1,  1,   "login <otp>",                   "Enable admin mode via TOTP"},
    {"logout",            true,   0,  0,   "logout",                        "End admin session"},
    {"reboot",            true,   0,  0,   "reboot",                        "Reboot device"},
    {"get-time",          true,   0,  0,   "get-time",                      "Show current RTC time and last NTP sync"},
    {"sync-ntp",          true,   0,  0,   "sync-ntp",                      "Force immediate NTP resync"},
    {"set-wifi",          true,   2,  2,   "set-wifi <ssid> <password>",    "Save WiFi credentials"},
    {"list-keys",         true,   0,  0,   "list-keys",                     "List all keys (no secrets)"},
    {"get-key",           true,   1,  1,   "get-key <id>",                  "Show key details (no secret)"},
    {"get-key-secret",    true,   1,  1,   "get-key-secret <id>",           "Show secret + QR code for key"},
    {"add-key",           true,   2,  2,   "add-key <id> <name>",           "Generate and save new key"},
    {"rename-key",        true,   2,  2,   "rename-key <id> <name>",        "Rename a key"},
    {"enable-key",        true,   1,  1,   "enable-key <id>",               "Enable a disabled key"},
    {"disable-key",       true,   1,  1,   "disable-key <id>",              "Disable a key without deleting"},
    {"delete-key",        true,   1,  1,   "delete-key <id>",               "Permanently delete a key"},
    {"set-key-admin",     true,   1,  1,   "set-key-admin <id>",            "Grant admin flag to key"},
    {"unset-key-admin",   true,   1,  1,   "unset-key-admin <id>",          "Remove admin flag from key"},
    {"export-keys",       true,   0,  0,   "export-keys",                   "Dump all keys as base64"},
    {"import-keys",       true,   1,  1,   "import-keys <base64>",          "Export backup then overwrite with provided data"},
};

#define NUM_COMMANDS (sizeof(COMMANDS) / sizeof(COMMANDS[0]))

// ---------------------------------------------------------------------------
// Help — defined after table
// ---------------------------------------------------------------------------

static void cmd_help(int argc, char **argv) {
    printf("\r\navailable commands:\r\n\r\n");
    printf("  %-32s  %s\r\n", "usage", "description");
    printf("  %-32s  %s\r\n",
           "--------------------------------",
           "-----------------------------------");
    for (size_t i = 0; i < NUM_COMMANDS; i++) {
        const command_t *cmd = &COMMANDS[i];
        if (cmd->requires_admin && !admin_mode) continue;
        if (strcmp(cmd->name, "?") == 0) continue;
        printf("  %-32s  %s\r\n", cmd->usage, cmd->description);
    }
    printf("\r\n");
    buzzer_beep_short();
}

// Handler lookup — must match COMMANDS table order
static void (*const HANDLERS[])(int, char**) = {
    cmd_help,
    cmd_help,
    cmd_status,
    cmd_test,
    cmd_login,
    cmd_logout,
    cmd_reboot,
    cmd_get_time,
    cmd_sync_ntp,
    cmd_set_wifi,
    cmd_list_keys,
    cmd_get_key,
    cmd_get_key_secret,
    cmd_add_key,
    cmd_rename_key,
    cmd_enable_key,
    cmd_disable_key,
    cmd_delete_key,
    cmd_set_key_admin,
    cmd_unset_key_admin,
    cmd_export_keys,
    cmd_import_keys,
};

// ---------------------------------------------------------------------------
// Dispatch
// ---------------------------------------------------------------------------

void commands_dispatch(int argc, char **argv) {
    for (size_t i = 0; i < NUM_COMMANDS; i++) {
        if (strcmp(argv[0], COMMANDS[i].name) != 0) continue;

        const command_t *cmd = &COMMANDS[i];

        if (cmd->requires_admin && !admin_mode) {
            printf("error: '%s' requires admin mode — use login <otp>\r\n",
                   cmd->name);
            buzzer_beep_short();
            return;
        }

        int user_args = argc - 1;
        if (user_args < cmd->min_args || user_args > cmd->max_args) {
            printf("error: usage: %s\r\n", cmd->usage);
            buzzer_beep_short();
            return;
        }

        HANDLERS[i](argc, argv);
        return;
    }

    printf("error: unknown command '%s' — type 'help' for available commands\r\n",
           argv[0]);
    buzzer_beep_short();
}
