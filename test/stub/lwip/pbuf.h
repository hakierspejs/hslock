#ifndef STUB_LWIP_PBUF_H
#define STUB_LWIP_PBUF_H

/* Host stub for <lwip/pbuf.h>: the packet buffer ntp.c allocates and reads. */

#include <stddef.h>

#include "lwip/arch.h"

typedef enum {
    PBUF_TRANSPORT,
    PBUF_RAM,
} pbuf_kind_t;

struct pbuf {
    struct pbuf *next;
    void        *payload;
    u16_t        len;
    u16_t        tot_len;
};

struct pbuf *pbuf_alloc(pbuf_kind_t layer, u16_t length, pbuf_kind_t type);
u8_t         pbuf_free(struct pbuf *p);
u16_t        pbuf_copy_partial(const struct pbuf *p, void *dataptr, u16_t len, u16_t offset);

#endif
