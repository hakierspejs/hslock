#ifndef FIFO_PROTOCOL_H
#define FIFO_PROTOCOL_H

#include <stdint.h>

// Core 1 → Core 0
// Word 1: (FIFO_MSG_VERIFY << 24) | (key_id & 0xFFFF)
// Word 2: totp_code
#define FIFO_MSG_VERIFY 0x01U

// Core 0 → Core 1
#define FIFO_RESULT_GRANTED 0x01U
#define FIFO_RESULT_DENIED  0x00U

#endif