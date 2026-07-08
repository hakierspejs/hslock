#ifndef TOTP_H
#define TOTP_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define TOTP_STEP_SECONDS 30
#define TOTP_DIGITS       6
#define TOTP_WINDOW       1 // check T-1, T, T+1

// Compute TOTP code for a given secret and unix timestamp step.
// Returns 6-digit code.
uint32_t totp_at(const uint8_t *secret, size_t secret_len, uint64_t step);

// Verify a 6-digit code against secret and current RTC time.
// Checks T-1, T, T+1 steps.
// Returns true if any step matches.
bool totp_verify(const uint8_t *secret, size_t secret_len, uint32_t code);

#endif
