#include "totp.h"
#include "hardware/clock.h"

#include "mbedtls/md.h"

#include <string.h>
#include <stdio.h>
#include <stddef.h>

// ---------------------------------------------------------------------------
// TOTP — RFC 6238
// ---------------------------------------------------------------------------

uint32_t totp_at(const uint8_t *secret, size_t secret_len, uint64_t step) {
    // Pack step as 8-byte big-endian
    uint8_t msg[8];
    msg[0] = (step >> 56) & 0xFF;
    msg[1] = (step >> 48) & 0xFF;
    msg[2] = (step >> 40) & 0xFF;
    msg[3] = (step >> 32) & 0xFF;
    msg[4] = (step >> 24) & 0xFF;
    msg[5] = (step >> 16) & 0xFF;
    msg[6] = (step >> 8) & 0xFF;
    msg[7] = (step) & 0xFF;

    // HMAC-SHA1
    uint8_t hmac[20];
    mbedtls_md_hmac(mbedtls_md_info_from_type(MBEDTLS_MD_SHA1), secret, secret_len, msg,
                    sizeof(msg), hmac);

    // Dynamic truncation
    int      offset = hmac[19] & 0x0F;
    uint32_t code =
        ((uint32_t)(hmac[offset] & 0x7F) << 24) | ((uint32_t)(hmac[offset + 1] & 0xFF) << 16) |
        ((uint32_t)(hmac[offset + 2] & 0xFF) << 8) | ((uint32_t)(hmac[offset + 3] & 0xFF));

    // 6 digits
    static const uint32_t POW10[7] = {1, 10, 100, 1000, 10000, 100000, 1000000};
    return code % POW10[TOTP_DIGITS];
}

bool totp_verify(const uint8_t *secret, size_t secret_len, uint32_t code) {
    uint32_t unix_time = clock_get_unix_time();
    if (unix_time == 0) {
        printf("[totp] RTC not set\r\n");
        return false;
    }

    uint64_t step = unix_time / TOTP_STEP_SECONDS;

    for (int delta = -TOTP_WINDOW; delta <= TOTP_WINDOW; delta++) {
        if (totp_at(secret, secret_len, step + delta) == code) {
            return true;
        }
    }

    return false;
}
