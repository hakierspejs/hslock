#ifndef HSLOCK_TEST_STUB_LWIP_IP_ADDR_H
#define HSLOCK_TEST_STUB_LWIP_IP_ADDR_H
/* Host stub for "lwip/ip_addr.h" - opaque ip_addr_t + the type selector. */
#include "lwip/arch.h"

typedef struct {
    u32_t addr;
} ip_addr_t;

#define IPADDR_TYPE_ANY 46

#endif
