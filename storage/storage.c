#include "storage.h"

#include "shared/wipe.h"
#include "lfs.h"
#include "lfs_util.h"
#include "pico/stdlib.h"
#include "pico/flash.h"
#include "pico/rand.h"
#include "pico/unique_id.h"
#include "hardware/flash.h"
#include "hardware/sync.h"

#include "mbedtls/gcm.h"
#include "mbedtls/md.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

// ---------------------------------------------------------------------------
// Flash layout
// ---------------------------------------------------------------------------
// Dedicate the last 256KB of the Pico W's 2MB flash to LittleFS.
// The linker places firmware at the START of flash; storage is at the END,
// so they never collide as long as firmware stays under ~1.75MB.

#define STORAGE_SIZE_BYTES   (256 * 1024)
#define STORAGE_FLASH_OFFSET (PICO_FLASH_SIZE_BYTES - STORAGE_SIZE_BYTES)

// LittleFS block = one flash erase sector
#define LFS_BLOCK_SIZE  FLASH_SECTOR_SIZE                     // 4096
#define LFS_BLOCK_COUNT (STORAGE_SIZE_BYTES / LFS_BLOCK_SIZE) // 64
#define LFS_READ_SIZE   FLASH_PAGE_SIZE                       // 256
#define LFS_PROG_SIZE   FLASH_PAGE_SIZE                       // 256
#define LFS_CACHE_SIZE  FLASH_PAGE_SIZE                       // 256
#define LFS_LOOKAHEAD   64

static uint8_t lfs_file_buf[LFS_CACHE_SIZE];

static const struct lfs_file_config LFS_FILE_CFG = {
    .buffer = lfs_file_buf,
};

// ---------------------------------------------------------------------------
// Models
// ---------------------------------------------------------------------------

typedef struct {
    uint16_t id;
    char     name[KEY_NAME_MAX];
    uint8_t  secret[KEY_SECRET_LEN];
    bool     is_enabled;
    bool     is_admin;
    uint32_t created_at;
    uint32_t checksum;
} key_record_stored_t;

static uint32_t key_checksum(const key_record_stored_t *key) {
    uint32_t crc = 0xFFFFFFFF;
    crc          = lfs_crc(crc, &key->id, sizeof(key->id));
    crc          = lfs_crc(crc, key->name, sizeof(key->name));
    crc          = lfs_crc(crc, key->secret, sizeof(key->secret));
    crc          = lfs_crc(crc, &key->is_enabled, sizeof(key->is_enabled));
    crc          = lfs_crc(crc, &key->is_admin, sizeof(key->is_admin));
    crc          = lfs_crc(crc, &key->created_at, sizeof(key->created_at));
    return crc;
}

// Legacy plaintext record -> logic model. Retained ONLY for reading records
// written by firmware that predates at-rest encryption (H7); new records are
// always written encrypted (key_record_enc_t below) and read via enc_to_key,
// and a legacy record is migrated to the encrypted format the next time the
// key is saved. Fill via an out-pointer rather than returning by value so the
// secret never lives in a transient helper-frame copy that would outlast this
// call.
static void to_record(const key_record_stored_t *s, key_record_t *k) {
    k->id                = s->id;
    k->is_enabled        = s->is_enabled;
    k->is_admin          = s->is_admin;
    k->created_at        = s->created_at;
    k->is_checksum_valid = (s->checksum == key_checksum(s));
    memcpy(k->name, s->name, sizeof(k->name));
    k->name[KEY_NAME_MAX - 1] = '\0';
    memcpy(k->secret, s->secret, sizeof(k->secret));
}

// ---------------------------------------------------------------------------
// At-rest encryption (H7)
// ---------------------------------------------------------------------------
// Secrets (TOTP seeds and the WiFi password) are encrypted at rest under a
// device-bound key-encryption key (KEK): HMAC-SHA256(compile-time secret,
// per-board unique id). AES-256-GCM provides confidentiality plus integrity —
// the 16-byte tag authenticates both the ciphertext and the cleartext metadata
// header (passed as AAD), so it replaces the old CRC for encrypted records.
//
// The RP2040 has no secure boot / flash encryption / readback protection, so
// this only raises the bar from "read the flash" to "read the flash AND the
// firmware" (the compile-time secret lives in the image). It is defence in
// depth, not a root of trust; RP2350 (OTP + secure boot) is the real fix. The
// board id is not secret, so the compile-time secret SHOULD be overridden per
// build via -DHSLOCK_STORAGE_KEK_SECRET=... rather than shipping this default.
#ifndef HSLOCK_STORAGE_KEK_SECRET
#define HSLOCK_STORAGE_KEK_SECRET "hslock-storage-kek-v1-override-at-build-time"
#endif

