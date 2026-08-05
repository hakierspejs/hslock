#include "random.h"

#include "pico/unique_id.h"

#include "mbedtls/hmac_drbg.h"
#include "mbedtls/md.h"

#include <string.h>

// ---------------------------------------------------------------------------
// Secret generation
//
// get_rand_64() is pico_rand: on the RP2040 (no hardware TRNG) it is a 128-bit
// xoroshiro128** software PRNG. Its output function is invertible and its state
// (128 bits) is smaller than a 160-bit TOTP secret, so a holder of one issued
// secret could recover the generator state and predict/rewind other secrets if
// the raw stream were exposed directly (ISSUES.md M8).
//
// Remediation: condition a pool of >= 64 PRNG draws plus the board's unique id
// through an HMAC-DRBG (SHA-256). The DRBG output is a one-way function of the
// pool, so an emitted secret no longer reveals the raw PRNG stream, and the
// board id personalises the stream per device. The DRBG is seeded directly from
// the pool buffer (mbedtls_hmac_drbg_seed_buf) so no MBEDTLS_ENTROPY_C /
// threading is pulled in.
// ---------------------------------------------------------------------------

// >= 64 draws, per the audit's "collect >= 64 draws" remediation.
#define SECRET_DRBG_DRAWS 64

static void secure_wipe(void *p, size_t n) {
    volatile uint8_t *v = (volatile uint8_t *)p;
    while (n--) {
        *v++ = 0;
    }
}

void generate_secret(uint8_t *out, size_t len) {
    if (out == NULL || len == 0) {
        return;
    }

    // Conditioned entropy pool: SECRET_DRBG_DRAWS PRNG draws + board id.
    uint8_t seed[SECRET_DRBG_DRAWS * sizeof(uint64_t) + PICO_UNIQUE_BOARD_ID_SIZE_BYTES];
    size_t  off = 0;

    for (int i = 0; i < SECRET_DRBG_DRAWS; i++) {
        uint64_t r = get_rand_64();
        memcpy(seed + off, &r, sizeof(r));
        off += sizeof(r);
    }

    pico_unique_board_id_t board_id;
    pico_get_unique_board_id(&board_id);
    memcpy(seed + off, board_id.id, PICO_UNIQUE_BOARD_ID_SIZE_BYTES);
    off += PICO_UNIQUE_BOARD_ID_SIZE_BYTES;

    mbedtls_hmac_drbg_context ctx;
    mbedtls_hmac_drbg_init(&ctx);

    const mbedtls_md_info_t *md = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);

    // Fail closed: never emit the raw pool bytes if the DRBG is unavailable.
    if (md == NULL || mbedtls_hmac_drbg_seed_buf(&ctx, md, seed, off) != 0 ||
        mbedtls_hmac_drbg_random(&ctx, out, len) != 0) {
        secure_wipe(out, len);
    }

    mbedtls_hmac_drbg_free(&ctx);
    secure_wipe(seed, sizeof(seed));
}
