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
#include "lfs.h"            /* second lfs mount, used to plant a legacy plaintext record */
#include "lfs_util.h"       /* lfs_crc: matches backup.c's whole-backup checksum */
#include "pico/rand.h"      /* get_rand_64: IV source for storage.c's GCM (H7) */
#include "pico/unique_id.h" /* board id: storage.c's device-bound KEK (H7) */
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

/* M13 witness: while a delete is in progress, record the longest run of zero
 * bytes seen in any single program buffer. storage_key_delete's zero-overwrite
 * commits the record's inline data as zeros, so a run >= KEY_SECRET_LEN proves
 * the secret field was scrubbed before removal; a plain lfs_remove (no fix)
 * only programs a small delete tag and never produces such a run. */
static volatile int    g_delete_active    = 0;
static volatile size_t g_del_max_zero_run = 0;

void flash_range_program(uint32_t flash_offs, const uint8_t *data, size_t count) {
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

/* --- device doubles storage.c's at-rest crypto (H7) needs ----------------- */

/* GCM IV source. storage.c draws a fresh 96-bit IV per write via get_rand_64;
 * a monotonic counter gives distinct, reproducible IVs. */
uint64_t get_rand_64(void) {
    static uint64_t ctr = 0x0123456789ABCDEFull;
    ctr += 0x9E3779B97F4A7C15ull;
    return ctr;
}

/* Per-board unique id feeding storage.c's KEK. Mutable so a test can simulate a
 * different device (rotated KEK) and confirm an encrypted record no longer
 * decrypts. */
static uint8_t g_board_id[PICO_UNIQUE_BOARD_ID_SIZE_BYTES] = {0xA0, 0xA1, 0xA2, 0xA3,
                                                              0xA4, 0xA5, 0xA6, 0xA7};

void pico_get_unique_board_id(pico_unique_board_id_t *id_out) {
    memcpy(id_out->id, g_board_id, PICO_UNIQUE_BOARD_ID_SIZE_BYTES);
}

/* --- second littlefs mount: plant a legacy plaintext key record ------------ */
/* storage.c writes encrypted records now, so to exercise the legacy-plaintext
 * migration/passthrough path we format the same RAM window with an independent
 * lfs mount and write one pre-H7 record by hand, then let storage.c mount it. */

/* Must match storage.c's PRIVATE key_record_stored_t byte-for-byte. */
typedef struct {
    uint16_t id;
    char     name[KEY_NAME_MAX];
    uint8_t  secret[KEY_SECRET_LEN];
    uint8_t  is_enabled;
    uint8_t  is_admin;
    uint32_t created_at;
    uint32_t checksum;
} legacy_stored_t;

/* Must match storage.c's PRIVATE key_checksum() field-by-field. */
static uint32_t legacy_checksum(const legacy_stored_t *k) {
    uint32_t crc = 0xFFFFFFFF;
    crc          = lfs_crc(crc, &k->id, sizeof(k->id));
    crc          = lfs_crc(crc, k->name, sizeof(k->name));
    crc          = lfs_crc(crc, k->secret, sizeof(k->secret));
    crc          = lfs_crc(crc, &k->is_enabled, sizeof(k->is_enabled));
    crc          = lfs_crc(crc, &k->is_admin, sizeof(k->is_admin));
    crc          = lfs_crc(crc, &k->created_at, sizeof(k->created_at));
    return crc;
}

/* littlefs block-device callbacks over the SAME RAM window storage.c uses. */
static int h2_read(const struct lfs_config *c, lfs_block_t b, lfs_off_t o, void *buf,
                   lfs_size_t sz) {
    (void)c;
    memcpy(buf, flash_ptr(STORAGE_FLASH_OFFSET + b * FLASH_SECTOR_SIZE + o), sz);
    return 0;
}
static int h2_prog(const struct lfs_config *c, lfs_block_t b, lfs_off_t o, const void *buf,
                   lfs_size_t sz) {
    (void)c;
    flash_range_program(STORAGE_FLASH_OFFSET + b * FLASH_SECTOR_SIZE + o, buf, sz);
    return 0;
}
static int h2_erase(const struct lfs_config *c, lfs_block_t b) {
    (void)c;
    flash_range_erase(STORAGE_FLASH_OFFSET + b * FLASH_SECTOR_SIZE, FLASH_SECTOR_SIZE);
    return 0;
}
static int h2_sync(const struct lfs_config *c) {
    (void)c;
    return 0;
}

/* Format the window and write ONE legacy plaintext record at /keys/<id>. */
static void plant_legacy_key(const legacy_stored_t *rec) {
    static uint8_t    rbuf[256], pbuf[256], lbuf[8], fbuf[256];
    struct lfs_config cfg = {
        .read             = h2_read,
        .prog             = h2_prog,
        .erase            = h2_erase,
        .sync             = h2_sync,
        .read_size        = 256,
        .prog_size        = 256,
        .block_size       = FLASH_SECTOR_SIZE,
        .block_count      = STORAGE_SIZE_BYTES / FLASH_SECTOR_SIZE,
        .cache_size       = 256,
        .lookahead_size   = sizeof(lbuf),
        .block_cycles     = 500,
        .read_buffer      = rbuf,
        .prog_buffer      = pbuf,
        .lookahead_buffer = lbuf,
    };
    const struct lfs_file_config fcfg = {.buffer = fbuf};

    lfs_t l;
    assert(lfs_format(&l, &cfg) == 0);
    assert(lfs_mount(&l, &cfg) == 0);
    assert(lfs_mkdir(&l, "/keys") == 0);
    char path[40];
    snprintf(path, sizeof path, "/keys/%05u", rec->id);
    lfs_file_t f;
    assert(lfs_file_opencfg(&l, &f, path, LFS_O_WRONLY | LFS_O_CREAT | LFS_O_TRUNC, &fcfg) == 0);
    assert(lfs_file_write(&l, &f, rec, sizeof(*rec)) == (lfs_ssize_t)sizeof(*rec));
    assert(lfs_file_close(&l, &f) == 0);
    assert(lfs_unmount(&l) == 0);
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
        /* H7 encrypts secrets at rest, so the plaintext seed never lands on
         * flash; the (encrypted) record still exists and must be scrubbed on
         * delete — that scrub is what this test verifies. */
        assert(window_contains(del_seed, sizeof del_seed) == false);
        assert(storage_key_exists(42) == true);

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

    /* --- H7: secrets are encrypted at rest -------------------------------- */
    /* A saved key's seed must NOT appear in cleartext anywhere in the window,
     * yet must decrypt back exactly on this device. Use a distinctive seed
     * pattern so the window scan can't collide with unrelated bytes. */
    {
        uint8_t distinct[KEY_SECRET_LEN];
        for (int i = 0; i < KEY_SECRET_LEN; i++)
            distinct[i] = (uint8_t)(0xC0 ^ (i * 7 + 3));
        assert(window_contains(distinct, sizeof distinct) == false); /* not there yet */

        key_record_t ek = make_key(11, "enc", true, true, 1700009999u, 0);
        memcpy(ek.secret, distinct, sizeof distinct);
        assert(storage_key_save(&ek) == true);

        /* confidentiality: plaintext seed is nowhere in flash */
        assert(window_contains(distinct, sizeof distinct) == false);

        /* round-trip: same device decrypts to the exact seed, tag verifies */
        key_record_t eg;
        assert(storage_key_get(11, &eg) == true);
        assert(eg.is_checksum_valid == true);
        assert(memcmp(eg.secret, distinct, sizeof distinct) == 0);
        assert(keys_equal(&ek, &eg));

        /* wifi password likewise never lands in cleartext, round-trips */
        wifi_config_t ewc = {0};
        snprintf(ewc.ssid, sizeof ewc.ssid, "net");
        snprintf(ewc.password, sizeof ewc.password, "PLAINTEXT-WIFI-PW-MARKER-XYZ");
        assert(storage_wifi_set(&ewc) == true);
        assert(window_contains((const uint8_t *)"PLAINTEXT-WIFI-PW-MARKER-XYZ", 28) == false);
        wifi_config_t ewg = {0};
        assert(storage_wifi_get(&ewg) == true);
        assert(strcmp(ewg.password, ewc.password) == 0);
        assert(strcmp(ewg.ssid, ewc.ssid) == 0);
        assert(storage_wifi_clear() == true);

        /* --- device binding: a rotated/foreign KEK must NOT decrypt -------- */
        g_board_id[0] ^= 0xFF; /* pretend this is a different board */
        key_record_t wrong;
        assert(storage_key_get(11, &wrong) == true); /* record still present */
        assert(wrong.is_checksum_valid == false);    /* tag fails under wrong KEK */
        uint8_t zero[KEY_SECRET_LEN] = {0};
        assert(memcmp(wrong.secret, zero, sizeof zero) == 0); /* seed not surfaced */
        g_board_id[0] ^= 0xFF;                                /* restore true device */
        assert(storage_key_get(11, &eg) == true);
        assert(eg.is_checksum_valid == true); /* decrypts again */
        assert(memcmp(eg.secret, distinct, sizeof distinct) == 0);

        assert(storage_key_delete(11) == true);
    }

    /* --- H7: legacy plaintext record migrates safely ---------------------- */
    /* A device flashed before H7 holds plaintext key_record_stored_t records.
     * storage.c must still read them (decrypt-or-passthrough), and re-encrypt
     * them on the next save. Plant one via an independent lfs mount, then let
     * storage.c mount the same media. */
    {
        flash_ram_map(); /* fresh window; storage re-mounts below */
        legacy_stored_t leg = {0};
        leg.id              = 7;
        leg.is_enabled      = 1;
        leg.is_admin        = 1;
        leg.created_at      = 1699999999u;
        snprintf(leg.name, sizeof leg.name, "legacy");
        for (int i = 0; i < KEY_SECRET_LEN; i++)
            leg.secret[i] = (uint8_t)(0x5A + i);
        leg.checksum = legacy_checksum(&leg);
        plant_legacy_key(&leg);

        assert(storage_init() == true); /* mounts the planted filesystem */

        /* passthrough: legacy record reads back with its plaintext seed */
        key_record_t lg;
        assert(storage_key_get(7, &lg) == true);
        assert(lg.is_checksum_valid == true);
        assert(lg.is_admin == true && lg.is_enabled == true);
        assert(strcmp(lg.name, "legacy") == 0);
        for (int i = 0; i < KEY_SECRET_LEN; i++)
            assert(lg.secret[i] == (uint8_t)(0x5A + i));

        /* the on-flash legacy record IS plaintext (that is the risk H7 closes),
         * and being plaintext it decrypts independent of the KEK — flip the
         * board id and it STILL reads (no crypto binds it). */
        assert(window_contains(leg.secret, KEY_SECRET_LEN) == true);
        g_board_id[0] ^= 0xFF;
        key_record_t lg_wrongdev;
        assert(storage_key_get(7, &lg_wrongdev) == true);
        assert(lg_wrongdev.is_checksum_valid == true); /* CRC, not KEK-bound */
        g_board_id[0] ^= 0xFF;

        /* migrate: re-save upgrades it to the encrypted format. (The stale
         * plaintext copy may linger in flash until a block is reused/formatted
         * — that residue is M13's concern, not H7's; do not assert it gone.) */
        assert(storage_key_save(&lg) == true);
        key_record_t mg;
        assert(storage_key_get(7, &mg) == true);
        assert(mg.is_checksum_valid == true);
        assert(keys_equal(&lg, &mg));

        /* proof of upgrade: the record is NOW KEK-bound, so a foreign device
         * can no longer decrypt it (a still-plaintext record would have). */
        g_board_id[0] ^= 0xFF;
        key_record_t mg_wrongdev;
        assert(storage_key_get(7, &mg_wrongdev) == true);
        assert(mg_wrongdev.is_checksum_valid == false); /* encrypted now */
        g_board_id[0] ^= 0xFF;
    }

    printf("storage OK\n");
    return 0;
}
