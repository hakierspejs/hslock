// Host harness for serial/commands_system.c's cmd_login - the admin
// authorization gate.
//
// Unlike harness_commands.c (which links the dispatcher against SPY handlers to
// test routing / admin-gating / argc), this harness links the REAL cmd_login
// and drives it directly against controllable test doubles for storage, the
// clock and TOTP. It is the CI-gated regression test for issue M1: cmd_login
// must FAIL CLOSED - it may never grant admin from an absent wifi config or an
// unset clock, and once any enabled+valid admin key exists a matching key id +
// TOTP code is mandatory. The only path that enters admin without a credential
// is a genuinely unprovisioned device (no enabled admin key exists at all).
//
// commands_system.c defines the whole system-command group (status/get-time/
// test/login/logout/reboot/format-storage), so every external symbol those
// handlers reference must resolve at link time even though we only call
// cmd_login here. The stubs below are the minimum to link + a small test
// double the tests steer.

// _POSIX_C_SOURCE: -std=c11 hides fileno/dup/dup2/close; we need them to
// redirect stdout for output capture.
#define _POSIX_C_SOURCE 200809L

#include "commands_handlers.h"
#include "pico/unique_id.h"
#include "storage/storage.h"
#include "storage/backup.h"

#include <assert.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

// admin_mode is normally defined (non-static) in serial/commands.c, which we do
// NOT link here; provide the definition cmd_login writes.
bool admin_mode = false;

// ---------------------------------------------------------------------------
// Test double state - each test seeds this, then calls cmd_login and asserts.
// ---------------------------------------------------------------------------

static bool         g_mounted       = true;
static bool         g_wifi_present  = true;
static bool         g_clock_set     = true;   // false => RTC unset / NTP unsynced
static uint32_t     g_expected_code = 123456; // TOTP that "matches" when clock is set
static key_record_t g_keys[BACKUP_MAX_KEYS];
static int          g_key_count = 0;

static int g_auth_errors; // buzzer_play_auth_error() call count
static int g_acks;        // buzzer_play_command_ack() call count

static void reset_state(void) {
    g_mounted       = true;
    g_wifi_present  = true;
    g_clock_set     = true;
    g_expected_code = 123456;
    g_key_count     = 0;
    memset(g_keys, 0, sizeof(g_keys));
    admin_mode    = false;
    g_auth_errors = 0;
    g_acks        = 0;
}

// Append one admin/non-admin key to the double.
static void add_key(uint16_t id, bool enabled, bool admin, bool checksum_ok) {
    assert(g_key_count < BACKUP_MAX_KEYS);
    key_record_t *k      = &g_keys[g_key_count++];
    k->id                = id;
    k->is_enabled        = enabled;
    k->is_admin          = admin;
    k->is_checksum_valid = checksum_ok;
}

// ---------------------------------------------------------------------------
// Storage / clock / TOTP / buzzer doubles (real handler externs)
// ---------------------------------------------------------------------------

bool storage_is_mounted(void) {
    return g_mounted;
}

bool storage_wifi_get(wifi_config_t *out) {
    if (!g_wifi_present)
        return false;
    memset(out, 0, sizeof(*out));
    strcpy(out->ssid, "net");
    return true;
}

int storage_key_list(key_record_t *out, int max_count) {
    int n = g_key_count < max_count ? g_key_count : max_count;
    for (int i = 0; i < n; i++)
        out[i] = g_keys[i];
    return n;
}

bool storage_key_get(uint16_t id, key_record_t *out) {
    for (int i = 0; i < g_key_count; i++) {
        if (g_keys[i].id == id) {
            *out = g_keys[i];
            return true;
        }
    }
    return false;
}

// Real totp_verify reads the RTC and returns false when the clock is unset;
// model that faithfully so the "unset time is never an escalation" property is
// what we assert against.
bool totp_verify(const uint8_t *secret, size_t secret_len, uint32_t code) {
    (void)secret;
    (void)secret_len;
    if (!g_clock_set)
        return false;
    return code == g_expected_code;
}

