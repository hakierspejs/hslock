// Host harness for serial/commands.c — the command dispatcher.
//
// commands.c is a self-contained dispatch layer: given a pre-tokenized
// argc/argv (the tokenizer itself lives in serial/console.c, not here), it
// does a linear name lookup in COMMANDS[], enforces admin gating, validates
// the argument count against each command's [min_args, max_args], and routes
// to the matching handler — or prints an error for unknown commands.
//
// We link commands.c against SPY handler stubs (one per command, recording the
// last routed handler + argc) and a stub buzzer, then drive commands_dispatch()
// with a table of representative lines. Each line's outcome is observable via:
//   - which spy handler ran (or none, for a rejected line),
//   - the buzzer-ack count (dispatch acks on help/reject/unknown paths), and
//   - the captured stdout, whose error prefix distinguishes the reject reason.
// This exercises every branch of commands_dispatch WITHOUT pulling in the real
// key/system/network/backup handlers (which touch flash, wifi and USB).

// _POSIX_C_SOURCE: -std=c11 hides fileno/dup/dup2/close; we need them to
// redirect stdout for output capture.
#define _POSIX_C_SOURCE 200809L

#include "commands.h"
#include "commands_handlers.h"

#include <assert.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

// admin_mode is defined (non-static) in commands.c; drive it directly so we can
// test the admin gate without going through the real cmd_login/TOTP path.
extern bool admin_mode;

// ---------------------------------------------------------------------------
// Spies
// ---------------------------------------------------------------------------

static const char *g_last_handler; // name of the last spy handler that ran
static int         g_last_argc;    // argc it was called with
static int         g_buzzer_acks;  // buzzer_play_command_ack() call count

static void spy_reset(void) {
    g_last_handler = NULL;
    g_last_argc    = -1;
    g_buzzer_acks  = 0;
}

void buzzer_play_command_ack(void) {
    g_buzzer_acks++;
}

// One spy per real handler prototype in commands_handlers.h. Each records that
// it was the routed handler; commands.c only ever calls the one it looked up.
#define SPY(fn)                                                                                    \
    void fn(int argc, char **argv) {                                                               \
        (void)argv;                                                                                \
        g_last_handler = #fn;                                                                      \
        g_last_argc    = argc;                                                                     \
    }

SPY(cmd_status)
SPY(cmd_get_time)
SPY(cmd_test)
SPY(cmd_login)
SPY(cmd_logout)
SPY(cmd_reboot)
SPY(cmd_sync_ntp)
SPY(cmd_set_wifi)
SPY(cmd_list_keys)
SPY(cmd_get_key)
SPY(cmd_get_key_secret)
SPY(cmd_add_key)
SPY(cmd_rename_key)
SPY(cmd_enable_key)
SPY(cmd_disable_key)
SPY(cmd_delete_key)
SPY(cmd_set_key_admin)
SPY(cmd_unset_key_admin)
SPY(cmd_export_keys)
SPY(cmd_import_keys)

// ---------------------------------------------------------------------------
// Dispatch driver with stdout capture
// ---------------------------------------------------------------------------

static char g_captured[8192];

