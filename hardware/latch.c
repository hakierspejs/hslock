#include "buzzer.h"
#include "latch.h"
#include "pico/stdlib.h"

void latch_init() {
    gpio_init(LATCH_PIN);
    gpio_set_dir(LATCH_PIN, GPIO_OUT);
    // Explicitly drive the pin low so the de-energised state is set on purpose
    // rather than relying on gpio_init()'s incidental low. This assumes a
    // FAIL-SECURE electric strike, where de-energised == locked; the door stays
    // locked across watchdog resets and reboot loops. A FAIL-SAFE strike or
    // maglock (de-energised == unlocked) would UNLOCK on every reset and must
    // NOT be wired to this pin without inverting the drive logic here and in
    // latch_open().
    gpio_put(LATCH_PIN, false);
}

void latch_open() {
    gpio_put(LATCH_PIN, true);
    sleep_ms(LATCH_OPEN_DELAY);
    gpio_put(LATCH_PIN, false);
}