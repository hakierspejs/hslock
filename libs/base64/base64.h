#ifndef BASE64_H
#define BASE64_H

#include <stddef.h>
#include <stdbool.h>

// Encoded length for n input bytes (includes null terminator)
#define BASE64_ENCODED_LEN(n) (((n) + 2) / 3 * 4 + 1)

// Decoded length for n base64 chars (worst case, before padding adjustment)
#define BASE64_DECODED_LEN(n) ((n) / 4 * 3)

// Encode binary data to base64 string.
// out must be at least BASE64_ENCODED_LEN(in_len) bytes.
void base64_encode(const unsigned char *in, size_t in_len, char *out);

// Decode base64 string to binary.
// out must be at least BASE64_DECODED_LEN(in_len) bytes.
// Returns number of decoded bytes, or -1 on invalid input.
int base64_decode(const char *in, size_t in_len, unsigned char *out);

#endif
