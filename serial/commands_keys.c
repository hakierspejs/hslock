#include "commands_handlers.h"
#include "hardware/buzzer.h"
#include "hardware/clock.h"
#include "shared/random.h"
#include "storage/backup.h"
#include "storage/storage.h"
#include "base32.h"
#include "qrcodegen.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

void cmd_list_keys(int argc, char **argv) {
    key_record_t keys[BACKUP_MAX_KEYS];
    int count = storage_key_list(keys, BACKUP_MAX_KEYS);

    if (count < 0) {
        printf("error: failed to read keys\r\n");
        buzzer_play_command_ack();
        return;
    }
    if (count == 0) {
        printf("no keys stored\r\n");
        buzzer_play_command_ack();
        return;
    }

   printf("%-6s  %-2s  %-24s  %-7s  %-5s  %s\r\n",
       "id", "ok", "name", "enabled", "admin", "created");
    printf("------  --  ------------------------  -------  -----  -------------------\r\n");

    for (int i = 0; i < count; i++) {
        key_record_t *k = &keys[i];

        char created[20] = "unknown";
        if (k->created_at > 0) {
            time_t t = (time_t)k->created_at;
            struct tm *tm = gmtime(&t);
            strftime(created, sizeof(created), "%Y-%m-%d %H:%M:%S", tm);
        }

        printf("%-6u  %-2s  %-24s  %-7s  %-5s  %s\r\n",
            k->id,
            k->is_checksum_valid ? "v" : "x",
            k->name,
            k->is_enabled ? "yes" : "no",
            k->is_admin   ? "yes" : "no",
            created);
    }

    printf("\r\n%d key(s)\r\n", count);
    buzzer_play_command_ack();
}

void cmd_get_key(int argc, char **argv) {
    uint16_t id = (uint16_t)strtoul(argv[1], NULL, 10);

    if (id > KEY_ID_MAX) {
        printf("error: id must be 0-255\r\n");
        buzzer_play_command_ack();
        return;
    }

    key_record_t key;
    if (!storage_key_get(id, &key)) {
        printf("error: key %u not found\r\n", id);
        buzzer_play_command_ack();
        return;
    }

    char created[20] = "unknown";
    if (key.created_at > 0) {
        time_t t = (time_t)key.created_at;
        struct tm *tm = gmtime(&t);
        strftime(created, sizeof(created), "%Y-%m-%d %H:%M:%S", tm);
    }

    printf("id:      %u\r\n",  key.id);
    printf("ok:      %s\r\n",  key.is_checksum_valid   ? "v" : "x");
    printf("name:    %s\r\n",  key.name);
    printf("enabled: %s\r\n",  key.is_enabled ? "yes" : "no");
    printf("admin:   %s\r\n",  key.is_admin   ? "yes" : "no");
    printf("created: %s\r\n",  created);
    printf("secret:  ");
    for (int i = 0; i < KEY_SECRET_LEN; i++) {
        printf("%02X", key.secret[i]);
    }
    printf("\r\n");

    buzzer_play_command_ack();
}

// Łódź URL-encoded: Ł=%C5%81, ó=%C3%B3, ź=%C5%BA, space=%20
#define ISSUER_ENC "Hackerspejs%20%C5%81%C3%B3d%C5%BA"
#define QR_BORDER       2  // quiet zone — required for scanners to work
#define SECRET_B32_LEN  BASE32_ENCODED_LEN(KEY_SECRET_LEN)  // 33 bytes for 20-byte secret

