#ifndef WIFI_H
#define WIFI_H

#include <stdbool.h>

// Connect to WiFi. Returns false on failure.
bool wifi_connect(const char *ssid, const char *password);

// True if currently connected
bool wifi_is_connected(void);

#endif