// Build argv from up to 8 string tokens (NULL-terminated), redirect stdout to a
// temp file for the duration of the dispatch, and stash what was printed in
// g_captured so tests can assert on the error prefix.
static void run(const char *tok0, ...) {
    char             *argv[8];
    int               argc = 0;
    const char       *t    = tok0;
    __builtin_va_list ap;
    __builtin_va_start(ap, tok0);
    while (t != NULL && argc < 8) {
        argv[argc++] = (char *)t;
        t            = __builtin_va_arg(ap, const char *);
    }
    __builtin_va_end(ap);

    spy_reset();

    fflush(stdout);
    int   saved = dup(fileno(stdout));
    FILE *tmp   = tmpfile();
    assert(tmp != NULL);
    fflush(stdout);
    dup2(fileno(tmp), fileno(stdout));

    commands_dispatch(argc, argv);

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

static void test_routes_public_command(void) {
    // "status" is public, 0..0 args -> routed regardless of admin state.
    admin_mode = false;
    run("status", (const char *)NULL);
    assert(g_last_handler != NULL && strcmp(g_last_handler, "cmd_status") == 0);
    assert(g_last_argc == 1);
}

static void test_help_and_alias_route_same_handler(void) {
    // "help" and its "?" alias both map to cmd_help (static, inside commands.c).
    // cmd_help is not a spy — it runs in commands.c and acks the buzzer once.
    admin_mode = false;
    run("help", (const char *)NULL);
    assert(g_last_handler == NULL); // internal handler, no spy
    assert(g_buzzer_acks == 1);     // cmd_help -> buzzer_play_command_ack
    assert(out_has("available commands"));

    run("?", (const char *)NULL);
    assert(g_buzzer_acks == 1);
    assert(out_has("available commands"));
}

static void test_help_hides_admin_rows_until_admin(void) {
    admin_mode = false;
    run("help", (const char *)NULL);
    assert(!out_has("logout")); // admin-only row hidden
    admin_mode = true;
    run("help", (const char *)NULL);
    assert(out_has("logout")); // now shown
    admin_mode = false;
}

static void test_unknown_command(void) {
    admin_mode = false;
    run("frobnicate", (const char *)NULL);
    assert(g_last_handler == NULL);
    assert(g_buzzer_acks == 1);
    assert(out_has("unknown command"));
}

static void test_empty_command_string(void) {
    // Empty command name reaches dispatch as argv[0]="" -> unknown path.
    admin_mode = false;
    run("", (const char *)NULL);
    assert(g_last_handler == NULL);
    assert(out_has("unknown command"));
}

static void test_admin_gate_blocks_when_not_admin(void) {
    // "logout" requires admin and takes 0 args; when not admin it must be
    // rejected on the admin gate BEFORE the handler runs.
    admin_mode = false;
    run("logout", (const char *)NULL);
    assert(g_last_handler == NULL);
    assert(g_buzzer_acks == 1);
    assert(out_has("requires admin mode"));
}

static void test_admin_gate_allows_when_admin(void) {
    admin_mode = true;
    run("logout", (const char *)NULL);
    assert(g_last_handler != NULL && strcmp(g_last_handler, "cmd_logout") == 0);
    admin_mode = false;
}

static void test_argc_too_few(void) {
    // "login <key_id> <totp_code>" needs exactly 2 user args; give it 1.
    admin_mode = false;
    run("login", "onlyone", (const char *)NULL);
    assert(g_last_handler == NULL);
    assert(g_buzzer_acks == 1);
    assert(out_has("usage:"));
}

static void test_argc_too_many(void) {
    // login with 3 user args exceeds max_args (2).
    admin_mode = false;
    run("login", "a", "b", "c", (const char *)NULL);
    assert(g_last_handler == NULL);
    assert(out_has("usage:"));
}

static void test_argc_exact_ok(void) {
    admin_mode = false;
    run("login", "keyid", "123456", (const char *)NULL);
    assert(g_last_handler != NULL && strcmp(g_last_handler, "cmd_login") == 0);
    assert(g_last_argc == 3);
}

static void test_admin_command_with_args_routes_when_admin(void) {
    // "add-key <id> <name>" — admin-only, exactly 2 args. Exercises the admin
    // gate PASS + argc PASS + route path together.
    admin_mode = true;
    run("add-key", "5", "frontdoor", (const char *)NULL);
    assert(g_last_handler != NULL && strcmp(g_last_handler, "cmd_add_key") == 0);
    assert(g_last_argc == 3);
    admin_mode = false;
}

static void test_admin_gate_precedes_argc_check(void) {
    // Even with a wrong argc, a non-admin caller of an admin command is
    // rejected on the admin gate (the gate is checked first in commands.c).
    admin_mode = false;
    run("add-key", "onlyone", (const char *)NULL);
    assert(g_last_handler == NULL);
    assert(out_has("requires admin mode"));
    assert(!out_has("usage:"));
}

static void test_on_disconnect_clears_admin(void) {
    admin_mode = true;
    assert(commands_is_admin());
    commands_on_disconnect(); // must clear admin + print notice
    assert(!admin_mode);
    assert(!commands_is_admin());
    // Second call is a no-op (admin already false) — exercises the false branch.
    commands_on_disconnect();
    assert(!commands_is_admin());
}

int main(void) {
    test_routes_public_command();
    test_help_and_alias_route_same_handler();
    test_help_hides_admin_rows_until_admin();
    test_unknown_command();
    test_empty_command_string();
    test_admin_gate_blocks_when_not_admin();
    test_admin_gate_allows_when_admin();
    test_argc_too_few();
    test_argc_too_many();
    test_argc_exact_ok();
    test_admin_command_with_args_routes_when_admin();
    test_admin_gate_precedes_argc_check();
    test_on_disconnect_clears_admin();

    printf("harness_commands: all assertions passed\n");
    return 0;
}
