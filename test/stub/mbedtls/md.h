#ifndef STUB_MBEDTLS_MD_H
#define STUB_MBEDTLS_MD_H

/* Host stub for <mbedtls/md.h>: only the HMAC path totp.c uses. */

#include <stddef.h>
#include <stdint.h>

typedef enum {
    MBEDTLS_MD_SHA1 = 4,
} mbedtls_md_type_t;

typedef struct mbedtls_md_info_t mbedtls_md_info_t;

const mbedtls_md_info_t *mbedtls_md_info_from_type(mbedtls_md_type_t md_type);
int mbedtls_md_hmac(const mbedtls_md_info_t *md_info, const uint8_t *key, size_t keylen,
                    const uint8_t *input, size_t ilen, uint8_t *output);

#endif
