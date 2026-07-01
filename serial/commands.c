#include "commands.h"
#include "version.h"
#include "hardware/buzzer.h"
#include "storage/storage.h"
#include "pico/stdlib.h"
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

// ---------------------------------------------------------------------------
// Admin state
// ---------------------------------------------------------------------------

static bool admin_mode = false;

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
    int         min_args;  // not counting command name
    int         max_args;
    const char *usage;
    const char *description;
    void      (*handler)(int argc, char **argv);
} command_t;

// ---------------------------------------------------------------------------
// Handlers — stubs for now, each prints + short beep
// ---------------------------------------------------------------------------

static void cmd_help(int argc, char **argv);  // forward decl — needs table

static void cmd_status(int argc, char **argv) {
    printf("mode:      %s\r\n", admin_mode ? "admin" : "user");
    printf("build:     %s %s\r\n", GIT_HASH, BUILD_DATE);

    wifi_config_t wifi;
    if (storage_wifi_get(&wifi)) {
        printf("wifi:      ssid=%s password=%s\r\n", wifi.ssid, wifi.password);
    } else {
        printf("wifi:      not configured\r\n");
    }

    buzzer_beep_short();
}

static void cmd_test(int argc, char **argv) {
    printf("test: [stub] blink LED, beep buzzer, open latch\r\n");
    buzzer_beep_short();
}

static void cmd_login(int argc, char **argv) {
    printf("login: [stub] would verify OTP '%s' against admin keys\r\n", argv[1]);
    admin_mode = true;
    printf("login: admin mode enabled\r\n");
    buzzer_beep_short();
}

static void cmd_logout(int argc, char **argv) {
    admin_mode = false;
    printf("logout: admin mode disabled\r\n");
    buzzer_beep_short();
}

static void cmd_reboot(int argc, char **argv) {
    printf("reboot: [stub] rebooting...\r\n");
    buzzer_beep_short();
}

static void cmd_get_time(int argc, char **argv) {
    printf("get-time: [stub] 2025-01-01 00:00:00 UTC, last sync: never\r\n");
    buzzer_beep_short();
}

static void cmd_sync_ntp(int argc, char **argv) {
    printf("sync-ntp: [stub] forcing NTP resync\r\n");
    buzzer_beep_short();
}

static void cmd_set_wifi(int argc, char **argv) {
    wifi_config_t cfg;

    if (strlen(argv[1]) >= WIFI_SSID_MAX) {
        printf("error: ssid too long (max %d chars)\r\n", WIFI_SSID_MAX - 1);
        buzzer_beep_short();
        return;
    }
    if (strlen(argv[2]) >= WIFI_PASSWORD_MAX) {
        printf("error: password too long (max %d chars)\r\n", WIFI_PASSWORD_MAX - 1);
        buzzer_beep_short();
        return;
    }

    strncpy(cfg.ssid,     argv[1], WIFI_SSID_MAX - 1);
    strncpy(cfg.password, argv[2], WIFI_PASSWORD_MAX - 1);
    cfg.ssid[WIFI_SSID_MAX - 1]         = '\0';
    cfg.password[WIFI_PASSWORD_MAX - 1] = '\0';

    bool ok = storage_wifi_set(&cfg);
    printf("storage_wifi_set: %s\r\n", ok ? "ok" : "FAILED");
    if (!ok) {
        buzzer_beep_short();
        return;
    }

    // Read back and verify
    wifi_config_t readback;
    bool rb_ok = storage_wifi_get(&readback);
    printf("storage_wifi_get: %s\r\n", rb_ok ? "ok" : "FAILED");
    if (rb_ok) {
        printf("readback: ssid='%s' password='%s'\r\n",
               readback.ssid, readback.password);
        bool match = strcmp(cfg.ssid,     readback.ssid)     == 0
                  && strcmp(cfg.password, readback.password)  == 0;
        printf("verified: %s\r\n", match ? "ok" : "MISMATCH");
    }

    buzzer_beep_short();
}

static void cmd_list_keys(int argc, char **argv) {
    printf("list-keys: [stub] no keys stored yet\r\n");
    buzzer_beep_short();
}

static void cmd_get_key(int argc, char **argv) {
    printf("get-key: [stub] would show key id=%s (no secret)\r\n", argv[1]);
    buzzer_beep_short();
}

static void cmd_get_key_secret(int argc, char **argv) {
    printf("get-key-secret: [stub] would show secret + QR for key id=%s\r\n",
           argv[1]);
    buzzer_beep_short();
}

static void cmd_add_key(int argc, char **argv) {
    printf("add-key: [stub] would generate secret for id=%s name='%s'\r\n",
           argv[1], argv[2]);
    buzzer_beep_short();
}

static void cmd_rename_key(int argc, char **argv) {
    printf("rename-key: [stub] would rename key id=%s to '%s'\r\n",
           argv[1], argv[2]);
    buzzer_beep_short();
}

static void cmd_enable_key(int argc, char **argv) {
    printf("enable-key: [stub] would enable key id=%s\r\n", argv[1]);
    buzzer_beep_short();
}

static void cmd_disable_key(int argc, char **argv) {
    printf("disable-key: [stub] would disable key id=%s\r\n", argv[1]);
    buzzer_beep_short();
}

static void cmd_delete_key(int argc, char **argv) {
    printf("delete-key: [stub] would permanently delete key id=%s\r\n",
           argv[1]);
    buzzer_beep_short();
}

static void cmd_set_key_admin(int argc, char **argv) {
    printf("set-key-admin: [stub] would flag key id=%s as admin\r\n", argv[1]);
    buzzer_beep_short();
}

static void cmd_unset_key_admin(int argc, char **argv) {
    printf("unset-key-admin: [stub] would remove admin flag from key id=%s\r\n",
           argv[1]);
    buzzer_beep_short();
}

static void cmd_export_keys(int argc, char **argv) {
    printf("export-keys: [stub] would dump all keys as base64\r\n");
    buzzer_beep_short();
}

static void cmd_import_keys(int argc, char **argv) {
    printf("import-keys: [stub] would export first, then import provided data\r\n");
    buzzer_beep_short();
}

// ---------------------------------------------------------------------------
// Command table
// ---------------------------------------------------------------------------

static const command_t COMMANDS[] = {
    // name               admin   min max  usage                            description
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
        if (strcmp(cmd->name, "?") == 0) continue;  // skip alias row
        printf("  %-32s  %s\r\n", cmd->usage, cmd->description);
    }
    printf("\r\n");
    buzzer_beep_short();
}

// Handler lookup — must match COMMANDS table order
static void (*const HANDLERS[])(int, char**) = {
    cmd_help,
    cmd_help,            // ? alias
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
