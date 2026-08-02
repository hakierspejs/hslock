#ifndef NTP_H
#define NTP_H

#include <stdint.h>
#include <stdbool.h>

#define NTP_RESYNC_INTERVAL_S (30 * 60) // 30 minutes
#define NTP_RETRY_INTERVAL_S  5         // retry on boot failure
#define NTP_TIMEOUT_S         15        // per-sync timeout

// Max backward correction tolerated on a single accepted sync, kept well under
// one TOTP step (30 s) so a rollback within it can never move the current OTP
// window. Covers only crystal drift + network jitter between syncs.
#define NTP_ROLLBACK_EPSILON_S 5

// Cumulative backward slack the clock may absorb across ALL syncs in one boot.
// Each accepted response that lands below the monotonic projection charges the
// difference against this budget; once exhausted, further backward corrections
// are rejected. Stops a deauth/replay loop from walking the clock back
// NTP_ROLLBACK_EPSILON_S per sync without bound.
#define NTP_ROLLBACK_BUDGET_S 300

// Max forward step tolerated on a single accepted sync once synced. The
// absolute sane band below already rejects a wild year-2100 jump; this is the
// tighter per-sync cap that keeps an on-path packet from yanking the clock a
// smaller-but-still-bogus distance ahead of the monotonic projection.
#define NTP_MAX_FORWARD_STEP_S 86400 // 1 day

// Sanity band for any timestamp accepted from the network: it must fall between
// the firmware's own build time and this many years after it. Applied
// unconditionally - including before the first sync, where rollback_check()
// has no floor at all - so a single malformed/spoofed response (e.g. epoch 0,
// or a wrapped-around seconds_since_1900 field) can't be applied to the RTC.
#define NTP_SANE_YEARS_AHEAD 20
#define NTP_SANE_MAX_AGE_S   ((uint32_t)NTP_SANE_YEARS_AHEAD * 365 * 86400)

// Call once at boot, before any WiFi/storage checks - initialises the RTC.
// Must not be gated on WiFi/storage availability: the RTC's clock divider is
// configured here, and any rtc_set_datetime()/rtc_get_datetime() call before
// this runs uses an unconfigured divider, causing the RTC to free-run far
// faster than 1Hz.
void ntp_init(void);

// Perform NTP sync. Blocks until done or timeout.
// Returns true on success.
bool ntp_sync(void);

// Call in core 0 loop - handles periodic resync
void ntp_task(void);

// Unix timestamp of last successful sync, 0 if never
uint32_t ntp_last_sync_time(void);

// True if at least one successful sync has completed
bool ntp_is_synced(void);

#endif
