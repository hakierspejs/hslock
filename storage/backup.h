#ifndef BACKUP_H
#define BACKUP_H

#include "storage.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// ---------------------------------------------------------------------------
// Binary format (base64-encoded for serial transport)
// ---------------------------------------------------------------------------

#define BACKUP_MAGIC    0x4C4B5348U  // "SHKL"
#define BACKUP_VERSION  1
#define BACKUP_MAX_KEYS 1024

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint32_t version;
    uint32_t key_count;
} backup_header_t;

// ---------------------------------------------------------------------------
// API
// ---------------------------------------------------------------------------

// Serialise all keys into buf. Returns byte count written, -1 on error.
int  backup_export(uint8_t *buf, size_t buf_size);

// Overwrite all keys from buf. Returns false on error.
bool backup_import(const uint8_t *buf, size_t size);

#endif
