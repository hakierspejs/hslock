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
    int rc = cyw43_arch_wifi_connect_timeout_ms(ssid, password, CYW43_AUTH_WPA2_AES_PSK, 15000);

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

void wifi_task(void) {
    static absolute_time_t next_check = {0};

    if (!time_reached(next_check))
        return;
    next_check = make_timeout_time_ms(30000); // check every 30s

    if (wifi_is_connected())
        return;

    printf("[wifi] connection lost, reconnecting...\r\n");

    wifi_config_t cfg;
    if (!storage_wifi_get(&cfg)) {
        printf("[wifi] no credentials stored\r\n");
        return;
    }

    if (wifi_connect(cfg.ssid, cfg.password)) {
        printf("[wifi] reconnected\r\n");
        ntp_sync();
    } else {
        printf("[wifi] reconnect failed\r\n");
    }
}
