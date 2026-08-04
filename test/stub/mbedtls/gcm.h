#ifndef STUB_MBEDTLS_GCM_H
#define STUB_MBEDTLS_GCM_H

/*
 * Host stub for <mbedtls/gcm.h>.
 *
 * This exists ONLY so storage.c compiles in contexts that use the shared
 * -Istub include path (the coverage target's compile-only pass over the whole
 * first-party tree). The harnesses that actually RUN storage.c's crypto
 * (asan_storage / vg_storage / cov_storage) are built with `-idirafter stub`
 * and link the real libmbedcrypto, so the system <mbedtls/gcm.h> wins there and
 * this file is never used for execution. The signatures mirror mbedTLS 3.x so
 * storage.c type-checks either way.
 */

#include <stddef.h>
#include <stdint.h>

typedef enum {
    MBEDTLS_CIPHER_ID_AES = 2,
} mbedtls_cipher_id_t;

#define MBEDTLS_GCM_ENCRYPT 1
#define MBEDTLS_GCM_DECRYPT 0

typedef struct {
    unsigned char opaque[512];
} mbedtls_gcm_context;

void mbedtls_gcm_init(mbedtls_gcm_context *ctx);
void mbedtls_gcm_free(mbedtls_gcm_context *ctx);
int  mbedtls_gcm_setkey(mbedtls_gcm_context *ctx, mbedtls_cipher_id_t cipher,
                        const unsigned char *key, unsigned int keybits);
int  mbedtls_gcm_crypt_and_tag(mbedtls_gcm_context *ctx, int mode, size_t length,
                               const unsigned char *iv, size_t iv_len, const unsigned char *add,
                               size_t add_len, const unsigned char *input, unsigned char *output,
                               size_t tag_len, unsigned char *tag);
int  mbedtls_gcm_auth_decrypt(mbedtls_gcm_context *ctx, size_t length, const unsigned char *iv,
                              size_t iv_len, const unsigned char *add, size_t add_len,
                              const unsigned char *tag, size_t tag_len, const unsigned char *input,
                              unsigned char *output);

#endif
