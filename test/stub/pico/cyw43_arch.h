#ifndef HSLOCK_TEST_STUB_PICO_CYW43_ARCH_H
#define HSLOCK_TEST_STUB_PICO_CYW43_ARCH_H
/*
 * Host stub for the Pico SDK "pico/cyw43_arch.h" - the CYW43 WiFi driver arch
 * layer. Declares just the entry points, enums and the global driver state that
 * network/wifi.c and network/ntp.c reference, so both COMPILE natively for the
 * coverage build (.gcno at true 0%). Never linked into a running harness.
 */
#include <stdint.h>

// Auth modes.
#define CYW43_AUTH_OPEN            0
#define CYW43_AUTH_WPA2_AES_PSK    0x00400004

// Interface / link-status enums.
#define CYW43_ITF_STA 0
#define CYW43_ITF_AP  1

#define CYW43_LINK_DOWN 0
#define CYW43_LINK_JOIN 1
#define CYW43_LINK_UP   3

// Opaque driver state object; only its address is taken on the host.
typedef struct {
    int _dummy;
} cyw43_t;
extern cyw43_t cyw43_state;

int  cyw43_arch_init(void);
void cyw43_arch_deinit(void);
void cyw43_arch_enable_sta_mode(void);
int  cyw43_arch_wifi_connect_timeout_ms(const char *ssid, const char *pw, uint32_t auth,
                                        uint32_t timeout_ms);
int  cyw43_tcpip_link_status(cyw43_t *self, int itf);
void cyw43_arch_poll(void);
void cyw43_arch_lwip_begin(void);
void cyw43_arch_lwip_end(void);

#endif
