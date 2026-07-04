#include <stdio.h>

#include "pico/stdlib.h"
#include "pico/multicore.h"

#include "hardware/buzzer.h"
#include "hardware/keypad.h"
#include "hardware/latch.h"

#include "network/wifi.h"
#include "network/ntp.h"

#include "serial/commands.h"
#include "serial/console.h"

#include "storage/storage.h"

static void main1(void) {
    // allow pausing core 1 while writing to flash
    multicore_lockout_victim_init();
    multicore_fifo_push_blocking(1);  // signal core 0: ready

    keypad_init();
 
    while (true) {
        char key = keypad_get_key();
        if (key) {
            // TODO: accumulate digits, verify TOTP, drive latch
            printf("[keypad] key pressed: %c\r\n", key);
            buzzer_beep_short();
        }
        sleep_ms(5);
    }
}

static void boot_network(void) {
    wifi_config_t cfg;
    if (!storage_wifi_get(&cfg)) {
        printf("[main] no wifi config — skipping NTP\r\n");
        printf("[main] use set-wifi to configure\r\n");
        buzzer_beep_long();
        return;
    }

    if (!wifi_connect(cfg.ssid, cfg.password)) {
        printf("[main] wifi connect failed — skipping NTP\r\n");
        buzzer_beep_long();
        return;
    }

    ntp_init();

    // Block until first NTP sync succeeds — beep + retry on failure
    printf("[main] waiting for NTP sync...\r\n");
    while (!ntp_sync()) {
        printf("[main] NTP sync failed, retrying in %ds...\r\n",
               NTP_RETRY_INTERVAL_S);
        buzzer_beep_short();
        sleep_ms(NTP_RETRY_INTERVAL_S * 1000);
    }

    printf("[main] NTP sync ok\r\n");
}

int main(void) {
    stdio_init_all();
 
    buzzer_init();
    latch_init();

    buzzer_beep_short();
    
    // Core 1 must be running and ready before any flash writes
    multicore_launch_core1(main1);
    multicore_fifo_pop_blocking();  // wait for core 1 ready signal

    storage_init();

    boot_network();
 
    // Startup beep — signals boot completed
    buzzer_beep_short();
    buzzer_beep_short();
 
    console_init();
 
    while (true) {
        console_task();
        ntp_task();
        sleep_ms(10);
    }
}