#define KEK_LEN     32 // AES-256
#define REC_IV_LEN  12 // GCM nonce
#define REC_TAG_LEN 16 // GCM tag

// Derive the 32-byte device-bound KEK. Deterministic per board, so an encrypted
// record round-trips on the same device across reboots.
static void derive_kek(uint8_t kek_out[KEK_LEN]) {
    pico_unique_board_id_t board;
    pico_get_unique_board_id(&board);
    const mbedtls_md_info_t *info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    mbedtls_md_hmac(info, (const uint8_t *)HSLOCK_STORAGE_KEK_SECRET,
                    strlen(HSLOCK_STORAGE_KEK_SECRET), board.id, sizeof(board.id), kek_out);
}

// AES-256-GCM seal: random per-record IV, AAD authenticated but not encrypted.
static bool gcm_seal(const uint8_t *aad, size_t aad_len, const uint8_t *pt, size_t pt_len,
                     uint8_t *iv_out, uint8_t *ct_out, uint8_t *tag_out) {
    // Fresh random 96-bit IV per write — the KEK is device-fixed, so IV reuse
    // under one key would be catastrophic for GCM. Draw from the RP2040 RNG.
    for (size_t i = 0; i < REC_IV_LEN;) {
        uint64_t r     = get_rand_64();
        size_t   chunk = (REC_IV_LEN - i) < sizeof(r) ? (REC_IV_LEN - i) : sizeof(r);
        memcpy(iv_out + i, &r, chunk);
        i += chunk;
    }

    uint8_t kek[KEK_LEN];
    derive_kek(kek);
    mbedtls_gcm_context ctx;
    mbedtls_gcm_init(&ctx);
    int rc = mbedtls_gcm_setkey(&ctx, MBEDTLS_CIPHER_ID_AES, kek, KEK_LEN * 8);
    if (rc == 0)
        rc = mbedtls_gcm_crypt_and_tag(&ctx, MBEDTLS_GCM_ENCRYPT, pt_len, iv_out, REC_IV_LEN, aad,
                                       aad_len, pt, ct_out, REC_TAG_LEN, tag_out);
    mbedtls_gcm_free(&ctx);
    secure_wipe(kek, sizeof(kek));
    return rc == 0;
}

// AES-256-GCM open: verifies the tag over (AAD, ciphertext). Returns false on
// any authentication failure — corruption, tampering, or a wrong/rotated KEK.
static bool gcm_open(const uint8_t *aad, size_t aad_len, const uint8_t *iv, const uint8_t *ct,
                     size_t ct_len, const uint8_t *tag, uint8_t *pt_out) {
    uint8_t kek[KEK_LEN];
    derive_kek(kek);
    mbedtls_gcm_context ctx;
    mbedtls_gcm_init(&ctx);
    int rc = mbedtls_gcm_setkey(&ctx, MBEDTLS_CIPHER_ID_AES, kek, KEK_LEN * 8);
    if (rc == 0)
        rc = mbedtls_gcm_auth_decrypt(&ctx, ct_len, iv, REC_IV_LEN, aad, aad_len, tag, REC_TAG_LEN,
                                      ct, pt_out);
    mbedtls_gcm_free(&ctx);
    secure_wipe(kek, sizeof(kek));
    return rc == 0;
}

// ---------------------------------------------------------------------------
// Encrypted key record (on-flash format v2)
// ---------------------------------------------------------------------------
// The cleartext header (magic..created_at) is stored in the clear AND fed to
// GCM as AAD, so any tampering with it fails the tag. Only `secret` is
// encrypted. sizeof(key_record_enc_t) differs from sizeof(key_record_stored_t),
// which is how a read distinguishes the two formats (see storage_key_get).

#define KEY_REC_MAGIC   0x324B5348u // "HSK2"
#define KEY_REC_VERSION 2

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t id;
    char     name[KEY_NAME_MAX];
    uint8_t  is_enabled;
    uint8_t  is_admin;
    uint32_t created_at;
    uint8_t  iv[REC_IV_LEN];
    uint8_t  tag[REC_TAG_LEN];
    uint8_t  secret_enc[KEY_SECRET_LEN];
} key_record_enc_t;

