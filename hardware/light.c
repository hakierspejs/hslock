#include "light.h"
#include "pico/stdlib.h"

#include <stdio.h>

void light_init() {
    gpio_init(LIGHT_PIN);
    gpio_set_dir(LIGHT_PIN, GPIO_OUT);
    light_off();
}

void light_on() {
    gpio_put(LIGHT_PIN, true);
    printf("lon\n");
}

void light_off() {
    gpio_put(LIGHT_PIN, false);
    printf("loff\n");
}
