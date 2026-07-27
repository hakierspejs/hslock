#include "core1.h"

#include "pico/stdlib.h"
#include "pico/multicore.h"

#include "hardware/buzzer.h"
#include "hardware/keypad.h"
#include "hardware/latch.h"
#include "hardware/led.h"
#include "hardware/watchdog.h"

#include "shared/fifo_protocol.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define FIFO_VERIFY_TIMEOUT_MS 2000

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
        buzzer_play_fail();
        input_clear();
        return;
    }

    int id_len = input_len - TOTP_CODE_LEN;

    char id_str[4]   = {0};
    char code_str[7] = {0};
    strncpy(id_str, input_buf, id_len);
    strncpy(code_str, input_buf + id_len, TOTP_CODE_LEN);

    uint16_t id   = (uint16_t)atoi(id_str);
    uint32_t code = (uint32_t)atoi(code_str);

    // Clear sensitive data from stack immediately
    memset(code_str, 0, sizeof(code_str));
    memset(input_buf, 0, sizeof(input_buf));
    input_len = 0;

    // Send verify request to core 0 via FIFO
    multicore_fifo_push_blocking((FIFO_MSG_VERIFY << 24) | (uint32_t)id);
    multicore_fifo_push_blocking(code);

    // Wait for verdict, feeding watchdog while waiting
    absolute_time_t deadline = make_timeout_time_ms(FIFO_VERIFY_TIMEOUT_MS);
    while (true) {
        watchdog_update();
        if (multicore_fifo_rvalid()) {
            uint32_t result = multicore_fifo_pop_blocking();
            if (result == FIFO_RESULT_GRANTED) {
                buzzer_play_success();
                latch_open();
            } else {
                buzzer_play_fail();
            }
            return;
        }
        if (time_reached(deadline)) {
            printf("[core1] verify timeout\r\n");
            buzzer_play_fail();
            return;
        }
        sleep_ms(5);
    }
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