// Serialise the authenticated metadata into a packed buffer (struct padding is
// never fed to GCM, so seal and open agree byte-for-byte).
static size_t build_key_aad(uint8_t *aad, const key_record_enc_t *e) {
    size_t   o     = 0;
    uint32_t magic = e->magic;
    uint16_t ver   = e->version;
    uint16_t id    = e->id;
    uint32_t cat   = e->created_at;
    memcpy(aad + o, &magic, sizeof(magic));
    o += sizeof(magic);
    memcpy(aad + o, &ver, sizeof(ver));
    o += sizeof(ver);
    memcpy(aad + o, &id, sizeof(id));
    o += sizeof(id);
    memcpy(aad + o, e->name, KEY_NAME_MAX);
    o += KEY_NAME_MAX;
    aad[o++] = e->is_enabled;
    aad[o++] = e->is_admin;
    memcpy(aad + o, &cat, sizeof(cat));
    o += sizeof(cat);
    return o;
}
#define KEY_AAD_LEN (4 + 2 + 2 + KEY_NAME_MAX + 1 + 1 + 4)

static bool key_to_enc(const key_record_t *k, key_record_enc_t *e) {
    memset(e, 0, sizeof(*e));
    e->magic      = KEY_REC_MAGIC;
    e->version    = KEY_REC_VERSION;
    e->id         = k->id;
    e->is_enabled = k->is_enabled ? 1 : 0;
    e->is_admin   = k->is_admin ? 1 : 0;
    e->created_at = k->created_at;
    memcpy(e->name, k->name, sizeof(e->name));

    uint8_t aad[KEY_AAD_LEN];
    size_t  aad_len = build_key_aad(aad, e);
    return gcm_seal(aad, aad_len, k->secret, KEY_SECRET_LEN, e->iv, e->secret_enc, e->tag);
}

static key_record_t enc_to_key(const key_record_enc_t *e) {
    key_record_t k;
    k.id         = e->id;
    k.is_enabled = (e->is_enabled != 0);
    k.is_admin   = (e->is_admin != 0);
    k.created_at = e->created_at;
    memcpy(k.name, e->name, sizeof(k.name));
    k.name[KEY_NAME_MAX - 1] = '\0';

    uint8_t aad[KEY_AAD_LEN];
    size_t  aad_len = build_key_aad(aad, e);
    k.is_checksum_valid =
        gcm_open(aad, aad_len, e->iv, e->secret_enc, KEY_SECRET_LEN, e->tag, k.secret);
    if (!k.is_checksum_valid)
        memset(k.secret, 0, sizeof(k.secret)); // never surface undecryptable bytes
    return k;
}

// ---------------------------------------------------------------------------
// Encrypted WiFi record (on-flash format v2)
// ---------------------------------------------------------------------------
// The whole wifi_config_t (ssid + password) is encrypted. sizeof differs from
// sizeof(wifi_config_t), the legacy plaintext size, so a read can tell them
// apart and migrate.

#define WIFI_REC_MAGIC   0x32575348u // "HSW2"
#define WIFI_REC_VERSION 2

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t _reserved;
    uint8_t  iv[REC_IV_LEN];
    uint8_t  tag[REC_TAG_LEN];
    uint8_t  ct[sizeof(wifi_config_t)];
} wifi_record_enc_t;

static size_t build_wifi_aad(uint8_t *aad, const wifi_record_enc_t *w) {
    size_t   o     = 0;
    uint32_t magic = w->magic;
    uint16_t ver   = w->version;
    memcpy(aad + o, &magic, sizeof(magic));
    o += sizeof(magic);
    memcpy(aad + o, &ver, sizeof(ver));
    o += sizeof(ver);
    return o;
}
#define WIFI_AAD_LEN (4 + 2)

// ---------------------------------------------------------------------------
// Flash block device callbacks
// ---------------------------------------------------------------------------

static int flash_read(const struct lfs_config *c, lfs_block_t block, lfs_off_t off, void *buffer,
                      lfs_size_t size) {
    uint32_t addr = XIP_BASE + STORAGE_FLASH_OFFSET + block * LFS_BLOCK_SIZE + off;
    memcpy(buffer, (const void *)addr, size);
    return LFS_ERR_OK;
}

// Params struct for flash_safe_execute callbacks (can't pass multiple args)
typedef struct {
    uint32_t       offset;
    const uint8_t *data;
    size_t         size;
} prog_params_t;
typedef struct {
    uint32_t offset;
    size_t   size;
} erase_params_t;

