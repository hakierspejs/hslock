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
#include <stdint.h>
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

// Bumped once per ntp_sync(); passed through the DNS callback arg so a delayed
// reply from an earlier, timed-out request is dropped instead of clobbering the
// state of a later sync (M14).
static uint32_t ntp_generation = 0;

static bool     synced                 = false;
static uint32_t last_sync_unix         = 0;
static uint64_t last_sync_monotonic_us = 0;

// Cumulative backward slack consumed since boot (see NTP_ROLLBACK_BUDGET_S).
static uint32_t rollback_budget_used_s = 0;

// ---------------------------------------------------------------------------
// Rollback protection
// ---------------------------------------------------------------------------

static bool rollback_check(uint32_t new_time) {
    if (!synced)
        return true; // no floor before first sync

    uint64_t elapsed_us = time_us_64() - last_sync_monotonic_us;
    uint32_t elapsed_s  = (uint32_t)(elapsed_us / 1000000ULL);

    // Where the monotonic clock says we should be now, in saturating arithmetic
    // so a huge elapsed_s can't wrap the projection around to a small value.
    uint32_t projected =
        (last_sync_unix > UINT32_MAX - elapsed_s) ? UINT32_MAX : last_sync_unix + elapsed_s;

    // Lower bound: at most NTP_ROLLBACK_EPSILON_S below the projection, computed
    // saturating so it never underflows to ~4.29e9 and freezes a bogus floor.
    uint32_t floor = (projected > NTP_ROLLBACK_EPSILON_S) ? projected - NTP_ROLLBACK_EPSILON_S : 0;
    if (new_time < floor) {
        printf("[ntp] rollback rejected: got %u, floor is %u\r\n", new_time, floor);
        return false;
    }

    // Upper bound: cap a single forward step so one on-path packet can't jump
    // the clock far ahead of the projection (the sane band catches only the
    // wildest jumps).
    if (projected <= UINT32_MAX - NTP_MAX_FORWARD_STEP_S &&
        new_time > projected + NTP_MAX_FORWARD_STEP_S) {
        printf("[ntp] forward jump rejected: got %u, projected %u\r\n", new_time, projected);
        return false;
    }

    // Cumulative backward budget: charge any correction that lands below the
    // projection; reject once the per-boot budget is exhausted so repeated
    // small rollbacks can't walk the clock back without bound.
    if (new_time < projected) {
        uint32_t backstep = projected - new_time;
        if (backstep > NTP_ROLLBACK_BUDGET_S - rollback_budget_used_s) {
            printf("[ntp] rollback budget exhausted: used %u, want %u more\r\n",
                   rollback_budget_used_s, backstep);
            return false;
        }
        rollback_budget_used_s += backstep;
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
    if (p->tot_len < 48 || ntp_state != NTP_STATE_WAITING) { // use tot_len not len (L10 fix too)
        printf("[ntp] response error\r\n");
        pbuf_free(p);
        return;
    }

    uint8_t buf[48];
    pbuf_copy_partial(p, buf, 48, 0);
    pbuf_free(p);

    // Validate source (belt-and-suspenders — udp_connect already filters)
    if (!ip_addr_cmp(addr, &server_addr) || port != NTP_PORT) {
        printf("[ntp] unexpected source\r\n");
        return;
    }

    // Validate LI, VN, mode — buf[0]: LI(7:6) VN(5:3) mode(2:0)
    uint8_t li      = (buf[0] >> 6) & 0x03;
    uint8_t mode    = buf[0] & 0x07;
    uint8_t stratum = buf[1];

    if (li == 3) { // LI=3: clock unsynchronised
        printf("[ntp] server clock unsynchronised\r\n");
        return;
    }
    if (mode != 4) { // mode must be 4 (server)
        printf("[ntp] unexpected mode: %u\r\n", mode);
        return;
    }
    if (stratum == 0 || stratum > 15) { // 0=kiss-o-death, >15=invalid
        printf("[ntp] invalid stratum: %u\r\n", stratum);
        return;
    }

    // Verify origin timestamp (bytes 24-31) echoes our nonce
    if (memcmp(&buf[24], ntp_nonce, 8) != 0) {
        printf("[ntp] origin timestamp mismatch — possible replay\r\n");
        return;
    }

    uint32_t seconds_since_1900 = ((uint32_t)buf[40] << 24) | ((uint32_t)buf[41] << 16) |
                                  ((uint32_t)buf[42] << 8) | (uint32_t)buf[43];

    // Guard the epoch subtraction: a value below NTP_DELTA (pre-1970, e.g. a
    // spoofed 0) would wrap to the far future instead of rejecting.
    if (seconds_since_1900 < NTP_DELTA) {
        printf("[ntp] timestamp before unix epoch: %u\r\n", seconds_since_1900);
        ntp_state = NTP_STATE_FAILED;
        return;
    }

    uint32_t unix_time = seconds_since_1900 - NTP_DELTA;

    // Unconditional sanity band - rollback_check() alone is not enough: it has
    // no floor at all before the first sync, and no upper bound ever. This
    // catches both a spoofed epoch 0 (H2) and a wrapped-around or wildly-future
    // seconds_since_1900 (H3) before either reaches the RTC.
    if (unix_time < BUILD_UNIX_TIME || unix_time - BUILD_UNIX_TIME > NTP_SANE_MAX_AGE_S) {
        printf("[ntp] timestamp outside sane band: %u\r\n", unix_time);
        return;
    }

    if (!rollback_check(unix_time)) {
        return;
    }

    apply_time(unix_time);
    ntp_state = NTP_STATE_SUCCESS;
}

// ---------------------------------------------------------------------------
// DNS callback
// ---------------------------------------------------------------------------

static void dns_found_cb(const char *name, const ip_addr_t *ipaddr, void *arg) {
    // Drop a stale callback from an earlier, timed-out sync (M14): a delayed DNS
    // reply must not clobber server_addr / nonce / state of a later request.
    if ((uint32_t)(uintptr_t)arg != ntp_generation)
        return;

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

    // New request generation — any DNS callback carrying an older token is stale.
    uint32_t generation = ++ntp_generation;
    void    *gen_arg    = (void *)(uintptr_t)generation;

    cyw43_arch_lwip_begin();
    err_t err = dns_gethostbyname(NTP_SERVER, &server_addr, dns_found_cb, gen_arg);
    cyw43_arch_lwip_end();

    if (err == ERR_OK) {
        // Already cached - fire callback manually
        dns_found_cb(NTP_SERVER, &server_addr, gen_arg);
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
