#ifndef DOOR_VERIFY_H
#define DOOR_VERIFY_H

#include <stdint.h>

// Core 1 <-> core 0 keypad verify handoff.
//
// Deliberately NOT the inter-core SIO FIFO: core1.c calls
// multicore_lockout_victim_init() (required by flash_safe_execute, so core 0
// can safely pause core 1 during flash writes). That installs an exclusive
// SIO_IRQ_PROC1 handler which silently drains and discards every FIFO word
// that isn't its own lockout magic - so a verdict core 0 pushed over the
// FIFO was eaten before core 1's poll loop ever saw it, and the door never
// opened even for a correct code (ISSUES.md C1).
//
// This is a lock-free single-writer/single-reader mailbox instead: core 1
// owns the request_* fields and core 0 owns the response_* fields. Each side
// writes its payload, then a __dmb() (see core1.c/main.c), then bumps its
// sequence number last - so once the other core observes a new sequence
// number, the payload it guards is guaranteed already visible.
//
// request_seq is a monotonically increasing per-attempt nonce, never reused,
// which also closes ISSUES.md C2: core 1 only accepts a response whose
// response_seq echoes the exact request_seq it just sent, so a late/stale
// response for an earlier attempt can never be mistaken for the verdict on
// a later one.
typedef struct {
    volatile uint32_t request_seq; // bumped by core 1 once request_id/request_code are valid
    volatile uint16_t request_id;
    volatile uint32_t request_code;

    volatile uint32_t response_seq;     // set by core 0 to the request_seq it answered
    volatile uint8_t  response_granted; // valid once response_seq == the request_seq it answers
} door_verify_mailbox_t;

extern door_verify_mailbox_t door_verify_mailbox;

#endif
