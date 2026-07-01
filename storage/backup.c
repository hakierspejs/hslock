#include "backup.h"
#include "storage.h"

#include <string.h>
#include <stdio.h>

int backup_export(uint8_t *buf, size_t buf_size) {
    key_record_t keys[BACKUP_MAX_KEYS];
    int count = storage_key_list(keys, BACKUP_MAX_KEYS);
    if (count < 0) return -1;

    size_t needed = sizeof(backup_header_t) + count * sizeof(key_record_t);
    if (buf_size < needed) return -1;

    backup_header_t hdr = {
        .magic     = BACKUP_MAGIC,
        .version   = BACKUP_VERSION,
        .key_count = (uint32_t)count,
    };

    uint8_t *p = buf;
    memcpy(p, &hdr, sizeof(hdr));
    p += sizeof(hdr);
    memcpy(p, keys, count * sizeof(key_record_t));

    return (int)needed;
}

bool backup_import(const uint8_t *buf, size_t size) {
    if (size < sizeof(backup_header_t)) return false;

    const backup_header_t *hdr = (const backup_header_t *)buf;
    if (hdr->magic   != BACKUP_MAGIC)   return false;
    if (hdr->version != BACKUP_VERSION) return false;

    size_t expected = sizeof(backup_header_t)
                    + hdr->key_count * sizeof(key_record_t);
    if (size < expected) return false;

    const key_record_t *keys =
        (const key_record_t *)(buf + sizeof(backup_header_t));

    // Delete all existing keys
    key_record_t existing[BACKUP_MAX_KEYS];
    int existing_count = storage_key_list(existing, BACKUP_MAX_KEYS);
    for (int i = 0; i < existing_count; i++) {
        storage_key_delete(existing[i].id);
    }

    // Write new keys
    for (uint32_t i = 0; i < hdr->key_count; i++) {
        if (!storage_key_save(&keys[i])) {
            printf("[backup] import: failed to write key %u\r\n", keys[i].id);
            return false;
        }
    }

    printf("[backup] import: wrote %u keys\r\n", hdr->key_count);
    return true;
}
