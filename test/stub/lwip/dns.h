#ifndef HSLOCK_TEST_STUB_LWIP_DNS_H
#define HSLOCK_TEST_STUB_LWIP_DNS_H
/*
 * Host stub for "lwip/dns.h" - dns_gethostbyname() plus its resolve-callback
 * signature, as referenced by network/ntp.c. Compile-only declaration; never
 * linked into a running harness.
 */
#include "lwip/err.h"
#include "lwip/ip_addr.h"

typedef void (*dns_found_callback)(const char *name, const ip_addr_t *ipaddr, void *callback_arg);

err_t dns_gethostbyname(const char *hostname, ip_addr_t *addr, dns_found_callback found,
                        void *callback_arg);

#endif
