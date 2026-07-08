#ifndef NTP_H
#define NTP_H

#include <stdint.h>
#include <stdbool.h>

#define NTP_RESYNC_INTERVAL_S  (30 * 60) // 30 minutes
#define NTP_RETRY_INTERVAL_S   5         // retry on boot failure
#define NTP_TIMEOUT_S          15        // per-sync timeout
#define NTP_ROLLBACK_EPSILON_S 60        // max allowed backward correction

// Call once after WiFi connected - initialises RTC
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
