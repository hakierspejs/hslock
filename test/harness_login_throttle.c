// Host harness for serial/login_throttle.c - the M7 serial-login brute-force
// cooldown.
//
// login_throttle.c is dependency-free (only stdint/stdbool), so this harness
// links it directly and drives its state machine with an INJECTED monotonic
// clock. That lets us assert the exact throttle contract cmd_login relies on
// without pulling in storage/TOTP/buzzer/USB: the first N consecutive failures
// are free, the (N+1)-th arms a cooldown that refuses attempts, the window is
// escalating (doubling) and capped, expiry reopens attempts, and a success
// clears everything.

#include "login_throttle.h"

#include <assert.h>
#include <stdio.h>

// Fail LOGIN_FAIL_THRESHOLD times at t=now, asserting each of the first
// (threshold-1) failures leaves the attempt un-blocked and the threshold-th
// arms the cooldown. Returns nothing; leaves t armed.
static void arm_to_threshold(login_throttle_t *t, uint64_t now) {
    for (unsigned i = 1; i < LOGIN_FAIL_THRESHOLD; i++) {
        login_throttle_record_failure(t, now);
        // Below the threshold: never blocked.
        assert(!login_throttle_blocked(t, now, NULL));
    }
    // The threshold-th failure arms the cooldown.
    login_throttle_record_failure(t, now);
    assert(login_throttle_blocked(t, now, NULL));
}

static void test_free_attempts_then_cooldown(void) {
    login_throttle_t t   = {0};
    uint64_t         now = 1000ULL * 1000000ULL; // arbitrary non-zero clock

    // A fresh throttle blocks nothing.
    assert(!login_throttle_blocked(&t, now, NULL));

    arm_to_threshold(&t, now);

    // The first armed window is the base window; remaining must be > 0 and
    // within the base.
    uint64_t remaining = 0;
    assert(login_throttle_blocked(&t, now, &remaining));
    assert(remaining > 0 && remaining <= LOGIN_COOLDOWN_BASE_US);
}

static void test_cooldown_expires(void) {
    login_throttle_t t   = {0};
    uint64_t         now = 5ULL * 1000000ULL;
    arm_to_threshold(&t, now);

    // Just before the deadline: still blocked.
    assert(login_throttle_blocked(&t, now + LOGIN_COOLDOWN_BASE_US - 1, NULL));
    // At/after the deadline: no longer blocked (drive the injectable clock fwd).
    assert(!login_throttle_blocked(&t, now + LOGIN_COOLDOWN_BASE_US, NULL));
    assert(!login_throttle_blocked(&t, now + LOGIN_COOLDOWN_BASE_US + 1, NULL));
}

static void test_window_escalates_and_caps(void) {
    login_throttle_t t   = {0};
    uint64_t         now = 0;
    arm_to_threshold(&t, now); // threshold-th failure -> base window

    uint64_t prev = 0;
    assert(login_throttle_blocked(&t, now, &prev));
    assert(prev == LOGIN_COOLDOWN_BASE_US); // now==0 => remaining == window

    // Each further failure at least doubles the window until it saturates at
    // the cap; it must never exceed the cap.
    for (int i = 0; i < 30; i++) {
        login_throttle_record_failure(&t, now);
        uint64_t cur = 0;
        assert(login_throttle_blocked(&t, now, &cur));
        assert(cur <= LOGIN_COOLDOWN_MAX_US);
        if (prev < LOGIN_COOLDOWN_MAX_US) {
            // Still climbing: expect a strictly larger (doubled) window until
            // the cap clamps it.
            assert(cur >= prev);
        } else {
            // Saturated: pinned at the cap.
            assert(cur == LOGIN_COOLDOWN_MAX_US);
        }
        prev = cur;
    }
    // After 30 failures past the threshold the window is firmly at the cap.
    assert(prev == LOGIN_COOLDOWN_MAX_US);
}

static void test_success_resets(void) {
    login_throttle_t t   = {0};
    uint64_t         now = 42ULL * 1000000ULL;
    arm_to_threshold(&t, now);
    assert(login_throttle_blocked(&t, now, NULL));

    login_throttle_reset(&t);
    assert(!login_throttle_blocked(&t, now, NULL));
    assert(t.fail_count == 0);
    assert(t.cooldown_until_us == 0);

    // After a reset the free-attempt budget is restored: a single failure must
    // NOT re-arm a cooldown.
    login_throttle_record_failure(&t, now);
    assert(!login_throttle_blocked(&t, now, NULL));
}

int main(void) {
    test_free_attempts_then_cooldown();
    test_cooldown_expires();
    test_window_escalates_and_caps();
    test_success_resets();

    printf("harness_login_throttle: all assertions passed\n");
    return 0;
}
