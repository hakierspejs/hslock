#include "core1.h"

#include "pico/stdlib.h"
#include "pico/multicore.h"

#include "hardware/buzzer.h"
#include "hardware/keypad.h"
#include "hardware/latch.h"
#include "hardware/led.h"
#include "hardware/sync.h"
#include "hardware/watchdog.h"

#include "shared/door_verify.h"

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

// Clear an abandoned (non-empty, untouched) buffer after this long so a later
// visitor's keystrokes cannot concatenate onto a previous person's partial
// entry (each retained digit removes a factor of 10 from the search space).
#define INPUT_IDLE_TIMEOUT_US (10ull * 1000 * 1000) // ~10 s

static char     input_buf[INPUT_MAX_LEN + 1];
static int      input_len     = 0;
static uint64_t last_input_us = 0; // timestamp of the last accepted keystroke

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

    // Send verify request to core 0 via the door_verify mailbox - NOT the
    // inter-core SIO FIFO, which multicore_lockout_victim_init() (below)
    // claims exclusively for flash_safe_execute's lockout protocol. See
    // shared/door_verify.h.
    static uint32_t next_seq = 0;
    uint32_t        my_seq   = ++next_seq; // never 0 - that's the idle value

    door_verify_mailbox.request_id   = id;
    door_verify_mailbox.request_code = code;
    __dmb(); // request_id/request_code visible before the seq that vouches for them
    door_verify_mailbox.request_seq = my_seq;

    // Wait for verdict, feeding watchdog while waiting
    absolute_time_t deadline = make_timeout_time_ms(FIFO_VERIFY_TIMEOUT_MS);
    while (true) {
        watchdog_update();
        if (door_verify_mailbox.response_seq == my_seq) {
            __dmb(); // seq match visible => response_granted is too
            if (door_verify_mailbox.response_granted) {
                buzzer_play_success();
                buzzer_play_door_open();
                buzzer_on();
                latch_open();
                buzzer_off();
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
        // Drop an abandoned partial entry after the idle window so the next
        // person cannot complete or extend a previous visitor's buffer.
        if (input_len > 0 && time_us_64() - last_input_us > INPUT_IDLE_TIMEOUT_US) {
            input_clear();
        }

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
                    last_input_us          = time_us_64();
                    buzzer_beep_short();
                } else {
                    // Buffer full: emit a DISTINCT tone rather than dropping the
                    // key silently, so "no beep" cannot be used as an oracle to
                    // learn that the buffer has reached its maximum length.
                    last_input_us = time_us_64();
                    buzzer_beep_medium();
                }
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