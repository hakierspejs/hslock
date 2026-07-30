#ifndef RANDOM_H
#define RANDOM_H

#include "pico/rand.h"

#include <stddef.h>
#include <stdint.h>

// Fill `out` with `len` cryptographically-derived random bytes. See
// shared/random.c for the entropy-conditioning rationale (get_rand_64 is
// pico_rand's non-cryptographic xoroshiro128**, not a CSPRNG).
void generate_secret(uint8_t *out, size_t len);

#endif