// those functions must live in ram so they can be executed while flash is locked
static void __no_inline_not_in_flash_func(do_flash_program)(void *param) {
    prog_params_t *p = (prog_params_t *)param;
    flash_range_program(p->offset, p->data, p->size);
}

static void __no_inline_not_in_flash_func(do_flash_erase)(void *param) {
    erase_params_t *p = (erase_params_t *)param;
    flash_range_erase(p->offset, p->size);
}

static int flash_prog(const struct lfs_config *c, lfs_block_t block, lfs_off_t off,
                      const void *buffer, lfs_size_t size) {
    prog_params_t p = {.offset = STORAGE_FLASH_OFFSET + block * LFS_BLOCK_SIZE + off,
                       .data   = (const uint8_t *)buffer,
                       .size   = size};
    // flash_safe_execute pauses core 1 and disables interrupts while writing
    int rc = flash_safe_execute(do_flash_program, &p, UINT32_MAX);
    return rc == PICO_OK ? LFS_ERR_OK : LFS_ERR_IO;
}

static int flash_erase(const struct lfs_config *c, lfs_block_t block) {
    erase_params_t p  = {.offset = STORAGE_FLASH_OFFSET + block * LFS_BLOCK_SIZE,
                         .size   = LFS_BLOCK_SIZE};
    int            rc = flash_safe_execute(do_flash_erase, &p, UINT32_MAX);
    return rc == PICO_OK ? LFS_ERR_OK : LFS_ERR_IO;
}

static int flash_sync(const struct lfs_config *c) {
    return LFS_ERR_OK; // no write buffer on NOR flash
}

// ---------------------------------------------------------------------------
// LittleFS state
// ---------------------------------------------------------------------------

static uint8_t lfs_read_buf[LFS_CACHE_SIZE];
static uint8_t lfs_prog_buf[LFS_CACHE_SIZE];
static uint8_t lfs_lookahead_buf[LFS_LOOKAHEAD / 8];

static const struct lfs_config LFS_CFG = {
    .read  = flash_read,
    .prog  = flash_prog,
    .erase = flash_erase,
    .sync  = flash_sync,

    .read_size   = LFS_READ_SIZE,
    .prog_size   = LFS_PROG_SIZE,
    .block_size  = LFS_BLOCK_SIZE,
    .block_count = LFS_BLOCK_COUNT,
    .cache_size  = LFS_CACHE_SIZE,
    // lookahead_size is in BYTES (littlefs tracks 8 blocks per byte and memsets
    // this many bytes into lookahead_buffer). It must equal the buffer size, so
    // derive it from the buffer to keep the two in lockstep; LFS_LOOKAHEAD/8 = 8
    // bytes tracks all 64 blocks in one pass.
    .lookahead_size = sizeof(lfs_lookahead_buf),
    .block_cycles   = 500, // wear leveling hint

    .read_buffer      = lfs_read_buf,
    .prog_buffer      = lfs_prog_buf,
    .lookahead_buffer = lfs_lookahead_buf,
};

static lfs_t lfs;
static bool  mounted = false;

// ---------------------------------------------------------------------------
// Directory helpers
// ---------------------------------------------------------------------------

#define DIR_KEYS  "/keys"
#define FILE_WIFI "/wifi"

static bool ensure_dirs(void) {
    struct lfs_info info;
    if (lfs_stat(&lfs, DIR_KEYS, &info) < 0) {
        return lfs_mkdir(&lfs, DIR_KEYS) >= 0;
    }
    return true;
}

static void key_path(uint16_t id, char *out, size_t out_size) {
    snprintf(out, out_size, DIR_KEYS "/%05u", id);
}

// ---------------------------------------------------------------------------
// Init
// ---------------------------------------------------------------------------

bool storage_init(void) {
    int rc = lfs_mount(&lfs, &LFS_CFG);
    if (rc < 0) {
        printf("[storage] mount FAILED (rc=%d) - will not auto-format\r\n", rc);
        return false;
    }

    if (!ensure_dirs()) {
        printf("[storage] ensure_dirs failed\r\n");
        return false;
    }

    mounted = true;
    printf("[storage] init ok\r\n");
    return true;
}

bool storage_is_mounted(void) {
    return mounted;
}

bool storage_format(void) {
    mounted = false;
    int rc  = lfs_format(&lfs, &LFS_CFG);
    if (rc < 0) {
        printf("[storage] format failed (rc=%d)\r\n", rc);
        return false;
    }
    printf("[storage] format ok\r\n");
    return storage_init();
}

// ---------------------------------------------------------------------------
// WiFi
// ---------------------------------------------------------------------------

