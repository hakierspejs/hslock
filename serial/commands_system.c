#include "commands_handlers.h"
#include "hardware/buzzer.h"
#include "hardware/clock.h"
#include "hardware/latch.h"
#include "hardware/light.h"
#include "hardware/watchdog.h"
#include "network/ntp.h"
#include "storage/storage.h"
#include "version.h"
#include "pico/stdlib.h"
#include <stdio.h>
#include <time.h>

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
        time_t lt = (time_t)last;
        struct tm *ltm = gmtime(&lt);
        char lbuf[20];
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

    time_t t = (time_t)unix_time;
    struct tm *tm = gmtime(&t);
    char buf[20];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", tm);
    printf("time: %s UTC\r\n", buf);

    uint32_t last = ntp_last_sync_time();
    if (last > 0) {
        time_t lt = (time_t)last;
        struct tm *ltm = gmtime(&lt);
        char lbuf[20];
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
    // TODO
    printf("login: [stub] would verify OTP '%s' against admin keys\r\n", argv[1]);
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
    while (true) tight_loop_contents();
}
