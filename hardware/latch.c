#include "latch.h"
#include "pico/stdlib.h"

void latch_init() {
    gpio_init(LATCH_PIN);
    gpio_set_dir(LATCH_PIN, GPIO_OUT);
}

void latch_open() {
    gpio_put(LATCH_PIN, true);
    sleep_ms(LATCH_OPEN_DELAY);
    gpio_put(LATCH_PIN, false);
    sleep_ms(LATCH_OPEN_DELAY);
}