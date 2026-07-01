#include "storage.h"

#include "lfs.h"
#include "pico/stdlib.h"
#include "pico/flash.h"
#include "hardware/flash.h"
#include "hardware/sync.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

// ---------------------------------------------------------------------------
// Flash layout
// ---------------------------------------------------------------------------
// Dedicate the last 256KB of the Pico W's 2MB flash to LittleFS.
// The linker places firmware at the START of flash; storage is at the END,
// so they never collide as long as firmware stays under ~1.75MB.

#define STORAGE_SIZE_BYTES  (256 * 1024)
#define STORAGE_FLASH_OFFSET (PICO_FLASH_SIZE_BYTES - STORAGE_SIZE_BYTES)

// LittleFS block = one flash erase sector
#define LFS_BLOCK_SIZE   FLASH_SECTOR_SIZE          // 4096
#define LFS_BLOCK_COUNT  (STORAGE_SIZE_BYTES / LFS_BLOCK_SIZE)  // 64
#define LFS_READ_SIZE    FLASH_PAGE_SIZE             // 256
#define LFS_PROG_SIZE    FLASH_PAGE_SIZE             // 256
#define LFS_CACHE_SIZE   FLASH_PAGE_SIZE             // 256
#define LFS_LOOKAHEAD    64

static uint8_t lfs_file_buf[LFS_CACHE_SIZE];

static const struct lfs_file_config LFS_FILE_CFG = {
    .buffer = lfs_file_buf,
};

// ---------------------------------------------------------------------------
// Flash block device callbacks
// ---------------------------------------------------------------------------

static int flash_read(const struct lfs_config *c, lfs_block_t block,
                      lfs_off_t off, void *buffer, lfs_size_t size) {
    uint32_t addr = XIP_BASE + STORAGE_FLASH_OFFSET
                  + block * LFS_BLOCK_SIZE + off;
    memcpy(buffer, (const void *)addr, size);
    return LFS_ERR_OK;
}

// Params struct for flash_safe_execute callbacks (can't pass multiple args)
typedef struct { uint32_t offset; const uint8_t *data; size_t size; } prog_params_t;
typedef struct { uint32_t offset; size_t size; }                       erase_params_t;

// those functions must live in ram so they can be executed while flash is locked
static void __no_inline_not_in_flash_func(do_flash_program)(void *param) {
    prog_params_t *p = (prog_params_t *)param;
    flash_range_program(p->offset, p->data, p->size);
}

static void __no_inline_not_in_flash_func(do_flash_erase)(void *param) {
    erase_params_t *p = (erase_params_t *)param;
    flash_range_erase(p->offset, p->size);
}

static int flash_prog(const struct lfs_config *c, lfs_block_t block,
                      lfs_off_t off, const void *buffer, lfs_size_t size) {
    prog_params_t p = {
        .offset = STORAGE_FLASH_OFFSET + block * LFS_BLOCK_SIZE + off,
        .data   = (const uint8_t *)buffer,
        .size   = size
    };
    // flash_safe_execute pauses core 1 and disables interrupts while writing
    int rc = flash_safe_execute(do_flash_program, &p, UINT32_MAX);
    return rc == PICO_OK ? LFS_ERR_OK : LFS_ERR_IO;
}

static int flash_erase(const struct lfs_config *c, lfs_block_t block) {
    erase_params_t p = {
        .offset = STORAGE_FLASH_OFFSET + block * LFS_BLOCK_SIZE,
        .size   = LFS_BLOCK_SIZE
    };
    int rc = flash_safe_execute(do_flash_erase, &p, UINT32_MAX);
    return rc == PICO_OK ? LFS_ERR_OK : LFS_ERR_IO;
}

static int flash_sync(const struct lfs_config *c) {
    return LFS_ERR_OK;  // no write buffer on NOR flash
}

// ---------------------------------------------------------------------------
// LittleFS state
// ---------------------------------------------------------------------------

static uint8_t lfs_read_buf [LFS_CACHE_SIZE];
static uint8_t lfs_prog_buf [LFS_CACHE_SIZE];
static uint8_t lfs_lookahead_buf[LFS_LOOKAHEAD / 8];

