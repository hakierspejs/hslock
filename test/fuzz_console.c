/*
 * libFuzzer harness: the serial-console command interface, end to end.
 *
 * This drives the REAL firmware input path exactly as it runs on the RP2040:
 *
 *   fuzz bytes -> getchar_timeout_us() -> console_task() char accumulator
 *              -> process_line() tokeniser -> commands_dispatch()
 *              -> real cmd_* handlers -> storage.c / backup.c on real littlefs
 *
 * console.c, commands.c and every cmd_* handler are the untouched first-party
 * sources; only the hardware/network leaves (buzzer, latch, led, light,
 * wifi, ntp, USB, flash, RNG, clock) are stubbed. Storage is backed by a
 * RAM-mapped XIP window (the same trick as test/harness_storage.c), so key
 * add/get/delete/list, wifi set/get and the base64 backup import/export code
 * all execute for real against a genuine littlefs.
 *
 * Per input we reset all persistent state (flash window, admin flag, console
 * input buffer) so a given input reproduces deterministically.
 */

#define _POSIX_C_SOURCE 200809L

#include <setjmp.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>

#include "hardware/flash.h" /* XIP_BASE, FLASH_SECTOR_SIZE */
#include "pico/stdlib.h"    /* PICO_FLASH_SIZE_BYTES, PICO_OK, PICO_ERROR_TIMEOUT */
#include "pico/unique_id.h"

#include "console.h"
#include "storage/storage.h"

/* admin_mode is a non-static extern in commands.c; reset it every run. */
extern bool admin_mode;

/* --- storage layout, mirrored from storage.c ------------------------------ */

#define STORAGE_SIZE_BYTES   (256 * 1024)
#define STORAGE_FLASH_OFFSET (PICO_FLASH_SIZE_BYTES - STORAGE_SIZE_BYTES)
#define STORAGE_WINDOW_BASE  ((uintptr_t)(XIP_BASE + STORAGE_FLASH_OFFSET))

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

/* --- fuzz input source + USB connection state ----------------------------- */

static const uint8_t *g_data;
static size_t         g_size;
static size_t         g_pos;
static bool           g_connected;

bool stdio_usb_connected(void) {
    return g_connected;
}

int getchar_timeout_us(uint32_t timeout_us) {
    (void)timeout_us;
    if (g_pos >= g_size)
        return PICO_ERROR_TIMEOUT;
    return g_data[g_pos++];
}

/* --- hardware / network leaves: no-op stubs ------------------------------- */

void buzzer_play_command_ack(void) {
}
void buzzer_play_auth_error(void) {
}
void buzzer_beep_short(void) {
}

void latch_open(void) {
}
void light_on(void) {
}
void light_off(void) {
}
void led_on(void) {
}
void led_off(void) {
}

void sleep_ms(uint32_t ms) {
    (void)ms; /* cmd_test would sleep 1s on hardware; skip it */
}

uint64_t time_us_64(void) {
    return 1234567890ULL; /* fixed uptime / boot-bypass clock */
}

uint32_t clock_get_unix_time(void) {
    return 1700000000u; /* fixed, non-zero: RTC "set", enables the TOTP path */
}

bool ntp_is_synced(void) {
    return false;
}
uint32_t ntp_last_sync_time(void) {
    return 0;
}
bool ntp_sync(void) {
    return false;
}

bool wifi_connect(const char *ssid, const char *password) {
    (void)ssid;
    (void)password;
    return false;
}

void pico_get_unique_board_id(pico_unique_board_id_t *id_out) {
    for (int i = 0; i < PICO_UNIQUE_BOARD_ID_SIZE_BYTES; i++)
        id_out->id[i] = (uint8_t)(0xA0 + i);
}

/* Deterministic RNG so add-key secret generation is reproducible. */
uint64_t get_rand_64(void) {
    static uint64_t s = 0x9E3779B97F4A7C15ULL;
    s ^= s << 13;
    s ^= s >> 7;
    s ^= s << 17;
    return s;
}

/* cmd_reboot calls watchdog_reboot() then spins in an infinite tight loop.
 * On hardware the watchdog fires; on the host we longjmp back out so the run
 * ends instead of hanging the fuzzer. */
static jmp_buf g_reboot_jmp;

void watchdog_reboot(uint32_t pc, uint32_t sp, uint32_t delay_ms) {
    (void)pc;
    (void)sp;
    (void)delay_ms;
    longjmp(g_reboot_jmp, 1);
}

/* --- per-run reset -------------------------------------------------------- */

static void flash_ram_reset(void) {
    memset((void *)STORAGE_WINDOW_BASE, 0xFF, STORAGE_SIZE_BYTES);
}

int LLVMFuzzerInitialize(int *argc, char ***argv) {
    (void)argc;
    (void)argv;

    /* Map the XIP storage window into host RAM at its true address, once. */
    void *base = mmap((void *)STORAGE_WINDOW_BASE, STORAGE_SIZE_BYTES, PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
    if (base == MAP_FAILED || (uintptr_t)base != STORAGE_WINDOW_BASE) {
        perror("mmap XIP window");
        return -1;
    }

    /* Silence all console output for throughput; keep stderr for sanitizers. */
    if (!freopen("/dev/null", "w", stdout)) {
        perror("freopen stdout");
        return -1;
    }
    return 0;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    /* Reset persistent state so this input reproduces deterministically. */
    flash_ram_reset();
    if (!storage_init())
        return 0; /* format/mount failure is not an input-driven bug */
    admin_mode = false;

    g_data      = data;
    g_size      = size;
    g_pos       = 0;
    g_connected = true;

    if (setjmp(g_reboot_jmp) == 0) {
        /* First call connects (prints banner, consumes no byte); subsequent
         * calls each read at most one byte. Drive until the input is drained. */
        console_task(); /* connect */
        while (g_pos < g_size)
            console_task();
    }

    /* Simulate USB disconnect: clears admin + zeroes the console input_len,
     * so no console state leaks into the next run. */
    g_connected = false;
    console_task();

    return 0;
}
