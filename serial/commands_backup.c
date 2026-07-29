#include "commands_handlers.h"
#include "hardware/buzzer.h"
#include "storage/backup.h"
#include "libs/base64/base64.h"
#include "pico/stdlib.h"
#include "pico/time.h"
#include <stdio.h>
#include <string.h>

void cmd_export_keys(int argc, char **argv) {
    static uint8_t export_buf[sizeof(backup_header_t) + BACKUP_MAX_KEYS * sizeof(backup_key_t)];
    static char    b64_buf[BASE64_ENCODED_LEN(sizeof(export_buf))];

    int len = backup_export(export_buf, sizeof(export_buf));
    if (len < 0) {
        printf("error: export failed\r\n");
        buzzer_play_command_ack();
        return;
    }

    base64_encode(export_buf, (size_t)len, b64_buf);
    printf("--- BEGIN HSLOCK BACKUP ---\r\n");
    printf("%s\r\n", b64_buf);
    printf("--- END HSLOCK BACKUP ---\r\n");

    buzzer_play_command_ack();
}

void cmd_import_keys(int argc, char **argv) {
    // Backup first
    printf("backing up current keys...\r\n");
    cmd_export_keys(0, NULL);

    printf("paste import data, then send empty line:\r\n");

    // Read base64 directly from serial into a large static buffer
    static char b64_buf[BASE64_ENCODED_LEN(sizeof(backup_header_t) +
                                           BACKUP_MAX_KEYS * sizeof(backup_key_t))];
    int         b64_len  = 0;
    bool        overflow = false;
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
            // M5: an over-long paste must ABORT the import, not be silently
            // dropped - a truncated blob decoded into import_buf is meaningless
            // and previously masked the length that overflows the decode buffer.
            if (b64_len + line_len < (int)sizeof(b64_buf)) {
                memcpy(b64_buf + b64_len, line, line_len);
                b64_len += line_len;
            } else {
                overflow = true;
            }
            line_len = 0;
        } else {
            if (line_len < (int)sizeof(line) - 1) {
                line[line_len++] = (char)c;
            }
        }
    }

    if (overflow) {
        printf("error: import data too large\r\n");
        buzzer_play_command_ack();
        return;
    }

    if (b64_len == 0) {
        printf("error: no data received\r\n");
        buzzer_play_command_ack();
        return;
    }

    static uint8_t import_buf[sizeof(backup_header_t) + BACKUP_MAX_KEYS * sizeof(backup_key_t)];

    // M5: reject up front any blob whose decoded length cannot fit import_buf,
    // before feeding it to the decoder. base64_decode is also capacity-bounded
    // (defence in depth), but rejecting here gives the operator a clear error.
    if (BASE64_DECODED_LEN((size_t)b64_len) > sizeof(import_buf)) {
        printf("error: import data too large\r\n");
        buzzer_play_command_ack();
        return;
    }

    int len = base64_decode(b64_buf, (size_t)b64_len, import_buf, sizeof(import_buf));
    if (len < 0) {
        printf("error: invalid base64\r\n");
        buzzer_play_command_ack();
        return;
    }

    // Export current keys as backup before overwriting
    printf("backing up current keys...\r\n");
    cmd_export_keys(0, NULL);

    printf("importing...\r\n");
    if (backup_import(import_buf, (size_t)len)) {
        printf("import ok\r\n");
    } else {
        printf("error: import failed\r\n");
    }

    buzzer_play_command_ack();
}
