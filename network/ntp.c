#include "ntp.h"
#include "wifi.h"
#include "hardware/buzzer.h"
#include "hardware/clock.h"
#include "version.h"

#include "pico/cyw43_arch.h"
#include "pico/rand.h"
#include "pico/time.h"
#include "lwip/udp.h"
#include "lwip/dns.h"

#include <string.h>
#include <stdio.h>
#include <time.h>

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

#define NTP_PORT   123
#define NTP_SERVER "pool.ntp.org"
#define NTP_DELTA  2208988800UL // seconds between 1900 and 1970 epochs

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

typedef enum {
    NTP_STATE_IDLE,
    NTP_STATE_RESOLVING,
    NTP_STATE_WAITING,
    NTP_STATE_SUCCESS,
    NTP_STATE_FAILED,
} ntp_state_t;

static ntp_state_t     ntp_state = NTP_STATE_IDLE;
static uint8_t         ntp_nonce[8]; // stored transmit timestamp for origin check
static struct udp_pcb *ntp_pcb = NULL;
static ip_addr_t       server_addr;

static bool     synced                 = false;
static uint32_t last_sync_unix         = 0;
static uint64_t last_sync_monotonic_us = 0;

// ---------------------------------------------------------------------------
// Rollback protection
// ---------------------------------------------------------------------------

static bool rollback_check(uint32_t new_time) {
    if (!synced)
        return true; // no floor before first sync

    uint64_t elapsed_us = time_us_64() - last_sync_monotonic_us;
    uint32_t elapsed_s  = (uint32_t)(elapsed_us / 1000000ULL);
    uint32_t floor      = last_sync_unix + elapsed_s - NTP_ROLLBACK_EPSILON_S;

    if (new_time < floor) {
        printf("[ntp] rollback rejected: got %u, floor is %u\r\n", new_time, floor);
        return false;
    }

    return true;
}

// ---------------------------------------------------------------------------
// Set RTC from unix timestamp
// ---------------------------------------------------------------------------

static void apply_time(uint32_t unix_time) {
    clock_set_from_unix_time(unix_time);

    last_sync_unix         = unix_time;
    last_sync_monotonic_us = time_us_64();
    synced                 = true;

    printf("[ntp] synced: unix=%u\r\n", unix_time);
}

// ---------------------------------------------------------------------------
// NTP response callback
// ---------------------------------------------------------------------------

static void ntp_recv_cb(void *arg, struct udp_pcb *pcb, struct pbuf *p, const ip_addr_t *addr,
                        u16_t port) {
    if (p->tot_len < 48) { // use tot_len not len (L10 fix too)
        printf("[ntp] response too short\r\n");
        pbuf_free(p);
        ntp_state = NTP_STATE_FAILED;
        return;
    }

    uint8_t buf[48];
    pbuf_copy_partial(p, buf, 48, 0);
    pbuf_free(p);

    // Validate source (belt-and-suspenders — udp_connect already filters)
    if (!ip_addr_cmp(addr, &server_addr) || port != NTP_PORT) {
        printf("[ntp] unexpected source\r\n");
        ntp_state = NTP_STATE_FAILED;
        return;
    }

    // Validate LI, VN, mode — buf[0]: LI(7:6) VN(5:3) mode(2:0)
    uint8_t li      = (buf[0] >> 6) & 0x03;
    uint8_t mode    = buf[0] & 0x07;
    uint8_t stratum = buf[1];

    if (li == 3) { // LI=3: clock unsynchronised
        printf("[ntp] server clock unsynchronised\r\n");
        ntp_state = NTP_STATE_FAILED;
        return;
    }
    if (mode != 4) { // mode must be 4 (server)
        printf("[ntp] unexpected mode: %u\r\n", mode);
        ntp_state = NTP_STATE_FAILED;
        return;
    }
    if (stratum == 0 || stratum > 15) { // 0=kiss-o-death, >15=invalid
        printf("[ntp] invalid stratum: %u\r\n", stratum);
        ntp_state = NTP_STATE_FAILED;
        return;
    }

    // Verify origin timestamp (bytes 24-31) echoes our nonce
    if (memcmp(&buf[24], ntp_nonce, 8) != 0) {
        printf("[ntp] origin timestamp mismatch — possible replay\r\n");
        ntp_state = NTP_STATE_FAILED;
        return;
    }

    uint32_t seconds_since_1900 = ((uint32_t)buf[40] << 24) | ((uint32_t)buf[41] << 16) |
                                  ((uint32_t)buf[42] << 8) | (uint32_t)buf[43];

    uint32_t unix_time = seconds_since_1900 - NTP_DELTA;

    // Unconditional sanity band - rollback_check() alone is not enough: it has
    // no floor at all before the first sync, and no upper bound ever. This
    // catches both a spoofed epoch 0 (H2) and a wrapped-around or wildly-future
    // seconds_since_1900 (H3) before either reaches the RTC.
    if (unix_time < BUILD_UNIX_TIME || unix_time - BUILD_UNIX_TIME > NTP_SANE_MAX_AGE_S) {
        printf("[ntp] timestamp outside sane band: %u\r\n", unix_time);
        ntp_state = NTP_STATE_FAILED;
        return;
    }

    if (!rollback_check(unix_time)) {
        ntp_state = NTP_STATE_FAILED;
        return;
    }

    apply_time(unix_time);
    ntp_state = NTP_STATE_SUCCESS;
}

