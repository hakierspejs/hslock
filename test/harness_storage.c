/*
 * Host harness: storage/storage.c + storage/backup.c on a RAM-backed LittleFS.
 *
 * The firmware's block device reads flash by dereferencing an absolute XIP
 * address (XIP_BASE + STORAGE_FLASH_OFFSET + ...) and writes it through
 * flash_range_program/erase (offset-from-flash-base). Both refer to the same
 * XIP window. We reproduce that window in host RAM with a single MAP_FIXED
 * anonymous mapping placed exactly at (void*)(XIP_BASE + STORAGE_FLASH_OFFSET):
 *   - storage.c's flash_read() dereferences (const void*)(uint32_t addr); the
 *     address lands inside the mapping, so reads hit our RAM.
 *   - the flash_range_program()/erase() shims below write the same window
 *     with NOR-flash semantics (erase -> 0xFF, program -> bitwise AND).
 *   - flash_safe_execute() simply calls the callback (no cores to pause).
 * This leaves the firmware source untouched: storage.c + backup.c + the real
 * littlefs lfs.c/lfs_util.c run natively against the RAM window, so their CRUD
 * / wifi / backup logic gets genuinely exercised.
 */

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>

#include "hardware/flash.h" /* XIP_BASE, FLASH_SECTOR_SIZE */
#include "pico/stdlib.h"    /* PICO_FLASH_SIZE_BYTES, PICO_OK */

#include "backup.h"
#include "storage.h"

/* Must match storage.c's private layout constants. */
#define STORAGE_SIZE_BYTES   (256 * 1024)
#define STORAGE_FLASH_OFFSET (PICO_FLASH_SIZE_BYTES - STORAGE_SIZE_BYTES)
#define STORAGE_WINDOW_BASE  ((uintptr_t)(XIP_BASE + STORAGE_FLASH_OFFSET))

/* Absolute host pointer for a flash-base-relative offset. */
static uint8_t *flash_ptr(uint32_t flash_offs) {
    return (uint8_t *)(uintptr_t)(XIP_BASE + flash_offs);
}

/* --- RAM-backed flash primitives storage.c drives ------------------------- */

void flash_range_program(uint32_t flash_offs, const uint8_t *data, size_t count) {
    uint8_t *dst = flash_ptr(flash_offs);
    for (size_t i = 0; i < count; i++)
        dst[i] &= data[i]; /* NOR: programming can only clear bits */
}

void flash_range_erase(uint32_t flash_offs, size_t count) {
    memset(flash_ptr(flash_offs), 0xFF, count); /* erased NOR reads as 0xFF */
}

int flash_safe_execute(void (*func)(void *), void *param, uint32_t timeout_ms) {
    (void)timeout_ms;
    func(param);
    return PICO_OK;
}

