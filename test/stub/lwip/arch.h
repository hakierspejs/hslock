#ifndef HSLOCK_TEST_STUB_LWIP_ARCH_H
#define HSLOCK_TEST_STUB_LWIP_ARCH_H
/*
 * Host stub for lwIP's "lwip/arch.h" - the fixed-width integer aliases lwIP
 * spreads through its public headers. Only enough for network/ntp.c to COMPILE
 * natively for the coverage build; never linked into a running harness.
 */
#include <stdint.h>

typedef uint8_t  u8_t;
typedef int8_t   s8_t;
typedef uint16_t u16_t;
typedef int16_t  s16_t;
typedef uint32_t u32_t;
typedef int32_t  s32_t;

#endif
