#ifndef STUB_LWIP_ARCH_H
#define STUB_LWIP_ARCH_H

/* Host stub for <lwip/arch.h>: the lwIP width typedefs and error codes. */

#include <stdint.h>

typedef uint8_t  u8_t;
typedef uint16_t u16_t;
typedef uint32_t u32_t;

typedef int8_t err_t;

#define ERR_OK         0
#define ERR_INPROGRESS (-5)

#endif
