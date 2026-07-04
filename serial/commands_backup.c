#include "commands_handlers.h"
#include "hardware/buzzer.h"
#include "storage/backup.h"
#include "libs/base64/base64.h"
#include <stdio.h>
#include <string.h>

void cmd_export_keys(int argc, char **argv) {
    static uint8_t export_buf[sizeof(backup_header_t) + BACKUP_MAX_KEYS * sizeof(backup_key_t)];
    static char    b64_buf[BASE64_ENCODED_LEN(sizeof(export_buf))];

    int len = backup_export(export_buf, sizeof(export_buf));
    if (len < 0) {
        printf("error: export failed\r\n");
        buzzer_beep_short();
        return;
    }

    base64_encode(export_buf, (size_t)len, b64_buf);
    printf("--- BEGIN HSLOCK BACKUP ---\r\n");
    printf("%s\r\n", b64_buf);
    printf("--- END HSLOCK BACKUP ---\r\n");

    buzzer_beep_short();
}

void cmd_import_keys(int argc, char **argv) {
    static uint8_t import_buf[sizeof(backup_header_t) + BACKUP_MAX_KEYS * sizeof(backup_key_t)];

    int len = base64_decode(argv[1], strlen(argv[1]), import_buf);
    if (len < 0) {
        printf("error: invalid base64\r\n");
        buzzer_beep_short();
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

    buzzer_beep_short();
}
