#ifndef KEYPAD_H
#define KEYPAD_H

// 4x4 matrix keypad
// Rows: GPIO 6-9  (outputs)
// Cols: GPIO 10-13 (inputs, internal pull-up)
//
// Layout:
//  1 2 3 A
//  4 5 6 B
//  7 8 9 C
//  * 0 # D

#define KEYPAD_ROWS        4
#define KEYPAD_COLS        4
#define KEYPAD_ROW_BASE    10
#define KEYPAD_COL_BASE    6
#define KEYPAD_DEBOUNCE_MS 20

// Initialize GPIO pins
void keypad_init(void);

// Non-blocking. Returns key char on new press, 0 if none.
// Call in a tight loop - handles debounce internally.
char keypad_get_key(void);

#endif