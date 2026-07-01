#include <stdio.h>

#include "pico/stdlib.h"
#include "pico/multicore.h"

#include "hardware/buzzer.h"
#include "hardware/keypad.h"
#include "hardware/latch.h"

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

int main(void) {
    stdio_init_all();
 
    buzzer_init();
    latch_init();

    // Core 1 must be running and ready before any flash writes
    multicore_launch_core1(main1);
    multicore_fifo_pop_blocking();  // wait for core 1 ready signal

    storage_init();
 
    // Startup beep — signals boot completed
    buzzer_beep_short();
    buzzer_beep_short();
 
    console_init();
 
    while (true) {
        console_task();
        sleep_ms(10);
    }
}
