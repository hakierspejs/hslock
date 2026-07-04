#include "base64.h"

static const char B64_CHARS[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

void base64_encode(const unsigned char *in, size_t in_len, char *out) {
    size_t i = 0, j = 0;

    while (i + 2 < in_len) {
        out[j++] = B64_CHARS[(in[i]     >> 2)];
        out[j++] = B64_CHARS[(in[i]     &  0x03) << 4 | (in[i+1] >> 4)];
        out[j++] = B64_CHARS[(in[i+1]   &  0x0F) << 2 | (in[i+2] >> 6)];
        out[j++] = B64_CHARS[(in[i+2]   &  0x3F)];
        i += 3;
    }

    if (i < in_len) {
        out[j++] = B64_CHARS[(in[i] >> 2)];
        if (i + 1 < in_len) {
            out[j++] = B64_CHARS[(in[i] & 0x03) << 4 | (in[i+1] >> 4)];
            out[j++] = B64_CHARS[(in[i+1] & 0x0F) << 2];
        } else {
            out[j++] = B64_CHARS[(in[i] & 0x03) << 4];
            out[j++] = '=';
        }
        out[j++] = '=';
    }

    out[j] = '\0';
}

static int b64_val(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+')              return 62;
    if (c == '/')              return 63;
    if (c == '=')              return 0;
    return -1;
}

int base64_decode(const char *in, size_t in_len, unsigned char *out) {
    if (in_len % 4 != 0) return -1;

    int out_len = 0;

    for (size_t i = 0; i < in_len; i += 4) {
        int a = b64_val(in[i]);
        int b = b64_val(in[i+1]);
        int c = b64_val(in[i+2]);
        int d = b64_val(in[i+3]);

        if (a < 0 || b < 0 || c < 0 || d < 0) return -1;

        out[out_len++] = (a << 2) | (b >> 4);
        if (in[i+2] != '=') out[out_len++] = (b << 4) | (c >> 2);
        if (in[i+3] != '=') out[out_len++] = (c << 6) | d;
    }

    return out_len;
}