static const struct lfs_config LFS_CFG = {
    .read  = flash_read,
    .prog  = flash_prog,
    .erase = flash_erase,
    .sync  = flash_sync,

    .read_size      = LFS_READ_SIZE,
    .prog_size      = LFS_PROG_SIZE,
    .block_size     = LFS_BLOCK_SIZE,
    .block_count    = LFS_BLOCK_COUNT,
    .cache_size     = LFS_CACHE_SIZE,
    .lookahead_size = LFS_LOOKAHEAD,
    .block_cycles   = 500,  // wear leveling hint

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
    printf("[storage] lfs_mount rc=%d\r\n", rc);

    if (rc < 0) {
        printf("[storage] mount failed, formatting...\r\n");
        rc = lfs_format(&lfs, &LFS_CFG);
        printf("[storage] lfs_format rc=%d\r\n", rc);
        if (rc < 0) return false;

        rc = lfs_mount(&lfs, &LFS_CFG);
        printf("[storage] lfs_mount after format rc=%d\r\n", rc);
        if (rc < 0) return false;
    }

    if (!ensure_dirs()) {
        printf("[storage] ensure_dirs failed\r\n");
        return false;
    }

    mounted = true;
    printf("[storage] init ok\r\n");
    return true;
}

// ---------------------------------------------------------------------------
// WiFi
// ---------------------------------------------------------------------------

bool storage_wifi_get(wifi_config_t *out) {
    if (!mounted) return false;

    lfs_file_t f;
    if (lfs_file_opencfg(&lfs, &f, FILE_WIFI, LFS_O_RDONLY, &LFS_FILE_CFG) < 0)
        return false;

    lfs_ssize_t n = lfs_file_read(&lfs, &f, out, sizeof(wifi_config_t));
    lfs_file_close(&lfs, &f);
    return n == (lfs_ssize_t)sizeof(wifi_config_t);
}

bool storage_wifi_set(const wifi_config_t *cfg) {
    if (!mounted) return false;

    lfs_file_t f;
    int flags = LFS_O_WRONLY | LFS_O_CREAT | LFS_O_TRUNC;
    int rc = lfs_file_opencfg(&lfs, &f, FILE_WIFI, flags, &LFS_FILE_CFG);
    if (rc < 0) return false;

    lfs_ssize_t n = lfs_file_write(&lfs, &f, cfg, sizeof(wifi_config_t));
    lfs_file_close(&lfs, &f);
    return n == (lfs_ssize_t)sizeof(wifi_config_t);
}

bool storage_wifi_clear(void) {
    if (!mounted) return false;
    return lfs_remove(&lfs, FILE_WIFI) >= 0;
}

// ---------------------------------------------------------------------------
// Key CRUD
// ---------------------------------------------------------------------------

bool storage_key_exists(uint16_t id) {
    if (!mounted) return false;
    char path[40];
    key_path(id, path, sizeof(path));
    struct lfs_info info;
    return lfs_stat(&lfs, path, &info) >= 0;
}

bool storage_key_get(uint16_t id, key_record_t *out) {
    if (!mounted) return false;
    char path[40];
    key_path(id, path, sizeof(path));

    lfs_file_t f;
    if (lfs_file_opencfg(&lfs, &f, path, LFS_O_RDONLY, &LFS_FILE_CFG) < 0)
        return false;

    lfs_ssize_t n = lfs_file_read(&lfs, &f, out, sizeof(key_record_t));
    lfs_file_close(&lfs, &f);
    return n == (lfs_ssize_t)sizeof(key_record_t);
}

bool storage_key_save(const key_record_t *key) {
    if (!mounted) return false;
    char path[40];
    key_path(key->id, path, sizeof(path));

    lfs_file_t f;
    int flags = LFS_O_WRONLY | LFS_O_CREAT | LFS_O_TRUNC;
    if (lfs_file_opencfg(&lfs, &f, path, flags, &LFS_FILE_CFG) < 0)
        return false;

    lfs_ssize_t n = lfs_file_write(&lfs, &f, key, sizeof(key_record_t));
    lfs_file_close(&lfs, &f);
    return n == (lfs_ssize_t)sizeof(key_record_t);
}

bool storage_key_delete(uint16_t id) {
    if (!mounted) return false;
    char path[40];
    key_path(id, path, sizeof(path));
    return lfs_remove(&lfs, path) >= 0;
}

int storage_key_list(key_record_t *out, int max_count) {
    if (!mounted) return -1;

    lfs_dir_t dir;
    if (lfs_dir_open(&lfs, &dir, DIR_KEYS) < 0)
        return -1;

    int count = 0;
    struct lfs_info info;

    while (count < max_count && lfs_dir_read(&lfs, &dir, &info) > 0) {
        if (info.type != LFS_TYPE_REG) continue;
        uint16_t id = (uint16_t)strtoul(info.name, NULL, 10);
        if (!storage_key_get(id, &out[count])) continue;
        count++;
    }

    lfs_dir_close(&lfs, &dir);
    return count;
}