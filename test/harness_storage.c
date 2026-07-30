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
#include "lfs_util.h" /* lfs_crc: matches backup.c's whole-backup checksum */
#include "storage.h"

/* Must match storage.c's private layout constants. */
#define STORAGE_SIZE_BYTES   (256 * 1024)
#define STORAGE_FLASH_OFFSET (PICO_FLASH_SIZE_BYTES - STORAGE_SIZE_BYTES)
#define STORAGE_WINDOW_BASE  ((uintptr_t)(XIP_BASE + STORAGE_FLASH_OFFSET))

/* Absolute host pointer for a flash-base-relative offset. */
static uint8_t *flash_ptr(uint32_t flash_offs) {
    return (uint8_t *)(uintptr_t)(XIP_BASE + flash_offs);
}

/* --- flash-write failure injection ----------------------------------------
 * When armed, any flash program whose payload contains g_fail_marker fails, as
 * a NOR write error or a power loss mid-program would: the byte is not written
 * and flash_safe_execute reports the error up, so the littlefs commit fails.
 * Used to prove that an import which fails while writing a new record leaves the
 * ORIGINAL keys intact. */
static bool    g_fail_armed = false;
static uint8_t g_fail_marker[8];
static bool    g_prog_error = false;

/* --- RAM-backed flash primitives storage.c drives ------------------------- */

/* M13 witness: while a delete is in progress, record the longest run of zero
 * bytes seen in any single program buffer. storage_key_delete's zero-overwrite
 * commits the record's inline data as zeros, so a run >= KEY_SECRET_LEN proves
 * the secret field was scrubbed before removal; a plain lfs_remove (no fix)
 * only programs a small delete tag and never produces such a run. */
static volatile int    g_delete_active    = 0;
static volatile size_t g_del_max_zero_run = 0;

void flash_range_program(uint32_t flash_offs, const uint8_t *data, size_t count) {
    if (g_fail_armed && count >= sizeof g_fail_marker) {
        for (size_t i = 0; i + sizeof g_fail_marker <= count; i++) {
            if (memcmp(data + i, g_fail_marker, sizeof g_fail_marker) == 0) {
                g_prog_error = true; /* skip the write and fail the op */
                return;
            }
        }
    }
    uint8_t *dst = flash_ptr(flash_offs);
    if (g_delete_active) {
        size_t run = 0;
        for (size_t i = 0; i < count; i++) {
            run = (data[i] == 0) ? run + 1 : 0;
            if (run > g_del_max_zero_run)
                g_del_max_zero_run = run;
        }
    }
    for (size_t i = 0; i < count; i++)
        dst[i] &= data[i]; /* NOR: programming can only clear bits */
}

void flash_range_erase(uint32_t flash_offs, size_t count) {
    memset(flash_ptr(flash_offs), 0xFF, count); /* erased NOR reads as 0xFF */
}