void cmd_get_key_secret(int argc, char **argv) {
    uint16_t id = (uint16_t)strtoul(argv[1], NULL, 10);

    if (id > KEY_ID_MAX) {
        printf("error: id must be 0-%d\r\n", KEY_ID_MAX);
        buzzer_play_command_ack();
        return;
    }

    key_record_t key;
    if (!storage_key_get(id, &key)) {
        printf("error: key %u not found\r\n", id);
        buzzer_play_command_ack();
        return;
    }

    if (!key.is_checksum_valid) {
        printf("error: key %u is corrupt\r\n", id);
        buzzer_play_command_ack();
        return;
    }

    // Base32-encode secret
    char secret_b32[SECRET_B32_LEN];
    base32_encode(key.secret, KEY_SECRET_LEN, secret_b32);

    // Build otpauth URI
    char uri[200];
    snprintf(uri, sizeof(uri),
        "otpauth://totp/%s:key%%20%u?secret=%s&issuer=%s&digits=6&period=30",
        ISSUER_ENC, id, secret_b32, ISSUER_ENC);

    printf("secret:  %s\r\n", secret_b32);
    printf("uri:     %s\r\n\r\n", uri);

    // Generate QR code - static buffers to avoid stack overflow
    static uint8_t qr[qrcodegen_BUFFER_LEN_MAX];
    static uint8_t tmp[qrcodegen_BUFFER_LEN_MAX];

    bool ok = qrcodegen_encodeText(uri, tmp, qr,
                                    qrcodegen_Ecc_MEDIUM,
                                    qrcodegen_VERSION_MIN,
                                    qrcodegen_VERSION_MAX,
                                    qrcodegen_Mask_AUTO,
                                    true);
    if (!ok) {
        printf("error: QR generation failed\r\n");
        buzzer_play_command_ack();
        return;
    }

    // Print ASCII QR with quiet zone border
    int size = qrcodegen_getSize(qr);
    for (int y = -QR_BORDER; y < size + QR_BORDER; y++) {
        for (int x = -QR_BORDER; x < size + QR_BORDER; x++) {
            bool dark = (x >= 0 && y >= 0 && x < size && y < size)
                     && qrcodegen_getModule(qr, x, y);
            printf(dark ? "##" : "  ");
        }
        printf("\r\n");
    }
    printf("\r\n");

    buzzer_play_command_ack();
}

void cmd_add_key(int argc, char **argv) {
    uint16_t id = (uint16_t)strtoul(argv[1], NULL, 10);

    if (id > KEY_ID_MAX) {
        printf("error: id must be 0-255\r\n");
        buzzer_play_command_ack();
        return;
    }

    if (storage_key_exists(id)) {
        printf("error: key %u already exists\r\n", id);
        buzzer_play_command_ack();
        return;
    }

    if (strlen(argv[2]) >= KEY_NAME_MAX) {
        printf("error: name too long (max %d chars)\r\n", KEY_NAME_MAX - 1);
        buzzer_play_command_ack();
        return;
    }

    key_record_t key = {0};
    key.id         = id;
    key.is_enabled = true;
    key.is_admin   = false;
    key.is_checksum_valid   = true;
    key.created_at = clock_get_unix_time();
    strncpy(key.name, argv[2], KEY_NAME_MAX - 1);
    generate_secret(key.secret);

    if (storage_key_save(&key)) {
        printf("key %u '%s' added\r\n", id, key.name);
    } else {
        printf("error: failed to save key %u\r\n", id);
    }

    buzzer_play_command_ack();
}

void cmd_rename_key(int argc, char **argv) {
    uint16_t id = (uint16_t)strtoul(argv[1], NULL, 10);

    if (id > KEY_ID_MAX) {
        printf("error: id must be 0-255\r\n");
        buzzer_play_command_ack();
        return;
    }

    if (strlen(argv[2]) >= KEY_NAME_MAX) {
        printf("error: name too long (max %d chars)\r\n", KEY_NAME_MAX - 1);
        buzzer_play_command_ack();
        return;
    }

    key_record_t key;
    if (!storage_key_get(id, &key)) {
        printf("error: key %u not found\r\n", id);
        buzzer_play_command_ack();
        return;
    }

    strncpy(key.name, argv[2], KEY_NAME_MAX - 1);
    key.name[KEY_NAME_MAX - 1] = '\0';

    if (storage_key_save(&key)) {
        printf("key %u renamed to '%s'\r\n", id, key.name);
    } else {
        printf("error: failed to save key %u\r\n", id);
    }

    buzzer_play_command_ack();
}

