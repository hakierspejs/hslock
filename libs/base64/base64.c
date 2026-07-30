#include "base64.h"

static const char B64_CHARS[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

void base64_encode(const unsigned char *in, size_t in_len, char *out) {
    size_t i = 0, j = 0;

    while (i + 2 < in_len) {
        out[j++] = B64_CHARS[(in[i] >> 2)];
        out[j++] = B64_CHARS[(in[i] & 0x03) << 4 | (in[i + 1] >> 4)];
        out[j++] = B64_CHARS[(in[i + 1] & 0x0F) << 2 | (in[i + 2] >> 6)];
        out[j++] = B64_CHARS[(in[i + 2] & 0x3F)];
        i += 3;
    }

    if (i < in_len) {
        out[j++] = B64_CHARS[(in[i] >> 2)];
        if (i + 1 < in_len) {
            out[j++] = B64_CHARS[(in[i] & 0x03) << 4 | (in[i + 1] >> 4)];
            out[j++] = B64_CHARS[(in[i + 1] & 0x0F) << 2];
        } else {
            out[j++] = B64_CHARS[(in[i] & 0x03) << 4];
            out[j++] = '=';
        }
        out[j++] = '=';
    }

    out[j] = '\0';
}

static int b64_val(char c) {
    if (c >= 'A' && c <= 'Z')
        return c - 'A';
    if (c >= 'a' && c <= 'z')
        return c - 'a' + 26;
    if (c >= '0' && c <= '9')
        return c - '0' + 52;
    if (c == '+')
        return 62;
    if (c == '/')
        return 63;
    if (c == '=')
        return 0;
    return -1;
}

int base64_decode(const char *in, size_t in_len, unsigned char *out, size_t out_cap) {
    if (in_len % 4 != 0)
        return -1;

    size_t out_len = 0;

    for (size_t i = 0; i < in_len; i += 4) {
        int is_final = (i + 4 == in_len);
        int pad2     = (in[i + 2] == '=');
        int pad3     = (in[i + 3] == '=');

        /* '=' is only ever valid as trailing padding in the FINAL quad, at
         * position 3 (one pad) or positions 2 and 3 (two pads). A pad in any
         * non-final quad, or at position 2 without one at position 3, is
         * non-canonical and rejected. */
        if ((pad2 || pad3) && !is_final)
            return -1;
        if (pad2 && !pad3)
            return -1;

        /* '=' in positions 0 or 1 is never valid (b64_val('=') is 0, so the
         * generic value check below would not catch it). */
        if (in[i] == '=' || in[i + 1] == '=')
            return -1;

        int a = b64_val(in[i]);
        int b = b64_val(in[i + 1]);
        if (a < 0 || b < 0)
            return -1;

        // Every write is bounded by the caller-supplied capacity: with no '=' in
        // the final quad the decoder emits in_len/4*3 bytes, which the caller may
        // have under-sized by one (see ISSUES.md M5). Reject rather than write
        // past the buffer.
        if (pad2) {
            /* "XX==": one output byte; the low 4 bits of the last data symbol
             * (b) are unused and must be zero for canonical input. */
            if (b & 0x0F)
                return -1;
            if (out_len >= out_cap)
                return -1;
            out[out_len++] = (a << 2) | (b >> 4);
        } else if (pad3) {
            /* "XXX=": two output bytes; the low 2 bits of the last data symbol
             * (c) are unused and must be zero for canonical input. */
            int c = b64_val(in[i + 2]);
            if (c < 0)
                return -1;
            if (c & 0x03)
                return -1;
            if (out_len >= out_cap)
                return -1;
            out[out_len++] = (a << 2) | (b >> 4);
            if (out_len >= out_cap)
                return -1;
            out[out_len++] = (b << 4) | (c >> 2);
        } else {
            int c = b64_val(in[i + 2]);
            int d = b64_val(in[i + 3]);
            if (c < 0 || d < 0)
                return -1;
            if (out_len >= out_cap)
                return -1;
            out[out_len++] = (a << 2) | (b >> 4);
            if (out_len >= out_cap)
                return -1;
            out[out_len++] = (b << 4) | (c >> 2);
            if (out_len >= out_cap)
                return -1;
            out[out_len++] = (c << 6) | d;
        }
    }

    return (int)out_len;
}
