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

// H7: at-rest encryption of secrets in storage (AES-256-GCM under a device-bound
// KEK derived via HMAC-SHA256). GCM needs AES + the cipher layer.
#define MBEDTLS_AES_C
#define MBEDTLS_CIPHER_C
#define MBEDTLS_GCM_C

#endif