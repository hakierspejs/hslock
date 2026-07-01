#include "buzzer.h"
#include "pico/stdlib.h"

void buzzer_init() {
    gpio_init(BUZZER_PIN);
    gpio_set_dir(BUZZER_PIN, GPIO_OUT);
}

void buzzer_beep_short() {
    gpio_put(BUZZER_PIN, true);
    sleep_ms(BUZZER_SHORT_BEEP_DELAY);
    gpio_put(BUZZER_PIN, false);
    sleep_ms(BUZZER_SHORT_BEEP_DELAY);
}

void buzzer_beep_medium() {
    gpio_put(BUZZER_PIN, true);
    sleep_ms(BUZZER_MEDIUM_BEEP_DELAY);
    gpio_put(BUZZER_PIN, false);
    sleep_ms(BUZZER_MEDIUM_BEEP_DELAY);
}

void buzzer_beep_long() {
    gpio_put(BUZZER_PIN, true);
    sleep_ms(BUZZER_LONG_BEEP_DELAY);
    gpio_put(BUZZER_PIN, false);
    sleep_ms(BUZZER_LONG_BEEP_DELAY);
}