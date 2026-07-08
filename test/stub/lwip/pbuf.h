#ifndef HSLOCK_TEST_STUB_LWIP_PBUF_H
#define HSLOCK_TEST_STUB_LWIP_PBUF_H
/*
 * Host stub for "lwip/pbuf.h" - the packet buffer type and helpers referenced
 * by network/ntp.c. Compile-only: shapes are minimal, functions are declared
 * (not defined) since the file is never linked into a running harness.
 */
#include <stddef.h>

#include "lwip/arch.h"

// Buffer layer / type selectors (values are not load-bearing on the host).
typedef enum {
    PBUF_TRANSPORT,
    PBUF_IP,
    PBUF_LINK,
    PBUF_RAW,
} pbuf_layer;

typedef enum {
    PBUF_RAM,
    PBUF_POOL,
    PBUF_ROM,
    PBUF_REF,
} pbuf_type;

struct pbuf {
    struct pbuf *next;
    void        *payload;
    u16_t        tot_len;
    u16_t        len;
};

struct pbuf *pbuf_alloc(pbuf_layer layer, u16_t length, pbuf_type type);
u8_t         pbuf_free(struct pbuf *p);
u16_t        pbuf_copy_partial(const struct pbuf *p, void *dataptr, u16_t len, u16_t offset);

#endif
