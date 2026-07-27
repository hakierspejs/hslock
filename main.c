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

#include "shared/fifo_protocol.h"
#include "shared/totp.h"

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

    ntp_init();

    // Block until first NTP sync succeeds - beep + retry on failure
    printf("[main] waiting for NTP sync...\r\n");
    while (!ntp_sync()) {
        printf("[main] NTP sync failed, retrying in %ds...\r\n", NTP_RETRY_INTERVAL_S);
        buzzer_beep_short();
        sleep_ms(NTP_RETRY_INTERVAL_S * 1000);
    }

    printf("[main] NTP sync ok\r\n");
}

static void core0_handle_fifo(void) {
    if (!multicore_fifo_rvalid())
        return;

    uint32_t word1 = multicore_fifo_pop_blocking();
    uint8_t  msg   = (word1 >> 24) & 0xFF;

    if (msg == FIFO_MSG_VERIFY) {
        uint16_t id   = (uint16_t)(word1 & 0xFFFF);
        uint32_t code = multicore_fifo_pop_blocking();

        uint32_t result = FIFO_RESULT_DENIED;

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
            result = FIFO_RESULT_GRANTED;
        }

        multicore_fifo_push_blocking(result);
    }
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

    boot_network();

    // Startup beep - signals boot completed
    buzzer_play_boot();

    console_init();

    while (true) {
        core0_handle_fifo();
        console_task();
        wifi_task();
        ntp_task();
        sleep_ms(10);
    }
}
