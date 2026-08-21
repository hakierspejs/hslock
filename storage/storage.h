#ifndef STORAGE_H
#define STORAGE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// ---------------------------------------------------------------------------
// Key record - fields stored per TOTP key
// ---------------------------------------------------------------------------

#define KEY_MAX_COUNT 128
#define KEY_ID_MAX    KEY_MAX_COUNT - 1

#define KEY_NAME_MAX   16
#define KEY_SECRET_LEN 20 // HMAC-SHA1 seed, matches old Arduino format

typedef struct {
    uint16_t id;
    char     name[KEY_NAME_MAX];
    uint8_t  secret[KEY_SECRET_LEN];
    bool     is_enabled;
    bool     is_admin;
    uint32_t created_at; // unix timestamp
    bool     is_checksum_valid;
} key_record_t;

// ---------------------------------------------------------------------------
// WiFi config
// ---------------------------------------------------------------------------

#define WIFI_SSID_MAX     64
#define WIFI_PASSWORD_MAX 64

typedef struct {
    char ssid[WIFI_SSID_MAX];
    char password[WIFI_PASSWORD_MAX];
} wifi_config_t;

// ---------------------------------------------------------------------------
// API
// ---------------------------------------------------------------------------

// Mount LittleFS. Call once from core 0 before launching core 1.
// Returns false if mount fails (formats and retries once).
bool storage_init(void);

bool storage_is_mounted(void);

// Explicitly erase and reinitialise storage.
// Only call when storage_init() has failed and operator has confirmed.
bool storage_format(void);

// WiFi credentials
bool storage_wifi_get(wifi_config_t *out);
bool storage_wifi_set(const wifi_config_t *cfg);
bool storage_wifi_clear(void);

// Key CRUD
bool storage_key_exists(uint16_t id);
bool storage_key_get(uint16_t id, key_record_t *out);
bool storage_key_save(const key_record_t *key); // create or update
bool storage_key_delete(uint16_t id);

// Atomically replace the ENTIRE key set with `records` (count in [0, KEY_MAX_COUNT]).
// New records are staged to a temporary directory first; the previous keys are
// removed only after every staged write has succeeded. A failure (or power loss)
// mid-operation therefore leaves the store with its ORIGINAL key set intact,
// never a wiped or partially written one. Returns false on any error.
bool storage_key_replace_all(const key_record_t *records, int count);

// List all keys. Returns count written into out[].
int storage_key_list(key_record_t *out, int max_count);

#endif