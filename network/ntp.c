#include "ntp.h"
#include "wifi.h"
#include "hardware/buzzer.h"
#include "hardware/clock.h"
#include "version.h"

#include "pico/cyw43_arch.h"
#include "pico/rand.h"
#include "pico/time.h"
#include "hardware/sync.h"
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

static volatile bool     synced                 = false;
static volatile uint32_t last_sync_unix         = 0;
static volatile uint64_t last_sync_monotonic_us = 0;

// ---------------------------------------------------------------------------
// 64-bit torn-read guard (M10b)
//
// last_sync_monotonic_us is 64-bit; on the RP2040 a 64-bit load/store is not
// atomic and `volatile` does not make it so. Guard every read and write of the
// (synced, last_sync_unix, last_sync_monotonic_us) triple with a brief critical
// section so a reader never observes a half-updated pair. Cheap: these run only
// on the NTP sync path and the once-per-loop resync check.
// ---------------------------------------------------------------------------

static void store_sync(uint32_t unix_time) {
    uint32_t irq           = save_and_disable_interrupts();
    last_sync_unix         = unix_time;
    last_sync_monotonic_us = time_us_64();
    synced                 = true;
    restore_interrupts(irq);
}

static void load_sync(bool *synced_out, uint32_t *unix_out, uint64_t *mono_out) {
    uint32_t irq = save_and_disable_interrupts();
    *synced_out  = synced;
    *unix_out    = last_sync_unix;
    *mono_out    = last_sync_monotonic_us;
    restore_interrupts(irq);
}

// Cumulative backward slack consumed since boot (see NTP_ROLLBACK_BUDGET_S).
static uint32_t rollback_budget_used_s = 0;

// ---------------------------------------------------------------------------
// Rollback protection
// ---------------------------------------------------------------------------

static bool rollback_check(uint32_t new_time) {
    bool     is_synced;
    uint32_t sync_unix;
    uint64_t sync_mono;
    load_sync(&is_synced, &sync_unix, &sync_mono);

    if (!is_synced)
        return true; // no floor before first sync

    uint64_t elapsed_us = time_us_64() - sync_mono;
    uint32_t elapsed_s  = (uint32_t)(elapsed_us / 1000000ULL);

    // Where the monotonic clock says we should be now, in saturating arithmetic
    // so a huge elapsed_s can't wrap the projection around to a small value.
    // Uses the snapshot (sync_unix) taken with sync_mono above so the pair stays
    // consistent even if a concurrent sync updates the globals mid-check (M10b).
    uint32_t projected = (sync_unix > UINT32_MAX - elapsed_s) ? UINT32_MAX : sync_unix + elapsed_s;

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
// Set RTC from unix timestamp (thread context only — see ntp_process_pending)
// ---------------------------------------------------------------------------

static void apply_time(uint32_t unix_time) {
    clock_set_from_unix_time(unix_time);
    store_sync(unix_time);
    printf("[ntp] synced: unix=%u\r\n", unix_time);
}

// ---------------------------------------------------------------------------
// NTP response callback + deferred processing (M10b)
//
// ntp_recv_cb is invoked by lwIP from a low-priority IRQ. Doing the RTC write
// (clock_set_from_unix_time) there races core 0's rtc_get in totp_verify (torn
// read an attacker can time), and printf there can deadlock against the stdio
// lock core 0 may hold. So the callback does the absolute minimum: copy the
// datagram + source into a single slot and raise a flag. All validation, the
// RTC write and every printf run in ntp_process_pending() from the ntp_sync()
// poll loop — i.e. thread context on core 0, the same context as rtc_get.
// ---------------------------------------------------------------------------

static volatile bool ntp_pkt_pending = false;
static uint8_t       ntp_pkt_buf[48];
static uint16_t      ntp_pkt_len = 0;
static ip_addr_t     ntp_pkt_addr;
static uint16_t      ntp_pkt_port = 0;

static void ntp_recv_cb(void *arg, struct udp_pcb *pcb, struct pbuf *p, const ip_addr_t *addr,
                        u16_t port) {
    (void)arg;
    (void)pcb;

    // IRQ/lwIP context: copy the (bounded) datagram + source, raise the flag,
    // and free the pbuf. No RTC write, no printf, no shared-state validation.
    uint16_t n = p->tot_len < 48 ? p->tot_len : 48;
    pbuf_copy_partial(p, ntp_pkt_buf, n, 0);
    ntp_pkt_len     = p->tot_len;
    ntp_pkt_addr    = *addr;
    ntp_pkt_port    = port;
    ntp_pkt_pending = true;

    pbuf_free(p);
}

// Thread-context processor. Run from the ntp_sync() poll loop. When a datagram
// is pending it snapshots the slot under a brief critical section (the slot is
// written from IRQ), runs the full validation, and on success does the RTC
// write via apply_time(). Sets ntp_state to SUCCESS/FAILED to release the poll
// loop. No-op when nothing is pending.
static void ntp_process_pending(void) {
    if (!ntp_pkt_pending)
        return;

    uint8_t   buf[48];
    uint16_t  len;
    ip_addr_t addr;
    uint16_t  port;

    uint32_t irq = save_and_disable_interrupts();
    memcpy(buf, ntp_pkt_buf, sizeof(buf));
    len             = ntp_pkt_len;
    addr            = ntp_pkt_addr;
    port            = ntp_pkt_port;
    ntp_pkt_pending = false;
    restore_interrupts(irq);

    if (len < 48) { // use tot_len not len (L10 fix too)
        printf("[ntp] response too short\r\n");
        ntp_state = NTP_STATE_FAILED;
        return;
    }

    // Validate source (belt-and-suspenders — udp_connect already filters)
    if (!ip_addr_cmp(&addr, &server_addr) || port != NTP_PORT) {
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
    ntp_pkt_pending = false; // discard any stale datagram from a previous sync
    ntp_state       = NTP_STATE_RESOLVING;

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
        ntp_process_pending(); // thread-context validation/RTC-write/printf (M10b)
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
    bool     is_synced;
    uint32_t sync_unix;
    uint64_t sync_mono;
    load_sync(&is_synced, &sync_unix, &sync_mono);

    if (!is_synced)
        return;
    if (!wifi_is_connected())
        return;

    uint64_t elapsed_us = time_us_64() - sync_mono;
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