void cmd_enable_key(int argc, char **argv) {
    uint16_t id = (uint16_t)strtoul(argv[1], NULL, 10);

    if (id > KEY_ID_MAX) {
        printf("error: id must be 0-255\r\n");
        buzzer_play_command_ack();
        return;
    }

    key_record_t key;
    if (!storage_key_get(id, &key)) {
        printf("error: key %u not found\r\n", id);
        buzzer_play_command_ack();
        return;
    }

    if (key.is_enabled) {
        printf("key %u is already enabled\r\n", id);
        buzzer_play_command_ack();
        return;
    }

    key.is_enabled = true;
    if (storage_key_save(&key)) {
        printf("key %u enabled\r\n", id);
    } else {
        printf("error: failed to save key %u\r\n", id);
    }

    buzzer_play_command_ack();
}

void cmd_disable_key(int argc, char **argv) {
    uint16_t id = (uint16_t)strtoul(argv[1], NULL, 10);

    if (id > KEY_ID_MAX) {
        printf("error: id must be 0-255\r\n");
        buzzer_play_command_ack();
        return;
    }

    key_record_t key;
    if (!storage_key_get(id, &key)) {
        printf("error: key %u not found\r\n", id);
        buzzer_play_command_ack();
        return;
    }

    if (!key.is_enabled) {
        printf("key %u is already disabled\r\n", id);
        buzzer_play_command_ack();
        return;
    }

    key.is_enabled = false;
    if (storage_key_save(&key)) {
        printf("key %u disabled\r\n", id);
    } else {
        printf("error: failed to save key %u\r\n", id);
    }

    buzzer_play_command_ack();
}

void cmd_delete_key(int argc, char **argv) {
    uint16_t id = (uint16_t)strtoul(argv[1], NULL, 10);

    if (id > KEY_ID_MAX) {
        printf("error: id must be 0-255\r\n");
        buzzer_play_command_ack();
        return;
    }

    if (!storage_key_exists(id)) {
        printf("error: key %u does not exist\r\n", id);
        buzzer_play_command_ack();
        return;
    }

    if (storage_key_delete(id)) {
        printf("key %u deleted\r\n", id);
    } else {
        printf("error: failed to delete key %u\r\n", id);
    }

    buzzer_play_command_ack();
}

void cmd_set_key_admin(int argc, char **argv) {
    uint16_t id = (uint16_t)strtoul(argv[1], NULL, 10);

    if (id > KEY_ID_MAX) {
        printf("error: id must be 0-255\r\n");
        buzzer_play_command_ack();
        return;
    }

    key_record_t key;
    if (!storage_key_get(id, &key)) {
        printf("error: key %u not found\r\n", id);
        buzzer_play_command_ack();
        return;
    }

    if (key.is_admin) {
        printf("key %u is already admin\r\n", id);
        buzzer_play_command_ack();
        return;
    }

    key.is_admin = true;
    if (storage_key_save(&key)) {
        printf("key %u set as admin\r\n", id);
    } else {
        printf("error: failed to save key %u\r\n", id);
    }

    buzzer_play_command_ack();
}

void cmd_unset_key_admin(int argc, char **argv) {
    uint16_t id = (uint16_t)strtoul(argv[1], NULL, 10);

    if (id > KEY_ID_MAX) {
        printf("error: id must be 0-255\r\n");
        buzzer_play_command_ack();
        return;
    }

    key_record_t key;
    if (!storage_key_get(id, &key)) {
        printf("error: key %u not found\r\n", id);
        buzzer_play_command_ack();
        return;
    }

    if (!key.is_admin) {
        printf("key %u is not admin\r\n", id);
        buzzer_play_command_ack();
        return;
    }

    key.is_admin = false;
    if (storage_key_save(&key)) {
        printf("key %u unset as admin\r\n", id);
    } else {
        printf("error: failed to save key %u\r\n", id);
    }

    buzzer_play_command_ack();
}
