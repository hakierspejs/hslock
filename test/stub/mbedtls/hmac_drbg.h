#ifndef STUB_MBEDTLS_HMAC_DRBG_H
#define STUB_MBEDTLS_HMAC_DRBG_H

/*
 * Host stub for <mbedtls/hmac_drbg.h>: enough of the HMAC-DRBG API for the
 * compile-only coverage pass over shared/random.c. The harnesses that actually
 * RUN random.c (asan/valgrind/coverage secret_qr) build against the real
 * <mbedtls/hmac_drbg.h> via -idirafter, so this stub is never linked/executed.
 */

#include "mbedtls/md.h"

#include <stddef.h>

typedef struct {
    int dummy;
} mbedtls_hmac_drbg_context;

/* Hand-wrapped (the long seed_buf prototype formats differently across
 * clang-format versions; fence it so CI and local agree). */
/* clang-format off */
void mbedtls_hmac_drbg_init(mbedtls_hmac_drbg_context *ctx);
void mbedtls_hmac_drbg_free(mbedtls_hmac_drbg_context *ctx);
int mbedtls_hmac_drbg_seed_buf(mbedtls_hmac_drbg_context *ctx,
                               const mbedtls_md_info_t *md_info,
                               const unsigned char *data, size_t data_len);
int mbedtls_hmac_drbg_random(void *p_rng, unsigned char *output, size_t out_len);
/* clang-format on */

#endif
