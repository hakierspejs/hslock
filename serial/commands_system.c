#include "commands_handlers.h"
#include "commands.h"
#include "login_throttle.h"
#include "hardware/buzzer.h"
#include "hardware/clock.h"
#include "hardware/latch.h"
#include "hardware/light.h"
#include "hardware/led.h"
#include "hardware/watchdog.h"
#include "hardware/structs/scb.h"
#include "network/ntp.h"
#include "storage/backup.h"
#include "storage/storage.h"
#include "shared/door_verify.h"
#include "shared/totp.h"
#include "shared/wipe.h"
#include "version.h"
#include "pico/stdlib.h"
#include "pico/time.h"
#include "pico/unique_id.h"
#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>

// if RTC for some reason fails to initialise, allow login after 5 minutes
#define BOOT_BYPASS_WINDOW_US (5ULL * 60 * 1000000)

void cmd_status(int argc, char **argv) {
    // Mode + build
    printf("mode:      %s\r\n", admin_mode ? "admin" : "user");
    printf("build:     %s %s %s\r\n", GIT_HASH, BUILD_DATE, GIT_DIRTY ? " [DIRTY]" : "");

    // Uptime
    uint64_t up_s = time_us_64() / 1000000ULL;
    printf("uptime:    %lluh %llum %llus\r\n", up_s / 3600, (up_s % 3600) / 60, up_s % 60);

    // Board ID (admin only: unique per-device identifier is target-selection data)
    if (commands_is_admin()) {
        pico_unique_board_id_t board_id;
        pico_get_unique_board_id(&board_id);
        printf("board id:  ");
        for (int i = 0; i < PICO_UNIQUE_BOARD_ID_SIZE_BYTES; i++) {
            printf("%02X", board_id.id[i]);
        }
        printf("\r\n");
    }

    // WiFi
    wifi_config_t wifi;
    if (storage_wifi_get(&wifi)) {
        printf("wifi:      %s\r\n", wifi.ssid);
    } else {
        printf("wifi:      not configured\r\n");
    }
    secure_wipe(&wifi, sizeof(wifi));

    // NTP
    if (ntp_is_synced()) {
        time_t     t  = (time_t)ntp_last_sync_time();
        struct tm *tm = gmtime(&t);
        char       buf[20];
        strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", tm);
        printf("ntp:       synced, last sync %s UTC\r\n", buf);
    } else {
        printf("ntp:       not synced\r\n");
    }

    // Keys (admin only: key inventory is target-selection data). Declared at
    // function scope so the scrub below always runs, even on the non-admin path
    // where the array stays zero-initialised.
    static key_record_t keys[BACKUP_MAX_KEYS];
    if (commands_is_admin()) {
        int count   = storage_key_list(keys, BACKUP_MAX_KEYS);
        int enabled = 0, corrupt = 0;
        for (int i = 0; i < count; i++) {
            if (!keys[i].is_checksum_valid)
                corrupt++;
            else if (keys[i].is_enabled)
                enabled++;
        }
        printf("keys:      %d total, %d enabled, %d corrupt\r\n", count, enabled, corrupt);
        // Scrub the resident key database (seeds included) from BSS.
        secure_wipe(keys, sizeof(keys));
    }

    buzzer_play_command_ack();
}

