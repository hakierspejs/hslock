#include "core1.h"

#include "pico/stdlib.h"
#include "pico/multicore.h"

#include "hardware/buzzer.h"
#include "hardware/keypad.h"
#include "hardware/latch.h"
#include "hardware/led.h"
#include "hardware/watchdog.h"

#include "storage/storage.h"
#include "shared/totp.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// ---------------------------------------------------------------------------
// Input buffer
// Input format: [1-3 digit key ID][6 digit TOTP code]#
// Min length: 7 (1 digit ID + 6 digit code)
// Max length: 9 (3 digit ID + 6 digit code)
// ---------------------------------------------------------------------------

#define INPUT_MIN_LEN 7
#define INPUT_MAX_LEN 9
#define TOTP_CODE_LEN 6

static char input_buf[INPUT_MAX_LEN + 1];
static int  input_len = 0;

static void input_clear(void) {
    memset(input_buf, 0, sizeof(input_buf));
    input_len = 0;
}

// ---------------------------------------------------------------------------
// Process completed input
// ---------------------------------------------------------------------------

static void process_input(void) {
    if (input_len < INPUT_MIN_LEN || input_len > INPUT_MAX_LEN) {
        printf("[core1] invalid input length: %d\r\n", input_len);
        buzzer_play_fail();
        input_clear();
        return;
    }

    // Split: last 6 digits = code, remainder = key ID
    int id_len = input_len - TOTP_CODE_LEN;

    char id_str[4]   = {0};
    char code_str[7] = {0};
    strncpy(id_str, input_buf, id_len);
    strncpy(code_str, input_buf + id_len, TOTP_CODE_LEN);

    uint16_t id   = (uint16_t)atoi(id_str);
    uint32_t code = (uint32_t)atoi(code_str);

    printf("[core1] attempt: key=%u code=%s\r\n", id, code_str);

    // Load key
    key_record_t key;
    if (!storage_key_get(id, &key) || !key.is_checksum_valid) {
        printf("[core1] key %u: not found or corrupt\r\n", id);
        buzzer_play_fail();
        input_clear();
        return;
    }

    if (!key.is_enabled) {
        printf("[core1] key %u: disabled\r\n", id);
        buzzer_play_fail();
        input_clear();
        return;
    }

    // Verify TOTP
    if (!totp_verify(key.secret, KEY_SECRET_LEN, code)) {
        printf("[core1] key %u: invalid code\r\n", id);
        buzzer_play_fail();
        input_clear();
        return;
    }

    // Access granted
    printf("[core1] key %u (%s): access granted\r\n", id, key.name);
    buzzer_play_success();
    latch_open();
    input_clear();
}

// ---------------------------------------------------------------------------
// Core 1 entry point
// ---------------------------------------------------------------------------

void main1(void) {
    multicore_lockout_victim_init();
    multicore_fifo_push_blocking(1); // signal core 0: ready

    keypad_init();

    while (true) {
        char key = keypad_get_key();

        if (key) {
            switch (key) {
            case 'A':
                buzzer_play_doorbell();
                break;

            case '*':
                input_clear();
                buzzer_beep_short();
                break;

            case '#':
                process_input();
                break;

            case '0':
            case '1':
            case '2':
            case '3':
            case '4':
            case '5':
            case '6':
            case '7':
            case '8':
            case '9':
                if (input_len < INPUT_MAX_LEN) {
                    input_buf[input_len++] = key;
                    buzzer_beep_short();
                }
                // silently ignore if buffer full
                break;

            // B, C, D reserved for future use
            default:
                break;
            }
        }

        sleep_ms(5);
        watchdog_update();
    }
}