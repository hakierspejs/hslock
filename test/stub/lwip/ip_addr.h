#ifndef STUB_LWIP_IP_ADDR_H
#define STUB_LWIP_IP_ADDR_H

/* Host stub for <lwip/ip_addr.h>: an opaque IP address holder. */

#include "lwip/arch.h"

typedef struct {
    u32_t addr;
} ip_addr_t;

#define IPADDR_TYPE_ANY 0

#endif
