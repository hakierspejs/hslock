#ifndef LOGIN_THROTTLE_H
#define LOGIN_THROTTLE_H

#include <stdbool.h>
#include <stdint.h>

// Brute-force cooldown for the serial `login` path (M7).
//
// The failure counter and cooldown deadline live in RAM only and therefore do
// NOT survive a reboot — acceptable for M7 (the audit notes "Nothing survives
// reboot"); persisting the counter overlaps H1's ignored persistence work and
// is out of scope here.
//
// Model: LOGIN_FAIL_THRESHOLD consecutive failures are free (so a legitimate
// operator fat-fingering a code a few times is not punished). Past the
// threshold, each further failure arms an escalating cooldown window that
// doubles from LOGIN_COOLDOWN_BASE_US up to a LOGIN_COOLDOWN_MAX_US cap; while
// the window is open, `login` refuses attempts without doing the key lookup or
// TOTP work. A single success clears the counter and the cooldown. Time is the
// monotonic clock (time_us_64() on the firmware); it is passed in so the logic
// is unit-testable with an injectable clock.

#define LOGIN_FAIL_THRESHOLD   5                            // free consecutive failures
#define LOGIN_COOLDOWN_BASE_US (2ULL * 1000000ULL)          // first cooldown: ~2 s
#define LOGIN_COOLDOWN_MAX_US  (15ULL * 60ULL * 1000000ULL) // cap: 15 min

typedef struct {
    uint32_t fail_count;        // consecutive auth failures since the last success
    uint64_t cooldown_until_us; // absolute monotonic deadline; 0 = no cooldown armed
} login_throttle_t;

// Returns true if an attempt at `now_us` must be refused (still inside the
// armed cooldown window). When it returns true and `remaining_us` is non-NULL,
// `*remaining_us` receives the microseconds left until the window closes.
bool login_throttle_blocked(const login_throttle_t *t, uint64_t now_us, uint64_t *remaining_us);

// Records an authentication failure at `now_us`. Once the consecutive-failure
// count reaches LOGIN_FAIL_THRESHOLD, arms/extends the escalating cooldown.
void login_throttle_record_failure(login_throttle_t *t, uint64_t now_us);

// Records a success: clears the counter and disarms any cooldown.
void login_throttle_reset(login_throttle_t *t);

#endif // LOGIN_THROTTLE_H