void buzzer_play_auth_error(void) {
    g_auth_errors++;
}
void buzzer_play_command_ack(void) {
    g_acks++;
}

// ---- Link-only stubs (referenced by other handlers in commands_system.c) ----

bool commands_is_admin(void) {
    return admin_mode;
}
bool storage_key_save(const key_record_t *k) {
    (void)k;
    return true;
}
bool storage_key_delete(uint16_t id) {
    (void)id;
    return true;
}
bool storage_key_exists(uint16_t id) {
    (void)id;
    return false;
}
bool storage_format(void) {
    return true;
}
bool clock_get_unix_time(uint32_t *out) {
    if (!g_clock_set)
        return false;
    *out = 1700000000UL;
    return true;
}
void clock_set_from_unix_time(uint32_t t) {
    (void)t;
}
uint32_t ntp_last_sync_time(void) {
    return 0;
}
bool ntp_is_synced(void) {
    return false;
}

void buzzer_play_boot(void) {
}
void light_on(void) {
}
void light_off(void) {
}
void led_on(void) {
}
void led_off(void) {
}
void latch_open(void) {
}

void sleep_ms(uint32_t ms) {
    (void)ms;
}
void sleep_us(uint64_t us) {
    (void)us;
}
uint64_t time_us_64(void) {
    return 600ULL * 1000000ULL;
} // well past any boot window
uint64_t make_timeout_time_ms(uint32_t ms) {
    return ms;
}
bool time_reached(uint64_t t) {
    (void)t;
    return true;
}
int getchar_timeout_us(uint32_t us) {
    (void)us;
    return -1;
}
void watchdog_reboot(uint32_t a, uint32_t b, uint32_t c) {
    (void)a;
    (void)b;
    (void)c;
}
void watchdog_enable(uint32_t a, bool b) {
    (void)a;
    (void)b;
}
void pico_get_unique_board_id(pico_unique_board_id_t *id) {
    memset(id, 0, sizeof(*id));
}

// ---------------------------------------------------------------------------
// cmd_login driver with stdout capture
// ---------------------------------------------------------------------------

static char g_captured[4096];

static void login(const char *id, const char *code) {
    char *argv[3];
    argv[0] = (char *)"login";
    argv[1] = (char *)id;
    argv[2] = (char *)code;

    fflush(stdout);
    int   saved = dup(fileno(stdout));
    FILE *tmp   = tmpfile();
    assert(tmp != NULL);
    fflush(stdout);
    dup2(fileno(tmp), fileno(stdout));

    cmd_login(3, argv);

    fflush(stdout);
    dup2(saved, fileno(stdout));
    close(saved);

    rewind(tmp);
    size_t n      = fread(g_captured, 1, sizeof(g_captured) - 1, tmp);
    g_captured[n] = '\0';
    fclose(tmp);
}

