#ifndef _MBEDTLS_CONFIG_H
#define _MBEDTLS_CONFIG_H

#if LIB_PICO_SHA256
// Enable hardware acceleration
#define MBEDTLS_SHA256_ALT
#else
#define MBEDTLS_SHA256_C
#endif

#define MBEDTLS_SHA1_C
#define MBEDTLS_MD_C

// HMAC-DRBG (SHA-256) used to condition TOTP secret generation (ISSUES.md M8).
// Depends only on MBEDTLS_MD_C + a hash (SHA-256, above); seeded from a caller-
// supplied buffer, so no MBEDTLS_ENTROPY_C / threading is required.
#define MBEDTLS_HMAC_DRBG_C

#endif