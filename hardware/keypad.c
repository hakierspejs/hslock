#include "keypad.h"
#include "pico/stdlib.h"
#include "pico/time.h"

static const char KEY_MAP[KEYPAD_ROWS][KEYPAD_COLS] = {
    {'1', '2', '3', 'A'},
    {'4', '5', '6', 'B'},
    {'7', '8', '9', 'C'},
    {'*', '0', '#', 'D'}
};

void keypad_init(void) {
    // Rows: outputs, default HIGH
    for (int r = 0; r < KEYPAD_ROWS; r++) {
        uint pin = KEYPAD_ROW_BASE + r;
        gpio_init(pin);
        gpio_set_dir(pin, GPIO_OUT);
        gpio_put(pin, true);
    }

    // Cols: inputs with internal pull-up
    // Key press pulls col LOW when its row is driven LOW
    for (int c = 0; c < KEYPAD_COLS; c++) {
        uint pin = KEYPAD_COL_BASE + c;
        gpio_init(pin);
        gpio_set_dir(pin, GPIO_IN);
        gpio_pull_up(pin);
    }
}

// Scan matrix once, return currently pressed key or 0
static char scan_raw(void) {
    for (int r = 0; r < KEYPAD_ROWS; r++) {
        // Pull this row LOW, keep others HIGH
        gpio_put(KEYPAD_ROW_BASE + r, false);
        sleep_us(10); // settle time for GPIO + RC

        for (int c = 0; c < KEYPAD_COLS; c++) {
            if (!gpio_get(KEYPAD_COL_BASE + c)) {
                gpio_put(KEYPAD_ROW_BASE + r, true);
                return KEY_MAP[r][c];
            }
        }

        gpio_put(KEYPAD_ROW_BASE + r, true);
    }
    return 0;
}

char keypad_get_key(void) {
    static char             last_raw    = 0;
    static char             last_stable = 0;
    static absolute_time_t  stable_at   = {0};

    char raw = scan_raw();

    if (raw != last_raw) {
        // Reading changed — restart debounce timer
        last_raw   = raw;
        stable_at  = make_timeout_time_ms(KEYPAD_DEBOUNCE_MS);
        return 0;
    }

    // Still within debounce window — ignore
    if (!time_reached(stable_at)) {
        return 0;
    }

    // Stable reading — fire only on press transition (not hold, not release)
    if (raw != 0 && raw != last_stable) {
        last_stable = raw;
        return raw;
    }

    if (raw == 0) {
        last_stable = 0;
    }

    return 0;
}