bool storage_wifi_get(wifi_config_t *out) {
    if (!mounted)
        return false;

    lfs_file_t f;
    if (lfs_file_opencfg(&lfs, &f, FILE_WIFI, LFS_O_RDONLY, &LFS_FILE_CFG) < 0)
        return false;

    union {
        wifi_record_enc_t enc;
        wifi_config_t     legacy;
        uint8_t           raw[sizeof(wifi_record_enc_t)];
    } buf;
    lfs_ssize_t n = lfs_file_read(&lfs, &f, &buf, sizeof(buf));
    lfs_file_close(&lfs, &f);

    // Defense-in-depth: never trust flash to be NUL-terminated. Both fields are
    // used directly as C strings (printf("%s"), cyw43 connect), so force a
    // terminator at the last byte to prevent an over-read past the field.
    if (n == (lfs_ssize_t)sizeof(wifi_record_enc_t) && buf.enc.magic == WIFI_REC_MAGIC &&
        buf.enc.version == WIFI_REC_VERSION) {
        uint8_t aad[WIFI_AAD_LEN];
        size_t  aad_len = build_wifi_aad(aad, &buf.enc);
        bool ok = gcm_open(aad, aad_len, buf.enc.iv, buf.enc.ct, sizeof(wifi_config_t), buf.enc.tag,
                           (uint8_t *)out);
        if (!ok) {
            printf("[storage] wifi decrypt failed (tamper or wrong device)\r\n");
            secure_wipe(out, sizeof(*out));
            return false;
        }
        out->ssid[WIFI_SSID_MAX - 1]         = '\0';
        out->password[WIFI_PASSWORD_MAX - 1] = '\0';
        return true;
    }

    // Legacy plaintext record: pass it through (migrated to encrypted on the
    // next storage_wifi_set).
    if (n == (lfs_ssize_t)sizeof(wifi_config_t)) {
        memcpy(out, &buf.legacy, sizeof(wifi_config_t));
        out->ssid[WIFI_SSID_MAX - 1]         = '\0';
        out->password[WIFI_PASSWORD_MAX - 1] = '\0';
        return true;
    }

    return false;
}

bool storage_wifi_set(const wifi_config_t *cfg) {
    if (!mounted)
        return false;

    wifi_record_enc_t rec;
    memset(&rec, 0, sizeof(rec));
    rec.magic   = WIFI_REC_MAGIC;
    rec.version = WIFI_REC_VERSION;
    uint8_t aad[WIFI_AAD_LEN];
    size_t  aad_len = build_wifi_aad(aad, &rec);
    if (!gcm_seal(aad, aad_len, (const uint8_t *)cfg, sizeof(wifi_config_t), rec.iv, rec.ct,
                  rec.tag))
        return false;

    lfs_file_t f;
    int        flags = LFS_O_WRONLY | LFS_O_CREAT | LFS_O_TRUNC;
    int        rc    = lfs_file_opencfg(&lfs, &f, FILE_WIFI, flags, &LFS_FILE_CFG);
    if (rc < 0)
        return false;

    lfs_ssize_t n = lfs_file_write(&lfs, &f, &rec, sizeof(rec));
    lfs_file_close(&lfs, &f);
    return n == (lfs_ssize_t)sizeof(rec);
}

bool storage_wifi_clear(void) {
    if (!mounted)
        return false;
    return lfs_remove(&lfs, FILE_WIFI) >= 0;
}

// ---------------------------------------------------------------------------
// Key CRUD
// ---------------------------------------------------------------------------

bool storage_key_exists(uint16_t id) {
    if (!mounted)
        return false;
    char path[40];
    key_path(id, path, sizeof(path));
    struct lfs_info info;
    return lfs_stat(&lfs, path, &info) >= 0;
}

