#ifndef STUB_PICO_CYW43_ARCH_H
#define STUB_PICO_CYW43_ARCH_H

/* Host stub for <pico/cyw43_arch.h>: the WiFi driver entry points ntp.c and
 * wifi.c call. */

#include <stdint.h>

#define CYW43_ITF_STA           0
#define CYW43_LINK_UP           1
#define CYW43_AUTH_WPA2_AES_PSK 0x00400004u

typedef struct {
    int dummy;
} cyw43_t;

extern cyw43_t cyw43_state;

int  cyw43_arch_init(void);
void cyw43_arch_enable_sta_mode(void);
int  cyw43_arch_wifi_connect_timeout_ms(const char *ssid, const char *password, uint32_t auth,
                                        uint32_t timeout_ms);
int  cyw43_tcpip_link_status(cyw43_t *self, int itf);
void cyw43_arch_lwip_begin(void);
void cyw43_arch_lwip_end(void);
void cyw43_arch_poll(void);

#endif
