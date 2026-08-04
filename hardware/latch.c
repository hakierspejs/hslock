#include "latch.h"
#include "pico/stdlib.h"

// Id of the pending "close the strike" alarm, 0 when none is scheduled.
static alarm_id_t latch_close_alarm = 0;

// Timer-alarm callback: de-energise the strike. Runs in the alarm IRQ context,
// so it only touches a GPIO register and clears the stored id.
static int64_t latch_close_cb(alarm_id_t id, void *user_data) {
    (void)id;
    (void)user_data;
    gpio_put(LATCH_PIN, false);
    latch_close_alarm = 0;
    return 0; // one-shot: do not reschedule
}

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

// Energise the strike and schedule its de-energise via a timer alarm, then
// return immediately. This used to sleep for LATCH_OPEN_DELAY ms, which stalled
// the caller (core 1's keypad loop, or core 0's console) for the whole grant
// window and ate the ~8s watchdog margin on the door-open path (ISSUES.md H4).
// The door stays open for the same duration either way.
void latch_open() {
    gpio_put(LATCH_PIN, true);

    // A second open within the window supersedes the pending close, so the
    // strike stays energised for a fresh full delay instead of closing early.
    if (latch_close_alarm > 0) {
        cancel_alarm(latch_close_alarm);
        latch_close_alarm = 0;
    }

    latch_close_alarm = add_alarm_in_ms(LATCH_OPEN_DELAY, latch_close_cb, NULL, true);
    if (latch_close_alarm < 0) {
        // Could not schedule the auto-close: fail safe by de-energising now
        // rather than risk leaving the strike open indefinitely.
        gpio_put(LATCH_PIN, false);
        latch_close_alarm = 0;
    }
}
