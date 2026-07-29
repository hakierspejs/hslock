#include "commands_handlers.h"
#include "hardware/buzzer.h"
#include "storage/backup.h"
#include "libs/base64/base64.h"
#include "pico/stdlib.h"
#include "pico/time.h"
#include <stdio.h>
#include <string.h>

#define BACKUP_PASSPHRASE_MAX 64

// ---------------------------------------------------------------------------
// Read one line from the console into buf (NUL-terminated, CR/LF stripped).
// Returns the length, or -1 on timeout with nothing entered.
// ---------------------------------------------------------------------------

static int read_line(char *buf, size_t cap, uint32_t timeout_ms) {
    size_t          len      = 0;
    absolute_time_t deadline = make_timeout_time_ms(timeout_ms);
    while (!time_reached(deadline)) {
        int c = getchar_timeout_us(10000);
        if (c == PICO_ERROR_TIMEOUT)
            continue;
        if (c == '\r')
            continue;
        if (c == '\n')
            break;
        if (len < cap - 1)
            buf[len++] = (char)c;
    }
    buf[len] = '\0';
    return (len == 0 && time_reached(deadline)) ? -1 : (int)len;
}

// ---------------------------------------------------------------------------
// Per-key admin confirmation prompt (backup_admin_confirm_fn).
// ---------------------------------------------------------------------------

static bool confirm_admin_prompt(uint16_t id, const char *name, void *ctx) {
    (void)ctx;
    printf("[import] key %u \"%s\" grants ADMIN. import as admin? type yes:\r\n", id, name);
    char answer[8];
    read_line(answer, sizeof(answer), 30000);
    return strcmp(answer, "yes") == 0;
}

// ---------------------------------------------------------------------------
// Emit an encrypted backup of the current key set under `passphrase`.
// ---------------------------------------------------------------------------

static void emit_backup(const char *passphrase) {
    static uint8_t export_buf[sizeof(backup_header_t) + BACKUP_MAX_KEYS * sizeof(backup_key_t)];
    static char    b64_buf[BASE64_ENCODED_LEN(sizeof(export_buf))];

    int len = backup_export(export_buf, sizeof(export_buf), passphrase);
    if (len < 0) {
        printf("error: export failed\r\n");
        return;
    }

    base64_encode(export_buf, (size_t)len, b64_buf);
    printf("--- BEGIN HSLOCK BACKUP ---\r\n");
    printf("%s\r\n", b64_buf);
    printf("--- END HSLOCK BACKUP ---\r\n");
}

void cmd_export_keys(int argc, char **argv) {
    (void)argc;
    (void)argv;

    printf("enter backup passphrase (encrypts the blob):\r\n");
    char passphrase[BACKUP_PASSPHRASE_MAX];
    if (read_line(passphrase, sizeof(passphrase), 60000) <= 0 || passphrase[0] == '\0') {
        printf("error: no passphrase entered\r\n");
        buzzer_play_command_ack();
        return;
    }

    emit_backup(passphrase);
    memset(passphrase, 0, sizeof(passphrase));

    buzzer_play_command_ack();
}

void cmd_import_keys(int argc, char **argv) {
    (void)argc;
    (void)argv;

    printf("enter backup passphrase (decrypts the blob):\r\n");
    char passphrase[BACKUP_PASSPHRASE_MAX];
    if (read_line(passphrase, sizeof(passphrase), 60000) <= 0 || passphrase[0] == '\0') {
        printf("error: no passphrase entered\r\n");
        buzzer_play_command_ack();
        return;
    }

    // Safety backup of the current key set (encrypted under the same passphrase)
    // before anything is overwritten.
    printf("backing up current keys...\r\n");
    emit_backup(passphrase);

    printf("paste import data, then send empty line:\r\n");

    static char b64_buf[BASE64_ENCODED_LEN(sizeof(backup_header_t) +
                                           BACKUP_MAX_KEYS * sizeof(backup_key_t))];
    int         b64_len = 0;
    char        line[256];
    int         line_len = 0;

    absolute_time_t deadline = make_timeout_time_ms(60000); // 60s to paste

    while (!time_reached(deadline)) {
        int c = getchar_timeout_us(10000);
        if (c == PICO_ERROR_TIMEOUT)
            continue;
        if (c == '\r')
            continue;

        if (c == '\n') {
            if (line_len == 0)
                break; // empty line = done
            if (b64_len + line_len < (int)sizeof(b64_buf)) {
                memcpy(b64_buf + b64_len, line, line_len);
                b64_len += line_len;
            }
            line_len = 0;
        } else {
            if (line_len < (int)sizeof(line) - 1) {
                line[line_len++] = (char)c;
            }
        }
    }

    if (b64_len == 0) {
        printf("error: no data received\r\n");
        memset(passphrase, 0, sizeof(passphrase));
        buzzer_play_command_ack();
        return;
    }

    static uint8_t import_buf[sizeof(backup_header_t) + BACKUP_MAX_KEYS * sizeof(backup_key_t)];

    int len = base64_decode(b64_buf, b64_len, import_buf);
    if (len < 0) {
        printf("error: invalid base64\r\n");
        memset(passphrase, 0, sizeof(passphrase));
        buzzer_play_command_ack();
        return;
    }

    printf("importing...\r\n");
    if (backup_import(import_buf, (size_t)len, passphrase, confirm_admin_prompt, NULL)) {
        printf("import ok\r\n");
    } else {
        printf("error: import failed\r\n");
    }

    memset(passphrase, 0, sizeof(passphrase));
    buzzer_play_command_ack();
}
