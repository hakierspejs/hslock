#ifndef BACKUP_H
#define BACKUP_H

#include "storage.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// ---------------------------------------------------------------------------
// Binary format (base64-encoded for serial transport)
// ---------------------------------------------------------------------------

#define BACKUP_MAGIC    0x4C4C5348U  // "HSLL"
#define BACKUP_VERSION  1
#define BACKUP_MAX_KEYS KEY_MAX_COUNT

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint32_t version;
    uint32_t key_count;
    uint32_t checksum;   // covers all backup_key_t records
} backup_header_t;

typedef struct __attribute__((packed)) {
    uint16_t id;
    char     name[KEY_NAME_MAX];
    uint8_t  secret[KEY_SECRET_LEN];
    bool     is_enabled;
    bool     is_admin;
    uint32_t created_at;
} backup_key_t;

// ---------------------------------------------------------------------------
// API
// ---------------------------------------------------------------------------

// Serialise all keys into buf. Returns byte count written, -1 on error.
int  backup_export(uint8_t *buf, size_t buf_size);

// Overwrite all keys from buf. Returns false on error.
bool backup_import(const uint8_t *buf, size_t size);

#endif