static bool out_has(const char *needle) {
    return strstr(g_captured, needle) != NULL;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

// Baseline: provisioned device, correct credentials -> admin granted.
static void test_valid_credentials_grant_admin(void) {
    reset_state();
    add_key(5, true, true, true);
    login("5", "123456");
    assert(admin_mode);
    assert(out_has("admin mode enabled"));
    assert(g_acks == 1 && g_auth_errors == 0);
}

// M1 regression: a provisioned device with NO wifi config must still require
// credentials. The old code granted "open mode" here, ignoring argv entirely.
static void test_no_wifi_does_not_bypass_when_admin_key_exists(void) {
    reset_state();
    g_wifi_present = false; // wifi unconfigured
    add_key(5, true, true, true);
    login("5", "000000"); // wrong TOTP
    assert(!admin_mode);
    assert(out_has("invalid credentials"));
    assert(!out_has("open mode"));
    assert(g_auth_errors == 1);
}

// M1 regression: an unset RTC (network-inducible: deny wifi assoc so NTP never
// syncs) must NOT grant admin when an admin key exists. The old code granted
// "open mode" once uptime passed a 5-minute window.
static void test_unset_clock_does_not_bypass_when_admin_key_exists(void) {
    reset_state();
    g_clock_set = false; // RTC never initialised
    add_key(5, true, true, true);
    login("5", "123456"); // even a "correct" code fails because clock is unset
    assert(!admin_mode);
    assert(out_has("invalid credentials"));
    assert(!out_has("open mode"));
    assert(g_auth_errors == 1);
}

// M1 regression combined worst case: no wifi AND unset clock AND a valid admin
// key present -> still fails closed.
static void test_no_wifi_and_unset_clock_still_fail_closed(void) {
    reset_state();
    g_wifi_present = false;
    g_clock_set    = false;
    add_key(5, true, true, true);
    login("5", "123456");
    assert(!admin_mode);
    assert(!out_has("open mode"));
}

static void test_wrong_totp_denied(void) {
    reset_state();
    add_key(5, true, true, true);
    login("5", "999999");
    assert(!admin_mode);
    assert(out_has("invalid credentials"));
}

static void test_unknown_key_id_denied(void) {
    reset_state();
    add_key(5, true, true, true);
    login("9", "123456"); // no key with id 9
    assert(!admin_mode);
    assert(out_has("invalid credentials"));
}

static void test_disabled_admin_key_denied(void) {
    reset_state();
    add_key(5, false /*disabled*/, true, true); // not counted as any_admin
    // With no ENABLED admin key, the device is treated as unprovisioned and
    // enters provisioning mode - a disabled key is not a usable credential.
    login("5", "123456");
    assert(admin_mode); // provisioning path (no enabled admin key exists)
    assert(out_has("provisioning mode"));
}

// A key that is enabled+admin but corrupt (bad checksum) does not count as an
// admin key; and requesting it directly is rejected as invalid credentials.
static void test_corrupt_admin_key_not_counted(void) {
    reset_state();
    add_key(5, true, true, false /*bad checksum*/);
    // any_admin stays false -> provisioning mode (no trustworthy admin key).
    login("5", "123456");
    assert(admin_mode);
    assert(out_has("provisioning mode"));
}

// Genuinely unprovisioned device (no admin key at all) -> bootstrap/provisioning
// grant is the ONLY credential-free admin path, gated on any_admin == false.
static void test_unprovisioned_device_enters_provisioning(void) {
    reset_state();
    // no keys at all
    login("0", "0");
    assert(admin_mode);
    assert(out_has("provisioning mode"));
    assert(g_acks == 1);
}

// A non-admin enabled key does not provision the device: still bootstrap.
static void test_non_admin_key_is_not_provisioned(void) {
    reset_state();
    add_key(3, true, false /*not admin*/, true);
    login("3", "123456");
    assert(admin_mode); // provisioning (no ADMIN key exists)
    assert(out_has("provisioning mode"));
}

// Provisioning path closes the moment an enabled admin key exists: adding one
// flips the device to credential-required.
static void test_provisioning_closes_once_admin_exists(void) {
    reset_state();
    add_key(7, true, true, true);
    login("0", "0"); // wrong credentials, but any_admin is now true
    assert(!admin_mode);
    assert(out_has("invalid credentials"));
    assert(!out_has("provisioning mode"));
}

// Storage unavailable -> fail closed regardless of anything else.
static void test_storage_unavailable_fail_closed(void) {
    reset_state();
    g_mounted = false;
    add_key(5, true, true, true);
    login("5", "123456");
    assert(!admin_mode);
    assert(out_has("storage unavailable"));
    assert(g_auth_errors == 1);
}

int main(void) {
    test_valid_credentials_grant_admin();
    test_no_wifi_does_not_bypass_when_admin_key_exists();
    test_unset_clock_does_not_bypass_when_admin_key_exists();
    test_no_wifi_and_unset_clock_still_fail_closed();
    test_wrong_totp_denied();
    test_unknown_key_id_denied();
    test_disabled_admin_key_denied();
    test_corrupt_admin_key_not_counted();
    test_unprovisioned_device_enters_provisioning();
    test_non_admin_key_is_not_provisioned();
    test_provisioning_closes_once_admin_exists();
    test_storage_unavailable_fail_closed();

    printf("harness_login: all assertions passed\n");
    return 0;
}
