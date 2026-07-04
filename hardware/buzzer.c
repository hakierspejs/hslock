#include "buzzer.h"
#include "pico/stdlib.h"

void buzzer_init() {
    gpio_init(BUZZER_PIN);
    gpio_set_dir(BUZZER_PIN, GPIO_OUT);
    buzzer_off();
}

void buzzer_on() {
    gpio_put(BUZZER_PIN, true);
}

void buzzer_off() {
    gpio_put(BUZZER_PIN, false);
}

void buzzer_beep_short() {
    buzzer_on();
    sleep_ms(BUZZER_SHORT_BEEP_DELAY);
    buzzer_off();
    sleep_ms(BUZZER_SHORT_BEEP_DELAY);
}

void buzzer_beep_medium() {
    buzzer_on();
    sleep_ms(BUZZER_MEDIUM_BEEP_DELAY);
    buzzer_off();
    sleep_ms(BUZZER_MEDIUM_BEEP_DELAY);
}

void buzzer_beep_long() {
    buzzer_on();
    sleep_ms(BUZZER_LONG_BEEP_DELAY);
    buzzer_off();
    sleep_ms(BUZZER_LONG_BEEP_DELAY);
}

void buzzer_play_boot() {
    buzzer_beep_short();
    buzzer_beep_short();
    buzzer_beep_short();
}

void buzzer_play_command_ack() {
    buzzer_beep_short();
}

void buzzer_play_ntp_sync_error() {
    buzzer_beep_short();
    buzzer_beep_medium();
    buzzer_beep_short();
    buzzer_beep_medium();
}