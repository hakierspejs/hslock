#ifndef STUB_LWIP_UDP_H
#define STUB_LWIP_UDP_H

/* Host stub for <lwip/udp.h>: the UDP PCB API ntp.c uses. */

#include "lwip/arch.h"
#include "lwip/ip_addr.h"
#include "lwip/pbuf.h"

struct udp_pcb;

typedef void (*udp_recv_fn)(void *arg, struct udp_pcb *pcb, struct pbuf *p, const ip_addr_t *addr,
                            u16_t port);

struct udp_pcb *udp_new_ip_type(u8_t type);
void            udp_remove(struct udp_pcb *pcb);
void            udp_recv(struct udp_pcb *pcb, udp_recv_fn recv, void *recv_arg);
err_t udp_sendto(struct udp_pcb *pcb, struct pbuf *p, const ip_addr_t *dst_ip, u16_t dst_port);

#endif