/* Map the XIP storage window into host RAM at its true address. */
static void flash_ram_map(void) {
    void *base = mmap((void *)STORAGE_WINDOW_BASE, STORAGE_SIZE_BYTES, PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
    if (base == MAP_FAILED) {
        perror("mmap XIP window");
        assert(0);
    }
    assert((uintptr_t)base == STORAGE_WINDOW_BASE);
    memset(base, 0xFF, STORAGE_SIZE_BYTES); /* fresh, fully-erased flash */
}

/* --- helpers -------------------------------------------------------------- */

static key_record_t make_key(uint16_t id, const char *name, bool enabled, bool admin,
                             uint32_t created, uint8_t seed) {
    key_record_t k = {0};
    k.id           = id;
    k.is_enabled   = enabled;
    k.is_admin     = admin;
    k.created_at   = created;
    snprintf(k.name, sizeof k.name, "%s", name);
    for (int i = 0; i < KEY_SECRET_LEN; i++)
        k.secret[i] = (uint8_t)(seed + i);
    return k;
}

static bool keys_equal(const key_record_t *a, const key_record_t *b) {
    return a->id == b->id && a->is_enabled == b->is_enabled && a->is_admin == b->is_admin &&
           a->created_at == b->created_at && memcmp(a->name, b->name, sizeof a->name) == 0 &&
           memcmp(a->secret, b->secret, sizeof a->secret) == 0;
}

int main(void) {
    /* --- before mount: every op must refuse -------------------------------- */
    key_record_t tmp;
    assert(storage_key_exists(1) == false);
    assert(storage_key_get(1, &tmp) == false);
    assert(storage_key_save(&tmp) == false);
    assert(storage_key_delete(1) == false);
    assert(storage_key_list(&tmp, 1) == -1);
    wifi_config_t wtmp;
    assert(storage_wifi_get(&wtmp) == false);
    assert(storage_wifi_set(&wtmp) == false);
    assert(storage_wifi_clear() == false);

    /* --- init on fresh 0xFF flash: mount fails -> format -> mount ---------- */
    flash_ram_map();
    assert(storage_init() == true);

    /* --- key save/get roundtrip ------------------------------------------- */
    key_record_t k1 = make_key(1, "alice", true, true, 1700000000u, 0x10);
    assert(storage_key_save(&k1) == true);
    assert(storage_key_exists(1) == true);
    assert(storage_key_exists(2) == false);

    key_record_t got;
    assert(storage_key_get(1, &got) == true);
    assert(got.is_checksum_valid == true);
    assert(keys_equal(&k1, &got));

    /* get of a nonexistent key fails */
    assert(storage_key_get(99, &got) == false);

    /* --- update existing key ---------------------------------------------- */
    key_record_t k1b = make_key(1, "alice2", false, false, 1700000123u, 0x20);
    assert(storage_key_save(&k1b) == true);
    assert(storage_key_get(1, &got) == true);
    assert(keys_equal(&k1b, &got));

    /* --- multiple keys + list --------------------------------------------- */
    key_record_t k2 = make_key(2, "bob", true, false, 1700000200u, 0x30);
    key_record_t k3 = make_key(3, "carol", false, true, 1700000300u, 0x40);
    assert(storage_key_save(&k2) == true);
    assert(storage_key_save(&k3) == true);

    key_record_t list[16];
    int          n = storage_key_list(list, 16);
    assert(n == 3);

    /* list truncation: max_count clamps to KEY_MAX_COUNT and to what fits */
    int n1 = storage_key_list(list, 1);
    assert(n1 == 1);
    int nbig = storage_key_list(list, KEY_MAX_COUNT + 100);
    assert(nbig == 3);

    /* --- delete + get-after-delete ---------------------------------------- */
    assert(storage_key_delete(2) == true);
    assert(storage_key_exists(2) == false);
    assert(storage_key_get(2, &got) == false);
    assert(storage_key_delete(2) == false); /* already gone */
    assert(storage_key_list(list, 16) == 2);

    /* --- wifi set/get/clear ----------------------------------------------- */
    wifi_config_t wcfg = {0};
    snprintf(wcfg.ssid, sizeof wcfg.ssid, "hackerspace");
    snprintf(wcfg.password, sizeof wcfg.password, "s3cr3t-pass");
    assert(storage_wifi_set(&wcfg) == true);

    wifi_config_t wgot = {0};
    assert(storage_wifi_get(&wgot) == true);
    assert(strcmp(wgot.ssid, wcfg.ssid) == 0);
    assert(strcmp(wgot.password, wcfg.password) == 0);

    assert(storage_wifi_clear() == true);
    assert(storage_wifi_get(&wgot) == false); /* gone */
    assert(storage_wifi_clear() == false);    /* already gone */

    /* --- backup export -> import roundtrip -------------------------------- */
    /* Current store holds keys id=1 (updated) and id=3. */
    static uint8_t backup[sizeof(backup_header_t) + BACKUP_MAX_KEYS * sizeof(backup_key_t)];
    int            blen = backup_export(backup, sizeof backup);
    assert(blen > 0);
    assert((size_t)blen == sizeof(backup_header_t) + 2 * sizeof(backup_key_t));

    /* buffer too small -> -1 */
    assert(backup_export(backup, sizeof(backup_header_t)) == -1);

    /* Snapshot expected keys, then wipe the store. */
    key_record_t expect[8];
    int          expect_n = storage_key_list(expect, 8);
    assert(expect_n == 2);
    for (int i = 0; i < expect_n; i++)
        assert(storage_key_delete(expect[i].id) == true);
    assert(storage_key_list(list, 16) == 0);

    /* Import restores them. */
    assert(backup_import(backup, (size_t)blen) == true);
    int restored_n = storage_key_list(list, 16);
    assert(restored_n == expect_n);
    for (int i = 0; i < expect_n; i++) {
        key_record_t r;
        assert(storage_key_get(expect[i].id, &r) == true);
        assert(keys_equal(&expect[i], &r));
    }

    /* --- corrupt / invalid backups all rejected --------------------------- */
    /* too small */
    assert(backup_import(backup, sizeof(backup_header_t) - 1) == false);

    /* bad magic */
    {
        uint8_t bad[sizeof backup];
        memcpy(bad, backup, (size_t)blen);
        backup_header_t *h = (backup_header_t *)bad;
        h->magic           = 0xDEADBEEFu;
        assert(backup_import(bad, (size_t)blen) == false);
    }
    /* bad version */
    {
        uint8_t bad[sizeof backup];
        memcpy(bad, backup, (size_t)blen);
        backup_header_t *h = (backup_header_t *)bad;
        h->version         = BACKUP_VERSION + 1;
        assert(backup_import(bad, (size_t)blen) == false);
    }
    /* key_count too large */
    {
        uint8_t bad[sizeof backup];
        memcpy(bad, backup, (size_t)blen);
        backup_header_t *h = (backup_header_t *)bad;
        h->key_count       = KEY_MAX_COUNT + 1;
        assert(backup_import(bad, (size_t)blen) == false);
    }
    /* truncated body (header claims more keys than bytes provided) */
    {
        uint8_t bad[sizeof backup];
        memcpy(bad, backup, (size_t)blen);
        backup_header_t *h = (backup_header_t *)bad;
        h->key_count       = 200; /* body far shorter than 200 records */
        assert(backup_import(bad, (size_t)blen) == false);
    }
    /* checksum mismatch (flip a payload byte, keep header intact) */
    {
        uint8_t bad[sizeof backup];
        memcpy(bad, backup, (size_t)blen);
        bad[sizeof(backup_header_t)] ^= 0xFF;
        assert(backup_import(bad, (size_t)blen) == false);
    }

    /* The store still holds the good import after all rejected attempts. */
    assert(storage_key_list(list, 16) == expect_n);

    /* --- empty backup (no keys) roundtrips -------------------------------- */
    for (int i = 0; i < expect_n; i++)
        assert(storage_key_delete(expect[i].id) == true);
    assert(storage_key_list(list, 16) == 0);
    int elen = backup_export(backup, sizeof backup);
    assert((size_t)elen == sizeof(backup_header_t));
    assert(backup_import(backup, (size_t)elen) == true);
    assert(storage_key_list(list, 16) == 0);

    printf("storage OK\n");
    return 0;
}
