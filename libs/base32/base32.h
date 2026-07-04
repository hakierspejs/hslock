#ifndef BASE32_H
#define BASE32_H

#include <stddef.h>
#include <stdint.h>

// Output buffer size for n input bytes (no padding, +1 for null terminator)
// 5 input bits → 1 base32 char, so ceil(n*8/5) + 1
#define BASE32_ENCODED_LEN(n) ((((n) * 8 + 4) / 5) + 1)

// Encode binary to base32 string (no padding).
// out must be at least BASE32_ENCODED_LEN(in_len) bytes.
void base32_encode(const uint8_t *in, size_t in_len, char *out);

#endif
