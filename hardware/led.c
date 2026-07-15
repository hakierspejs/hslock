#include "led.h"
#include "pico/stdlib.h"

#include <stdio.h>

void led_init() {
    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);
    led_off();
}

void led_on() {
    gpio_put(LED_PIN, true);
    printf("lon\n");
}

void led_off() {
    gpio_put(LED_PIN, false);
    printf("loff\n");
}
