#include "login_throttle.h"

#include <stddef.h>

bool login_throttle_blocked(const login_throttle_t *t, uint64_t now_us, uint64_t *remaining_us) {
    if (t->cooldown_until_us != 0 && now_us < t->cooldown_until_us) {
        if (remaining_us != NULL) {
            *remaining_us = t->cooldown_until_us - now_us;
        }
        return true;
    }
    return false;
}

void login_throttle_record_failure(login_throttle_t *t, uint64_t now_us) {
    if (t->fail_count < UINT32_MAX) {
        t->fail_count++;
    }

    // The first LOGIN_FAIL_THRESHOLD failures are free (no cooldown armed).
    if (t->fail_count < LOGIN_FAIL_THRESHOLD) {
        return;
    }

    // Double the window per failure past the threshold, saturating at the cap.
    // Looping (rather than a shift) avoids undefined behaviour on a large shift
    // count and needs no overflow reasoning.
    unsigned steps  = t->fail_count - LOGIN_FAIL_THRESHOLD;
    uint64_t window = LOGIN_COOLDOWN_BASE_US;
    for (unsigned i = 0; i < steps && window < LOGIN_COOLDOWN_MAX_US; i++) {
        window <<= 1;
    }
    if (window > LOGIN_COOLDOWN_MAX_US) {
        window = LOGIN_COOLDOWN_MAX_US;
    }

    t->cooldown_until_us = now_us + window;
}

void login_throttle_reset(login_throttle_t *t) {
    t->fail_count        = 0;
    t->cooldown_until_us = 0;
}
