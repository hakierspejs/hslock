#ifndef STUB_PICO_UNIQUE_ID_H
#define STUB_PICO_UNIQUE_ID_H

/*
 * Host stub for <pico/unique_id.h>: the per-board flash unique-id read used by
 * the `status` command. On real hardware this is the 64-bit id of the on-board
 * flash chip; here we only need the type, the size macro and the prototype.
 */

#include <stdint.h>

#define PICO_UNIQUE_BOARD_ID_SIZE_BYTES 8

typedef struct {
    uint8_t id[PICO_UNIQUE_BOARD_ID_SIZE_BYTES];
} pico_unique_board_id_t;

void pico_get_unique_board_id(pico_unique_board_id_t *id_out);

#endif
