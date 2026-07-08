#ifndef HSLOCK_TEST_STUB_LWIP_ERR_H
#define HSLOCK_TEST_STUB_LWIP_ERR_H
/* Host stub for "lwip/err.h" - err_t plus the codes network/ntp.c checks. */
#include "lwip/arch.h"

typedef s8_t err_t;

#define ERR_OK         0  // no error
#define ERR_INPROGRESS -5 // operation in progress

#endif
