#include "commands_handlers.h"
#include "hardware/buzzer.h"
#include "network/ntp.h"
#include "network/wifi.h"
#include "storage/storage.h"
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

// Zero a wifi_config_t through a volatile pointer so the compiler cannot
// optimise the wipe away once the struct (which holds the WiFi password) is
// no longer live. The password would otherwise linger on the core-0 stack.
static void wifi_config_wipe(wifi_config_t *cfg) {
    volatile char *p = (volatile char *)cfg;
    for (size_t i = 0; i < sizeof(*cfg); i++) {
        p[i] = 0;
    }
}

void cmd_sync_ntp(int argc, char **argv) {
    printf("syncing...\r\n");
    if (ntp_sync()) {
        printf("sync ok\r\n");
    } else {
        printf("error: sync failed\r\n");
    }
    buzzer_beep_short();
}

void cmd_set_wifi(int argc, char **argv) {
    wifi_config_t cfg = {0};

    if (strlen(argv[1]) >= WIFI_SSID_MAX) {
        printf("error: ssid too long (max %d chars)\r\n", WIFI_SSID_MAX - 1);
        buzzer_play_command_ack();
        return;
    }
    if (strlen(argv[2]) >= WIFI_PASSWORD_MAX) {
        printf("error: password too long (max %d chars)\r\n", WIFI_PASSWORD_MAX - 1);
        buzzer_play_command_ack();
        return;
    }

    strncpy(cfg.ssid, argv[1], WIFI_SSID_MAX - 1);
    strncpy(cfg.password, argv[2], WIFI_PASSWORD_MAX - 1);
    cfg.ssid[WIFI_SSID_MAX - 1]         = '\0';
    cfg.password[WIFI_PASSWORD_MAX - 1] = '\0';

    bool ok = storage_wifi_set(&cfg);
    printf("storage_wifi_set: %s\r\n", ok ? "ok" : "FAILED");
    if (!ok) {
        wifi_config_wipe(&cfg);
        buzzer_play_command_ack();
        return;
    }

    printf("reconnecting wifi...\r\n");
    if (wifi_connect(cfg.ssid, cfg.password)) {
        printf("syncing ntp...\r\n");
        if (ntp_sync()) {
            printf("ntp sync ok\r\n");
        } else {
            printf("ntp sync failed\r\n");
        }
    } else {
        printf("wifi connect failed\r\n");
    }

    wifi_config_wipe(&cfg);
    buzzer_play_command_ack();
}
