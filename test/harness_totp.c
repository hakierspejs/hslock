/*
 * Host harness: shared/totp.c (RFC 6238 TOTP, HMAC-SHA1, 6 digits, 30s step).
 *
 * Built against the REAL mbedtls (system libmbedcrypto) so the HMAC-SHA1 path
 * computes genuine codes — the compile-only stub in stub/mbedtls/md.h cannot.
 * clock_get_unix_time() is defined here (instead of linking hardware/clock.c,
 * which needs the RP2040 RTC) so the verify() time can be injected and the
 * vectors stay deterministic.
 *
 * Coverage of the two functions in totp.c:
 *   totp_at    — the standard RFC 6238 SHA1 test-vector table (seed
 *                "12345678901234567890"), asserting the exact 6-digit codes.
 *   totp_verify — RTC-not-set (unix_time==0) -> false; correct code accepted;
 *                each of the T-1 / T / T+1 window steps accepted; a wrong code
 *                and an out-of-window code rejected.
 */

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "totp.h"

/* Injectable clock: totp_verify() reads the current time through this. */
static uint32_t g_unix_time = 0;

uint32_t clock_get_unix_time(void) {
    return g_unix_time;
}

/* RFC 6238 Appendix B, SHA1 rows, truncated to TOTP_DIGITS (6) digits. */
struct vector {
    uint64_t unix_time;
    uint32_t code;
};

int main(void) {
    /* Standard RFC 4226/6238 seed: ASCII "12345678901234567890" (20 bytes). */
    const uint8_t secret[]   = "12345678901234567890";
    const size_t  secret_len = sizeof secret - 1; /* drop the NUL */

    static const struct vector vectors[] = {
        {59, 287082},             /* full RFC code 94287082 */
        {1111111109, 81804},      /* 07081804 */
        {1111111111, 50471},      /* 14050471 */
        {1234567890, 5924},       /* 89005924 */
        {2000000000, 279037},     /* 69279037 */
        {20000000000ULL, 353130}, /* 65353130 */
    };

    for (size_t i = 0; i < sizeof vectors / sizeof vectors[0]; i++) {
        uint64_t step = vectors[i].unix_time / TOTP_STEP_SECONDS;
        uint32_t got  = totp_at(secret, secret_len, step);
        assert(got == vectors[i].code);
    }

    /* --- totp_verify ------------------------------------------------------ */

    /* RTC not set: unix_time == 0 must be rejected regardless of code. */
    g_unix_time = 0;
    assert(totp_verify(secret, secret_len, 0) == false);
    assert(totp_verify(secret, secret_len, totp_at(secret, secret_len, 0)) == false);

    /* Pick a time whose step and neighbours all yield distinct codes so the
     * window logic is exercised unambiguously. */
    g_unix_time   = 1111111111; /* step 37037037 */
    uint64_t step = g_unix_time / TOTP_STEP_SECONDS;

    uint32_t code_here = totp_at(secret, secret_len, step);
    uint32_t code_prev = totp_at(secret, secret_len, step - 1);
    uint32_t code_next = totp_at(secret, secret_len, step + 1);
    uint32_t code_far  = totp_at(secret, secret_len, step + TOTP_WINDOW + 1);

    /* Correct current code accepted. */
    assert(totp_verify(secret, secret_len, code_here) == true);
    /* Both window neighbours (T-1, T+1) accepted. */
    assert(totp_verify(secret, secret_len, code_prev) == true);
    assert(totp_verify(secret, secret_len, code_next) == true);
    /* A code just outside the window is rejected. */
    if (code_far != code_here && code_far != code_prev && code_far != code_next) {
        assert(totp_verify(secret, secret_len, code_far) == false);
    }
    /* An obviously wrong code is rejected. */
    assert(totp_verify(secret, secret_len, 999999u + 1u) == false); /* impossible 7-digit */

    printf("totp OK\n");
    return 0;
}