void cmd_get_time(int argc, char **argv) {
    uint32_t unix_time;
    if (!clock_get_unix_time(&unix_time)) {
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
    light_on();
    sleep_ms(1000);
    light_off();
    led_on();
    sleep_ms(1000);
    led_off();
    latch_open();

    buzzer_play_command_ack();
}

// Brute-force cooldown state for the serial login path (M7). RAM-only: reset on
// reboot, which is acceptable for M7 (persisting overlaps H1's ignored work).
static login_throttle_t login_throttle;

void cmd_login(int argc, char **argv) {
    // Brute-force throttle: once too many consecutive failures have accrued,
    // refuse further attempts for the escalating cooldown window WITHOUT doing
    // the key lookup / TOTP work, so an attacker can no longer trade the
    // buzzer's ~1.2 s blocking beep for ~48 guesses/min.
    uint64_t now_us = time_us_64();
    uint64_t remaining_us;
    if (login_throttle_blocked(&login_throttle, now_us, &remaining_us)) {
        uint32_t remaining_s = (uint32_t)(remaining_us / 1000000ULL) + 1;
        printf("error: too many failed attempts, locked for %us\r\n", remaining_s);
        buzzer_play_auth_error();
        return;
    }

    uint16_t id   = (uint16_t)strtoul(argv[1], NULL, 10);
    uint32_t code = (uint32_t)strtoul(argv[2], NULL, 10);

    // Check if any admin keys exist
    static key_record_t keys[BACKUP_MAX_KEYS];
    int                 count = storage_key_list(keys, BACKUP_MAX_KEYS);

    bool any_admin = false;
    for (int i = 0; i < count; i++) {
        if (keys[i].is_admin && keys[i].is_enabled && keys[i].is_checksum_valid) {
            any_admin = true;
            break;
        }
    }
    // Only the any_admin predicate was needed; scrub the resident key database
    // (seeds included) from BSS before any of the branches below returns.
    secure_wipe(keys, sizeof(keys));

    // Storage must be working - never grant access if we can't trust our own data
    if (!storage_is_mounted()) {
        printf("error: storage unavailable\r\n");
        buzzer_play_auth_error();
        return;
    }

    // No wifi configured - allow login (device needs to be set up)
    wifi_config_t wifi;
    if (storage_is_mounted() && !storage_wifi_get(&wifi)) {
        printf("warning: wifi not configured - open mode\r\n");
        admin_mode = true;
        printf("login: admin mode enabled\r\n");
        buzzer_play_command_ack();
        return;
    }
    // On the fall-through path wifi holds the configured SSID/password; it is
    // only needed for the presence check above, so scrub the credentials.
    secure_wipe(&wifi, sizeof(wifi));

    // RTC not initialised - NTP never synced
    uint32_t now_unix;
    if (!clock_get_unix_time(&now_unix)) {
        if (time_us_64() >= BOOT_BYPASS_WINDOW_US) {
            printf("warning: RTC not set - open mode\r\n");
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

    // No admin keys - allow any credentials (bootstrap mode)
    if (!any_admin) {
        printf("warning: no admin keys configured - bootstrap mode\r\n");
        admin_mode = true;
        printf("login: admin mode enabled\r\n");
        buzzer_play_command_ack();
        return;
    }

    // Load requested key
    key_record_t key;
    if (!storage_key_get(id, &key)) {
        login_throttle_record_failure(&login_throttle, now_us);
        printf("error: invalid credentials\r\n");
        secure_wipe(&key, sizeof(key));
        buzzer_play_auth_error();
        return;
    }

    if (!key.is_enabled || !key.is_admin || !key.is_checksum_valid) {
        login_throttle_record_failure(&login_throttle, now_us);
        printf("error: invalid credentials\r\n");
        secure_wipe(&key, sizeof(key));
        buzzer_play_auth_error();
        return;
    }

    // Verify TOTP
    if (!totp_verify(key.secret, KEY_SECRET_LEN, code)) {
        login_throttle_record_failure(&login_throttle, now_us);
        printf("error: invalid credentials\r\n");
        secure_wipe(&key, sizeof(key));
        buzzer_play_auth_error();
        return;
    }

    login_throttle_reset(&login_throttle);
    admin_mode = true;
    printf("login: admin mode enabled\r\n");
    secure_wipe(&key, sizeof(key));
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
    sleep_ms(200); // let printf flush
    // Direct system reset — bypasses watchdog entirely
    hw_set_bits(&scb_hw->aircr, M0PLUS_AIRCR_SYSRESETREQ_BITS);
    while (true)
        tight_loop_contents();
}

void cmd_format_storage(int argc, char **argv) {
    if (storage_is_mounted()) {
        printf("error: storage is working fine - format refused\r\n");
        printf("use 'export-keys' + 'reboot' + 'format-storage' if you really want this\r\n");
        buzzer_play_command_ack();
        return;
    }

    printf("*** WARNING: this will permanently delete ALL keys ***\r\n");
    printf("type CONFIRM to proceed: ");
    fflush(stdout);

    char            confirm[10] = {0};
    int             len         = 0;
    absolute_time_t deadline    = make_timeout_time_ms(15000);

    while (!time_reached(deadline) && len < 7) {
        // Core 0 is the only servicer of keypad door-verify requests; pump it
        // each iteration so the confirm wait never starves the keypad (L9).
        core0_handle_door_verify();

        int c = getchar_timeout_us(0);
        if (c == PICO_ERROR_TIMEOUT) {
            sleep_ms(10);
            continue;
        }
        if (c == '\r' || c == '\n') {
            printf("\r\n");
            break;
        }
        putchar(c);
        fflush(stdout);
        confirm[len++] = (char)c;
    }

    if (strncmp(confirm, "CONFIRM", 7) != 0) {
        printf("error: aborted\r\n");
        buzzer_play_command_ack();
        return;
    }

    printf("formatting...\r\n");
    if (storage_format()) {
        printf("done - rebooting\r\n");
        buzzer_play_boot();
        watchdog_reboot(0, 0, 500);
        while (true)
            tight_loop_contents();
    } else {
        printf("error: format failed - hardware may be dead\r\n");
        buzzer_play_auth_error();
    }
}