int flash_safe_execute(void (*func)(void *), void *param, uint32_t timeout_ms) {
    (void)timeout_ms;
    g_prog_error = false;
    func(param);
    if (g_prog_error) {
        g_prog_error = false;
        return PICO_ERROR_TIMEOUT; /* != PICO_OK -> storage.c returns LFS_ERR_IO */
    }
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

/* Scan the whole storage window for `pat` — used to prove a secret is NOT
 * sitting in cleartext anywhere in flash. */
static bool window_contains(const uint8_t *pat, size_t n) {
    const uint8_t *w = flash_ptr(STORAGE_FLASH_OFFSET);
    for (size_t i = 0; i + n <= STORAGE_SIZE_BYTES; i++)
        if (memcmp(w + i, pat, n) == 0)
            return true;
    return false;
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

    /* --- init on fresh 0xFF flash: mount fails; explicit format recovers --- */
    // storage_init() no longer auto-formats a corrupt/blank device (recovery is
    // now operator-driven via the "format-storage" console command). On fresh
    // 0xFF flash the mount fails and init reports failure; storage_format() is
    // the explicit recovery path that formats and re-mounts.
    flash_ram_map();
    assert(storage_is_mounted() == false);
    assert(storage_init() == false);
    assert(storage_is_mounted() == false);
    assert(storage_format() == true);
    assert(storage_is_mounted() == true);

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
    key_record_t k3 = make_key(3, "carol", true, true, 1700000300u, 0x40);
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

    /* --- out-of-range id rejected (L7) ------------------------------------ */
    /* storage_key_save must refuse ids above KEY_ID_MAX so an unmanageable
     * (get/delete/unset-admin gate on id > KEY_ID_MAX) key can never persist. */
    key_record_t kbad = make_key(KEY_ID_MAX + 1, "evil", true, true, 1700000400u, 0x50);
    assert(storage_key_save(&kbad) == false);
    assert(storage_key_exists(KEY_ID_MAX + 1) == false);
    assert(storage_key_list(list, 16) == 3); /* still only the 3 valid keys */

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

    /* L8: storage_wifi_get must NUL-terminate both fields on read, even when the
     * flash bytes carry no terminator. Plant a config whose ssid/password fill
     * the whole field with non-NUL bytes (storage_wifi_set is a raw writer that
     * does NOT zero-pad, unlike cmd_set_wifi), then get: the last byte of each
     * field must be forced to '\0' so downstream printf("%s")/connect can't
     * over-read the password into the SSID. */
    wifi_config_t wfull;
    memset(wfull.ssid, 'S', sizeof wfull.ssid);
    memset(wfull.password, 'P', sizeof wfull.password);
    assert(storage_wifi_set(&wfull) == true);
    wifi_config_t wterm;
    memset(&wterm, 'X', sizeof wterm); /* poison so we see the terminator set */
    assert(storage_wifi_get(&wterm) == true);
    assert(wterm.ssid[WIFI_SSID_MAX - 1] == '\0');
    assert(wterm.password[WIFI_PASSWORD_MAX - 1] == '\0');
    assert(strlen(wterm.ssid) == WIFI_SSID_MAX - 1);         /* no over-read */
    assert(strlen(wterm.password) == WIFI_PASSWORD_MAX - 1); /* no over-read */

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

    /* --- M3: empty / admin-less imports rejected before wiping keys ------- */
    /* Regression for the fail-open wipe. A key_count==0 blob (backup_checksum
     * over zero records is 0xFFFFFFFF, so it once cleared every gate), and a
     * well-formed checksum-valid blob carrying no enabled+admin record, both
     * let the unconditional delete loop strand the device with no admin key
     * (any_admin == false -> cmd_login hands out admin to any credentials).
     * Both must now be refused BEFORE any key is touched; a blob with >= 1
     * enabled admin still imports. */
    assert(storage_key_list(list, 16) == expect_n); /* keys 1 & 3 present */

    /* (a) zero-key blob: rejected, existing keys untouched. */
    {
        uint8_t         empty[sizeof(backup_header_t)];
        backup_header_t eh = {.magic     = BACKUP_MAGIC,
                              .version   = BACKUP_VERSION,
                              .key_count = 0,
                              .checksum  = 0xFFFFFFFFu}; /* lfs_crc(~0,x,0)==~0 */
        memcpy(empty, &eh, sizeof eh);
        assert(backup_import(empty, sizeof empty) == false);
        assert(storage_key_list(list, 16) == expect_n);
    }

    /* (b) one enabled-but-non-admin key: rejected before the delete loop. */
    {
        static uint8_t blob[sizeof(backup_header_t) + sizeof(backup_key_t)];
        backup_key_t  *rec = (backup_key_t *)(blob + sizeof(backup_header_t));
        backup_key_t   bk  = {0};
        bk.id              = 50;
        snprintf(bk.name, sizeof bk.name, "user");
        memset(bk.secret, 0x11, sizeof bk.secret);
        bk.is_enabled = true;
        bk.is_admin   = false;
        bk.created_at = 1700009000u;
        memcpy(rec, &bk, sizeof bk);
        backup_header_t bh = {.magic     = BACKUP_MAGIC,
                              .version   = BACKUP_VERSION,
                              .key_count = 1,
                              .checksum  = lfs_crc(0xFFFFFFFF, rec, sizeof(backup_key_t))};
        memcpy(blob, &bh, sizeof bh);
        assert(backup_import(blob, sizeof blob) == false);
        assert(storage_key_list(list, 16) == expect_n); /* untouched */

        /* (c) same blob, now enabled + admin -> import succeeds, replaces store. */
        rec->is_admin = true;
        bh.checksum   = lfs_crc(0xFFFFFFFF, rec, sizeof(backup_key_t));
        memcpy(blob, &bh, sizeof bh);
        assert(backup_import(blob, sizeof blob) == true);
        assert(storage_key_list(list, 16) == 1);
        key_record_t g;
        assert(storage_key_get(50, &g) == true);
        assert(g.is_admin == true && g.is_enabled == true);
        assert(storage_key_delete(50) == true);
    }
    assert(storage_key_list(list, 16) == 0); /* store empty for the UBSan block */

    /* --- UBSan regression: non-bool flag byte in an otherwise-valid blob --- */
    /* to_key_record() must not load is_enabled/is_admin straight into a `bool`:
     * a crafted import blob can carry any byte there, and loading a bool whose
     * object representation is not 0/1 is undefined behaviour (caught by
     * -fsanitize=undefined). Build a valid 1-key blob, poke a non-bool flag
     * byte, refresh the header CRC so the blob still validates, and import it.
     * The import must succeed and the flag must read back canonicalised. */
    {
        key_record_t seed = make_key(9, "ub", true, false, 1234, 0x40);
        assert(storage_key_save(&seed) == true);
        uint8_t craft[sizeof backup];
        int     clen = backup_export(craft, sizeof craft);
        assert((size_t)clen == sizeof(backup_header_t) + sizeof(backup_key_t));
        assert(storage_key_delete(9) == true);

        backup_key_t *bk          = (backup_key_t *)(craft + sizeof(backup_header_t));
        *(uint8_t *)&bk->is_admin = 67; /* non-bool byte in an otherwise-valid record */
        backup_header_t *bh       = (backup_header_t *)craft;
        bh->checksum              = lfs_crc(0xFFFFFFFF, bk, sizeof(backup_key_t));

        assert(backup_import(craft, (size_t)clen) == true);
        key_record_t g;
        assert(storage_key_get(9, &g) == true);
        assert(g.is_admin == true);   /* canonicalised: exactly 1, not 67 */
        assert(g.is_enabled == true); /* untouched flag survives */
        assert(storage_key_delete(9) == true);
    }

    /* --- M13: delete-key zero-overwrites the record before unlinking ------- */
    /* storage_key_delete must overwrite the record's bytes with zeros and
     * lfs_file_sync BEFORE lfs_remove, so the NEWEST inline value of the revoked
     * key is zeros, not its secret.
     *
     * Note on what CANNOT be asserted: littlefs is an append-only log. The
     * zero-overwrite commits a NEW inline copy; the ORIGINAL secret bytes stay
     * physically in the metadata block pair until it is compacted and the stale
     * sector erased — so a raw window scan still finds the plaintext seed right
     * after delete (verified: window_contains stays true). Only storage_format()
     * guarantees erasure. Asserting the seed is physically gone would therefore
     * be false; we instead witness the fix directly: during the delete, the
     * flash-program shim records the longest zero run it programs, and the
     * zero-overwrite of the KEY_SECRET_LEN-byte secret field (in fact the whole
     * record) produces a run >= KEY_SECRET_LEN. A plain lfs_remove (no fix) only
     * programs a small delete tag and never does (measured: 2 bytes vs 48). */
    {
        uint8_t del_seed[KEY_SECRET_LEN];
        for (int i = 0; i < KEY_SECRET_LEN; i++)
            del_seed[i] = (uint8_t)(0x3C ^ (i * 5 + 1));
        key_record_t vk = make_key(42, "victim", true, false, 1700111111u, 0);
        memcpy(vk.secret, del_seed, sizeof del_seed);
        assert(storage_key_save(&vk) == true);
        assert(window_contains(del_seed, sizeof del_seed) == true); /* seed on flash */

        g_del_max_zero_run = 0;
        g_delete_active    = 1;
        assert(storage_key_delete(42) == true);
        g_delete_active = 0;

        /* the record's inline data was scrubbed to zeros before removal */
        assert(g_del_max_zero_run >= (size_t)KEY_SECRET_LEN);
        /* the current filesystem value is gone (remove committed) */
        assert(storage_key_exists(42) == false);
        assert(storage_key_get(42, &tmp) == false);
    }

    /* --- M6: an import that fails mid-write must NOT wipe the store --------- */
    /* Old backup_import deleted every key BEFORE writing the new set and bailed
     * out on the first storage_key_save failure, leaving storage empty/partial
     * with no rollback. The fix stages the new records first and only removes
     * the old keys once all staged writes succeed. Prove it: seed a known set,
     * arm the flash shim to fail while a new record is being written, import a
     * VALID replacement blob, and assert the ORIGINAL keys are still there. */
    {
        /* start from a clean store, then seed three known originals */
        {
            key_record_t cur[16];
            int          cn = storage_key_list(cur, 16);
            for (int i = 0; i < cn; i++)
                assert(storage_key_delete(cur[i].id) == true);
        }
        key_record_t o1 = make_key(1, "orig-a", true, true, 1700001000u, 0x11);
        key_record_t o2 = make_key(2, "orig-b", true, false, 1700002000u, 0x22);
        key_record_t o3 = make_key(3, "orig-c", false, false, 1700003000u, 0x33);
        assert(storage_key_save(&o1) == true);
        assert(storage_key_save(&o2) == true);
        assert(storage_key_save(&o3) == true);
        assert(storage_key_list(list, 16) == 3);

        /* Build a VALID import blob for a DISJOINT new set {50,51,52}. Each new
         * record's secret starts at a distinct seed, so the flash shim can fail
         * exactly on the write of one specific new record. */
        static uint8_t imp[sizeof(backup_header_t) + 3 * sizeof(backup_key_t)];
        backup_key_t  *bks = (backup_key_t *)(imp + sizeof(backup_header_t));
        memset(imp, 0, sizeof imp);
        const struct {
            uint16_t id;
            uint8_t  seed;
            bool     admin;
        } spec[3] = {{50, 0x77, true}, {51, 0x88, false}, {52, 0x99, false}};
        for (int i = 0; i < 3; i++) {
            bks[i].id         = spec[i].id;
            bks[i].is_enabled = true;
            bks[i].is_admin   = spec[i].admin;
            bks[i].created_at = 1700010000u + spec[i].id;
            snprintf(bks[i].name, sizeof bks[i].name, "new-%u", spec[i].id);
            for (int j = 0; j < KEY_SECRET_LEN; j++)
                bks[i].secret[j] = (uint8_t)(spec[i].seed + j);
        }
        backup_header_t ih = {
            .magic     = BACKUP_MAGIC,
            .version   = BACKUP_VERSION,
            .key_count = 3,
            .checksum  = lfs_crc(0xFFFFFFFF, bks, 3 * sizeof(backup_key_t)),
        };
        memcpy(imp, &ih, sizeof ih);

        /* Arm the shim to fail while the SECOND new record (id 51, seed 0x88) is
         * written - i.e. mid-import, after some progress. With the old code this
         * point is reached only AFTER every original key was already deleted. */
        for (int j = 0; j < (int)sizeof g_fail_marker; j++)
            g_fail_marker[j] = (uint8_t)(0x88 + j);
        g_fail_armed = true;
        assert(backup_import(imp, sizeof imp) == false);
        g_fail_armed = false;

        /* The ORIGINAL keys must all be intact and unchanged... */
        key_record_t r;
        assert(storage_key_get(1, &r) == true && keys_equal(&o1, &r));
        assert(storage_key_get(2, &r) == true && keys_equal(&o2, &r));
        assert(storage_key_get(3, &r) == true && keys_equal(&o3, &r));
        assert(storage_key_list(list, 16) == 3);
        /* ...and NONE of the new keys leaked in. */
        assert(storage_key_exists(50) == false);
        assert(storage_key_exists(51) == false);
        assert(storage_key_exists(52) == false);

        /* Positive control: with the shim disarmed the same blob imports cleanly
         * and fully swaps the set (old keys pruned, new keys in place). */
        assert(backup_import(imp, sizeof imp) == true);
        assert(storage_key_list(list, 16) == 3);
        assert(storage_key_exists(50) == true);
        assert(storage_key_exists(51) == true);
        assert(storage_key_exists(52) == true);
        assert(storage_key_exists(1) == false);
        assert(storage_key_exists(2) == false);
        assert(storage_key_exists(3) == false);
        assert(storage_key_get(51, &r) == true);
        assert(r.is_enabled == true && r.is_admin == false);
    }

    printf("storage OK\n");
    return 0;
}