bool storage_key_get(uint16_t id, key_record_t *out) {
    if (!mounted)
        return false;
    char path[40];
    key_path(id, path, sizeof(path));

    lfs_file_t f;
    if (lfs_file_opencfg(&lfs, &f, path, LFS_O_RDONLY, &LFS_FILE_CFG) < 0)
        return false;

    union {
        key_record_enc_t    enc;
        key_record_stored_t legacy;
        uint8_t             raw[sizeof(key_record_enc_t)];
    } buf;
    lfs_ssize_t n = lfs_file_read(&lfs, &f, &buf, sizeof(buf));
    lfs_file_close(&lfs, &f);

    if (n == (lfs_ssize_t)sizeof(key_record_enc_t) && buf.enc.magic == KEY_REC_MAGIC &&
        buf.enc.version == KEY_REC_VERSION) {
        *out = enc_to_key(&buf.enc);
    } else if (n == (lfs_ssize_t)sizeof(key_record_stored_t)) {
        // Legacy plaintext record (pre-H7). Read it so the key keeps working;
        // it is migrated to the encrypted format the next time it is saved.
        to_record(&buf.legacy, out);
    } else {
        secure_wipe(&buf, sizeof(buf));
        return false;
    }
    // The raw record (secret / ciphertext included) is no longer needed: scrub
    // it so nothing lingers on the stack after this read.
    secure_wipe(&buf, sizeof(buf));

    if (!out->is_checksum_valid)
        printf("[storage] key %u checksum mismatch\r\n", id);

    return true;
}

bool storage_key_save(const key_record_t *key) {
    if (!mounted)
        return false;
    if (key->id > KEY_ID_MAX)
        return false;
    char path[40];
    key_path(key->id, path, sizeof(path));

    key_record_enc_t enc; // AES-256-GCM seal computed here (H7)
    if (!key_to_enc(key, &enc))
        return false;

    lfs_file_t f;
    int        flags = LFS_O_WRONLY | LFS_O_CREAT | LFS_O_TRUNC;
    if (lfs_file_opencfg(&lfs, &f, path, flags, &LFS_FILE_CFG) < 0) {
        secure_wipe(&enc, sizeof(enc));
        return false;
    }

    lfs_ssize_t n = lfs_file_write(&lfs, &f, &enc, sizeof(enc));
    lfs_file_close(&lfs, &f);
    bool ok = n == (lfs_ssize_t)sizeof(enc);
    // Scrub the serialised record from the stack.
    secure_wipe(&enc, sizeof(enc));
    return ok;
}

bool storage_key_delete(uint16_t id) {
    if (!mounted)
        return false;
    char path[40];
    key_path(id, path, sizeof(path));

    // Secure-erase hygiene (M13): overwrite the record's bytes with zeros and
    // sync BEFORE removing it, so the NEWEST inline value in the append-only
    // /keys metadata log is zeros rather than the secret. Without this,
    // lfs_remove only appends a delete tag and the last-written record — still
    // holding the plaintext secret — stays the freshest inline copy of that
    // file until the block pair is compacted and the stale sector erased.
    //
    // This does NOT scrub the stale copies already appended to the log by prior
    // writes; only storage_format() guarantees erasure (see docs/COMMANDS.md).
    // It does ensure a revoked key's current on-flash value is no longer its
    // secret, which is correct revocation hygiene. Best-effort: if the overwrite
    // fails we still fall through to lfs_remove so the key is at least unlinked.
    lfs_file_t f;
    if (lfs_file_opencfg(&lfs, &f, path, LFS_O_WRONLY, &LFS_FILE_CFG) >= 0) {
        lfs_soff_t size = lfs_file_size(&lfs, &f);
        if (size > 0) {
            // A record never exceeds sizeof(key_record_stored_t) (the on-flash
            // layout); loop anyway so an unexpected size still clears fully.
            uint8_t    zeros[sizeof(key_record_stored_t)] = {0};
            lfs_soff_t remaining                          = size;
            while (remaining > 0) {
                lfs_size_t chunk = remaining > (lfs_soff_t)sizeof(zeros) ? (lfs_size_t)sizeof(zeros)
                                                                         : (lfs_size_t)remaining;
                if (lfs_file_write(&lfs, &f, zeros, chunk) < 0)
                    break;
                remaining -= chunk;
            }
            lfs_file_sync(&lfs, &f);
        }
        lfs_file_close(&lfs, &f);
    }

    return lfs_remove(&lfs, path) >= 0;
}

int storage_key_list(key_record_t *out, int max_count) {
    if (!mounted)
        return -1;

    if (max_count > KEY_MAX_COUNT)
        max_count = KEY_MAX_COUNT;

    lfs_dir_t dir;
    if (lfs_dir_open(&lfs, &dir, DIR_KEYS) < 0)
        return -1;

    int             count = 0;
    struct lfs_info info;

    while (count < max_count && lfs_dir_read(&lfs, &dir, &info) > 0) {
        if (info.type != LFS_TYPE_REG)
            continue;
        uint16_t id = (uint16_t)strtoul(info.name, NULL, 10);
        if (!storage_key_get(id, &out[count]))
            continue;
        count++;
    }

    lfs_dir_close(&lfs, &dir);
    return count;
}