// ---------------------------------------------------------------------------
// DNS callback
// ---------------------------------------------------------------------------

static void dns_found_cb(const char *name, const ip_addr_t *ipaddr, void *arg) {
    if (!ipaddr) {
        printf("[ntp] DNS failed\r\n");
        ntp_state = NTP_STATE_FAILED;
        return;
    }

    server_addr = *ipaddr;

    // Connect UDP pcb to server — reject datagrams from other sources
    udp_connect(ntp_pcb, &server_addr, NTP_PORT);

    struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, 48, PBUF_RAM);
    if (!p) {
        printf("[ntp] pbuf_alloc failed\r\n");
        ntp_state = NTP_STATE_FAILED;
        return;
    }
    uint8_t *req = (uint8_t *)p->payload;
    memset(req, 0, 48);
    req[0] = 0x1B; // LI=0, VN=3, Mode=3 (client)

    // Generate random nonce and place in transmit timestamp (bytes 40-47)
    uint64_t nonce = get_rand_64();
    memcpy(&req[40], &nonce, 8);
    memcpy(ntp_nonce, &req[40], 8); // save for origin check

    udp_sendto(ntp_pcb, p, &server_addr, NTP_PORT);
    pbuf_free(p);

    ntp_state = NTP_STATE_WAITING;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void ntp_init(void) {
    rtc_init();
}

bool ntp_sync(void) {
    if (ntp_pcb) {
        udp_remove(ntp_pcb);
        ntp_pcb = NULL;
    }

    ntp_pcb = udp_new_ip_type(IPADDR_TYPE_ANY);
    if (!ntp_pcb) {
        printf("[ntp] failed to create UDP pcb\r\n");
        return false;
    }

    udp_recv(ntp_pcb, ntp_recv_cb, NULL);
    ntp_state = NTP_STATE_RESOLVING;

    cyw43_arch_lwip_begin();
    err_t err = dns_gethostbyname(NTP_SERVER, &server_addr, dns_found_cb, NULL);
    cyw43_arch_lwip_end();

    if (err == ERR_OK) {
        // Already cached - fire callback manually
        dns_found_cb(NTP_SERVER, &server_addr, NULL);
    } else if (err != ERR_INPROGRESS) {
        printf("[ntp] DNS error: %d\r\n", err);
        udp_remove(ntp_pcb);
        ntp_pcb = NULL;
        return false;
    }

    // Poll until done or timeout
    absolute_time_t deadline = make_timeout_time_ms(NTP_TIMEOUT_S * 1000);
    while (ntp_state != NTP_STATE_SUCCESS && ntp_state != NTP_STATE_FAILED) {
        cyw43_arch_poll();
        sleep_ms(10);
        if (time_reached(deadline)) {
            printf("[ntp] timed out\r\n");
            udp_remove(ntp_pcb);
            ntp_pcb = NULL;
            return false;
        }
    }

    bool ok   = ntp_state == NTP_STATE_SUCCESS;
    ntp_state = NTP_STATE_IDLE;

    udp_remove(ntp_pcb);
    ntp_pcb = NULL;

    return ok;
}

void ntp_task(void) {
    if (!synced)
        return;
    if (!wifi_is_connected())
        return;

    uint64_t elapsed_us = time_us_64() - last_sync_monotonic_us;
    uint32_t elapsed_s  = (uint32_t)(elapsed_us / 1000000ULL);

    if (elapsed_s < NTP_RESYNC_INTERVAL_S)
        return;

    printf("[ntp] periodic resync...\r\n");
    if (!ntp_sync()) {
        printf("[ntp] periodic resync failed, continuing on RTC\r\n");
        buzzer_play_ntp_sync_error();
    }
}

uint32_t ntp_last_sync_time(void) {
    return last_sync_unix;
}

bool ntp_is_synced(void) {
    return synced;
}
