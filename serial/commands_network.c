#include "commands_handlers.h"
#include "hardware/buzzer.h"
#include "storage/storage.h"
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

void cmd_sync_ntp(int argc, char **argv) {
    // TODO
    printf("sync-ntp: [stub] forcing NTP resync\r\n");
    buzzer_beep_short();
}

void cmd_set_wifi(int argc, char **argv) {
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

    buzzer_beep_short();
}
