#include "wifi.h"
#include "pico/cyw43_arch.h"
#include <stdio.h>
#include "storage/storage.h"
#include "network/ntp.h"
#include "pico/time.h"

static bool initialised = false;

bool wifi_connect(const char *ssid, const char *password) {
    if (!initialised) {
        if (cyw43_arch_init()) {
            printf("[wifi] failed to init cyw43\r\n");
            return false;
        }
        cyw43_arch_enable_sta_mode();
        initialised = true;
    }

    printf("[wifi] connecting to '%s'...\r\n", ssid);
    // Cap the blocking association attempt below the ~8s watchdog window: since
    // core 0 now feeds the watchdog itself (ISSUES.md H4), a 15s blocking
    // connect on the wifi_task() reconnect path would trip a reset mid-attempt
    // and reboot-loop while the AP is slow/unreachable. A failed attempt is
    // retried by wifi_task() on its next tick.
    int rc = cyw43_arch_wifi_connect_timeout_ms(ssid, password, CYW43_AUTH_WPA2_AES_PSK, 6000);

    if (rc) {
        printf("[wifi] connect failed: %d\r\n", rc);
        return false;
    }

    printf("[wifi] connected\r\n");
    return true;
}

bool wifi_is_connected(void) {
    if (!initialised)
        return false;
    return cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA) == CYW43_LINK_UP;
}

typedef enum {
    WIFI_LOG_STATE_UNKNOWN,
    WIFI_LOG_STATE_CONNECTED,
    WIFI_LOG_STATE_NO_CREDENTIALS,
    WIFI_LOG_STATE_RETRY_FAILED,
} wifi_log_state_t;

void wifi_task(void) {
    static absolute_time_t  next_check        = {0};
    static wifi_log_state_t last_logged_state = WIFI_LOG_STATE_UNKNOWN;

    if (!time_reached(next_check))
        return;
    next_check = make_timeout_time_ms(30000); // check every 30s

    if (wifi_is_connected()) {
        last_logged_state = WIFI_LOG_STATE_CONNECTED;
        return;
    }

    wifi_config_t cfg;
    if (!storage_wifi_get(&cfg)) {
        // No credentials stored - nothing to retry until 'set-wifi' is run.
        // Log once on the transition instead of every 30s forever.
        if (last_logged_state != WIFI_LOG_STATE_NO_CREDENTIALS)
            printf("[wifi] no credentials stored - not retrying\r\n");
        last_logged_state = WIFI_LOG_STATE_NO_CREDENTIALS;
        return;
    }

    if (last_logged_state != WIFI_LOG_STATE_RETRY_FAILED)
        printf("[wifi] connection lost, reconnecting...\r\n");

    if (wifi_connect(cfg.ssid, cfg.password)) {
        printf("[wifi] reconnected\r\n");
        // Don't force an NTP sync on every reconnect: a deauth/reassociate loop
        // would otherwise drive one sync per cycle, bypassing the resync
        // interval and its rollback budget. ntp_task() schedules resyncs per
        // NTP_RESYNC_INTERVAL_S.
        last_logged_state = WIFI_LOG_STATE_CONNECTED;
    } else {
        printf("[wifi] reconnect failed\r\n");
        last_logged_state = WIFI_LOG_STATE_RETRY_FAILED;
    }
}
