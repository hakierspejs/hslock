#include "commands_handlers.h"
#include "hardware/buzzer.h"
#include "hardware/clock.h"
#include "hardware/latch.h"
#include "hardware/light.h"
#include "hardware/watchdog.h"
#include "network/ntp.h"
#include "storage/backup.h"
#include "storage/storage.h"
#include "shared/totp.h"
#include "version.h"
#include "pico/stdlib.h"
#include "pico/time.h"
#include <stdio.h>
#include <time.h>
#include <stdlib.h>

// if RTC for some reason fails to initialise, allow login after 5 minutes
#define BOOT_BYPASS_WINDOW_US (5ULL * 60 * 1000000)

void cmd_status(int argc, char **argv) {
    printf("mode:      %s\r\n", admin_mode ? "admin" : "user");
    printf("build:     %s %s %s\r\n", GIT_HASH, GIT_DIRTY ? "[with changes]" : "", BUILD_DATE);

    wifi_config_t wifi;
    if (storage_wifi_get(&wifi)) {
        printf("wifi:      ssid=%s\r\n", wifi.ssid);
    } else {
        printf("wifi:      not configured\r\n");
    }

    uint32_t last = ntp_last_sync_time();
    if (last > 0) {
        time_t     lt  = (time_t)last;
        struct tm *ltm = gmtime(&lt);
        char       lbuf[20];
        strftime(lbuf, sizeof(lbuf), "%Y-%m-%d %H:%M:%S", ltm);
        printf("last sync: %s UTC\r\n", lbuf);
    } else {
        printf("last sync: never\r\n");
    }

    buzzer_play_command_ack();
}

void cmd_get_time(int argc, char **argv) {
    uint32_t unix_time = clock_get_unix_time();

    if (unix_time == 0) {
        printf("time: not set\r\n");
        buzzer_play_command_ack();
        return;
    }

    time_t     t  = (time_t)unix_time;
    struct tm *tm = gmtime(&t);
    char       buf[20];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", tm);
    printf("time: %s UTC\r\n", buf);

    uint32_t last = ntp_last_sync_time();
    if (last > 0) {
        time_t     lt  = (time_t)last;
        struct tm *ltm = gmtime(&lt);
        char       lbuf[20];
        strftime(lbuf, sizeof(lbuf), "%Y-%m-%d %H:%M:%S", ltm);
        printf("last sync: %s UTC\r\n", lbuf);
    } else {
        printf("last sync: never\r\n");
    }

    buzzer_play_command_ack();
}

void cmd_test(int argc, char **argv) {
    for (int i = 0; i < 3; i++) {
        buzzer_play_command_ack();
    }
    light_on();
    sleep_ms(1000);
    light_off();
    latch_open();
}

void cmd_login(int argc, char **argv) {
    uint16_t id   = (uint16_t)strtoul(argv[1], NULL, 10);
    uint32_t code = (uint32_t)strtoul(argv[2], NULL, 10);

    // Check if any admin keys exist
    key_record_t keys[BACKUP_MAX_KEYS];
    int          count = storage_key_list(keys, BACKUP_MAX_KEYS);

    bool any_admin = false;
    for (int i = 0; i < count; i++) {
        if (keys[i].is_admin && keys[i].is_enabled && keys[i].is_checksum_valid) {
            any_admin = true;
            break;
        }
    }

    // No wifi configured — allow login (device needs to be set up)
    wifi_config_t wifi;
    if (!storage_wifi_get(&wifi)) {
        printf("warning: wifi not configured — open mode\r\n");
        admin_mode = true;
        printf("login: admin mode enabled\r\n");
        buzzer_play_command_ack();
        return;
    }

    // RTC not initialised — NTP never synced
    if (clock_get_unix_time() == 0) {
        if (time_us_64() >= BOOT_BYPASS_WINDOW_US) {
            printf("warning: RTC not set — open mode\r\n");
            admin_mode = true;
            printf("login: admin mode enabled\r\n");
            buzzer_play_command_ack();
        } else {
            uint64_t remaining_us = BOOT_BYPASS_WINDOW_US - time_us_64();
            uint32_t remaining_s  = (uint32_t)(remaining_us / 1000000ULL);
            printf("error: RTC not set, try again in %us\r\n", remaining_s);
            buzzer_play_auth_error();
        }
        return;
    }

    // No admin keys — allow any credentials (bootstrap mode)
    if (!any_admin) {
        printf("warning: no admin keys configured — bootstrap mode\r\n");
        admin_mode = true;
        printf("login: admin mode enabled\r\n");
        buzzer_play_command_ack();
        return;
    }

    // Load requested key
    key_record_t key;
    if (!storage_key_get(id, &key)) {
        printf("error: invalid credentials\r\n");
        buzzer_play_auth_error();
        return;
    }

    if (!key.is_enabled || !key.is_admin || !key.is_checksum_valid) {
        printf("error: invalid credentials\r\n");
        buzzer_play_auth_error();
        return;
    }

    // Verify TOTP
    if (!totp_verify(key.secret, KEY_SECRET_LEN, code)) {
        printf("error: invalid credentials\r\n");
        buzzer_play_auth_error();
        return;
    }

    admin_mode = true;
    printf("login: admin mode enabled\r\n");
    buzzer_play_command_ack();
}

void cmd_logout(int argc, char **argv) {
    admin_mode = false;
    printf("logout: admin mode disabled\r\n");
    buzzer_play_command_ack();
}

void cmd_reboot(int argc, char **argv) {
    printf("rebooting...\r\n");
    buzzer_play_command_ack();
    watchdog_reboot(0, 0, 100);
    while (true)
        tight_loop_contents();
}
