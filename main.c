#include <stdio.h>

#include "pico/stdlib.h"
#include "pico/multicore.h"

#include "core1.h"

#include "hardware/buzzer.h"
#include "hardware/keypad.h"
#include "hardware/latch.h"
#include "hardware/light.h"
#include "hardware/led.h"
#include "hardware/watchdog.h"

#include "network/wifi.h"
#include "network/ntp.h"

#include "serial/commands.h"
#include "serial/console.h"

#include "storage/storage.h"

#include "hardware/sync.h"
#include "shared/debug.h"
#include "shared/door_verify.h"
#include "shared/totp.h"

door_verify_mailbox_t door_verify_mailbox;

static void boot_network(void) {
    wifi_config_t cfg;
    if (!storage_wifi_get(&cfg)) {
        printf("[main] no wifi config - skipping NTP\r\n");
        printf("[main] use set-wifi to configure\r\n");
        buzzer_beep_long();
        return;
    }

    if (!wifi_connect(cfg.ssid, cfg.password)) {
        printf("[main] wifi connect failed - skipping NTP\r\n");
        buzzer_beep_long();
        return;
    }

    // Block until first NTP sync succeeds - beep + retry on failure
    printf("[main] waiting for NTP sync...\r\n");
    while (!ntp_sync()) {
        printf("[main] NTP sync failed, retrying in %ds...\r\n", NTP_RETRY_INTERVAL_S);
        buzzer_beep_short();
        sleep_ms(NTP_RETRY_INTERVAL_S * 1000);
    }

    printf("[main] NTP sync ok\r\n");
}

static void core0_handle_door_verify(void) {
    static uint32_t last_handled_seq = 0;

    uint32_t seq = door_verify_mailbox.request_seq;
    if (seq == last_handled_seq)
        return;

    __dmb(); // request_seq visible => request_id/request_code are too
    uint16_t id   = door_verify_mailbox.request_id;
    uint32_t code = door_verify_mailbox.request_code;

    bool granted = false;

    key_record_t key;
    if (!storage_key_get(id, &key)) {
        DBG("[door] key %u: not found\r\n", id);
    } else if (!key.is_checksum_valid) {
        DBG("[door] key %u: corrupt\r\n", id);
    } else if (!key.is_enabled) {
        DBG("[door] key %u: disabled\r\n", id);
    } else if (!totp_verify(key.secret, KEY_SECRET_LEN, code)) {
        DBG("[door] key %u: invalid code\r\n", id);
    } else {
        DBG("[door] key %u (%s): granted\r\n", id, key.name);
        granted = true;
    }

    door_verify_mailbox.response_granted = granted;
    __dmb(); // response_granted visible before the seq that vouches for it
    door_verify_mailbox.response_seq = seq;

    last_handled_seq = seq;
}

int main(void) {
    stdio_init_all();

    buzzer_init();
    latch_init();
    light_init();
    led_init();

    buzzer_beep_short();

    watchdog_enable(8000, true); // 8 second timeout, pause on debug

    // Core 1 must be running and ready before any flash writes
    multicore_launch_core1(main1);
    multicore_fifo_pop_blocking(); // wait for core 1 ready signal

    bool storage_ok = storage_init();
    if (!storage_ok) {
        printf("[main] *** STORAGE FAILURE - recovery mode ***\r\n");
        printf("[main] use 'format-storage' to erase and reinitialise\r\n");
        printf("[main] WARNING: this will permanently delete ALL keys\r\n");
        for (int i = 0; i < 5; i++) {
            buzzer_beep_long();
            sleep_ms(100);
            buzzer_beep_long();
            sleep_ms(500);
        }
    }

    // Must run every boot, independent of whether WiFi/storage are available -
    // it configures the RTC's clock divider, and without it the RTC free-runs
    // at its uninitialised (much faster than 1Hz) rate.
    ntp_init();

    boot_network();

    // Startup beep - signals boot completed
    buzzer_play_boot();

    console_init();

    while (true) {
        core0_handle_door_verify();
        console_task();
        wifi_task();
        ntp_task();
        sleep_ms(10);
    }
}
