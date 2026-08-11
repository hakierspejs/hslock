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
#include "shared/door_verify.h"
#include "shared/totp.h"

door_verify_mailbox_t door_verify_mailbox;

// Max boot-time NTP sync attempts before booting into a degraded, time-not-set
// state instead of blocking forever (ISSUES.md H4).
#define BOOT_NTP_MAX_ATTEMPTS 3

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

    // Bound the boot-time NTP sync. A blackholed UDP/123 or a poisoned DNS must
    // not wedge boot forever: that would leave the door dead AND the serial
    // console unreachable, with no path to recovery (ISSUES.md H4). After a few
    // attempts, boot anyway into a degraded "time-not-set" state - the console
    // comes up and ntp_task() keeps retrying the first sync in the service loop.
    // The door stays closed while the clock is unset: totp_verify() fails closed
    // when clock_get_unix_time() reports the RTC was never set (consistent with
    // M1's intent).
    printf("[main] waiting for NTP sync...\r\n");
    for (int attempt = 1; attempt <= BOOT_NTP_MAX_ATTEMPTS; attempt++) {
        if (ntp_sync()) {
            printf("[main] NTP sync ok\r\n");
            return;
        }
        printf("[main] NTP sync failed (%d/%d), retrying in %ds...\r\n", attempt,
               BOOT_NTP_MAX_ATTEMPTS, NTP_RETRY_INTERVAL_S);
        buzzer_beep_short();
        if (attempt < BOOT_NTP_MAX_ATTEMPTS)
            sleep_ms(NTP_RETRY_INTERVAL_S * 1000);
    }

    printf("[main] NTP unavailable - booting in degraded (time-not-set) mode\r\n");
    printf("[main] door disabled until time is set; console available, NTP retrying\r\n");
}

void core0_handle_door_verify(void) {
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
        printf("[door] key %u: not found\r\n", id);
    } else if (!key.is_checksum_valid) {
        printf("[door] key %u: corrupt\r\n", id);
    } else if (!key.is_enabled) {
        printf("[door] key %u: disabled\r\n", id);
    } else if (!totp_verify(key.secret, KEY_SECRET_LEN, code)) {
        printf("[door] key %u: invalid code\r\n", id);
    } else {
        printf("[door] key %u (%s): granted\r\n", id, key.name);
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

    // Enable the watchdog only now that boot is complete. The boot path has
    // legitimately long single-core blocking (recovery beeps, WiFi association,
    // the bounded NTP sync) that no single core can pet within the RP2040's
    // ~8s watchdog ceiling, and boot is already bounded on every path above.
    // From here the service loop feeds the watchdog, gated on core 1's
    // heartbeat, so a wedge on EITHER core triggers a reset (ISSUES.md H4).
    watchdog_enable(8000, true); // 8 second timeout, pause on debug

    while (true) {
        watchdog_feed_core0(); // pets iff core 1's heartbeat advanced
        core0_handle_door_verify();
        console_task();
        wifi_task();
        ntp_task();
        sleep_ms(10);
    }
}
