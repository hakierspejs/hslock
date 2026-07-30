/*
 * Host harness: libs/base64 encode + decode roundtrip.
 * Exercises empty input and every input-length residue mod 3 (0/1/2) so that
 * both the full-triple loop and the two tail branches of base64_encode, plus
 * the padding branches of base64_decode, are covered. Asserts roundtrip
 * identity: decode(encode(x)) == x for all cases.
 */

#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "base64.h"

static void roundtrip(const unsigned char *in, size_t in_len) {
    char encoded[BASE64_ENCODED_LEN(64)];
    assert(BASE64_ENCODED_LEN(in_len) <= sizeof encoded);

    base64_encode(in, in_len, encoded);

    size_t enc_len = strlen(encoded);
    /* Encoded length is always a multiple of 4 (with padding). */
    assert(enc_len % 4 == 0);
    assert(enc_len == (in_len + 2) / 3 * 4);

    unsigned char decoded[64];
    int           dec_len = base64_decode(encoded, enc_len, decoded, sizeof decoded);

    assert(dec_len == (int)in_len);
    assert(memcmp(in, decoded, in_len) == 0);
}

/*
 * M5 regression: base64_decode must never write past out_cap. The one-byte OOB
 * write reported in ISSUES.md is a no-'='-padding final quad whose in_len/4*3
 * output is one byte larger than the caller-sized buffer. Decode into a heap
 * buffer sized EXACTLY one below the natural output length so that, under ASan,
 * the old (capacity-blind) decoder's extra store lands in a redzone and aborts;
 * the fixed decoder must instead return -1 and leave the buffer untouched past
 * out_cap. "AAAAAAAA" (8 chars, no padding) decodes to 6 zero bytes.
 */
static void reject_over_capacity(void) {
    const char *in     = "AAAAAAAA"; /* 8 chars, no '=' -> 6 output bytes */
    size_t      in_len = strlen(in);
    size_t      full   = in_len / 4 * 3; /* == 6 */

    /* Cap one byte short of the full output: previously a 1-byte OOB write. */
    unsigned char *tight = malloc(full - 1);
    assert(tight != NULL);
    assert(base64_decode(in, in_len, tight, full - 1) == -1);
    free(tight); /* ASan verifies nothing was written past tight[full-2]. */

    /* Zero capacity: not even the first byte may be written. */
    unsigned char *zero = malloc(1);
    assert(zero != NULL);
    assert(base64_decode(in, in_len, zero, 0) == -1);
    free(zero);

    /* Exact capacity still succeeds and yields the full decode. */
    unsigned char exact[6];
    assert(base64_decode(in, in_len, exact, sizeof exact) == (int)full);
    for (size_t i = 0; i < full; i++)
        assert(exact[i] == 0x00);
}

int main(void) {
    /* Empty input: encoder emits just a null terminator, decoder returns 0. */
    roundtrip((const unsigned char *)"", 0);

    /* Lengths covering every residue mod 3. */
    const char *samples[] = {
        "f",           /* 1  -> "==" padding branch (single tail byte)   */
        "fo",          /* 2  -> "="  padding branch (two tail bytes)     */
        "foo",         /* 3  -> exact triple, no padding                 */
        "foob",        /* 4                                              */
        "fooba",       /* 5                                              */
        "foobar",      /* 6                                              */
        "hello world", /* 11                                             */
    };
    for (size_t i = 0; i < sizeof samples / sizeof samples[0]; i++) {
        roundtrip((const unsigned char *)samples[i], strlen(samples[i]));
    }

    /* Binary payload including a NUL and high bytes. */
    const unsigned char bin[] = {0x00, 0xff, 0x10, 0x80, 0x7f, 0x01, 0xab, 0xcd};
    roundtrip(bin, sizeof bin);

    /* Invalid inputs: bad length and out-of-alphabet char must be rejected. */
    unsigned char scratch[64];
    assert(base64_decode("abc", 3, scratch, sizeof scratch) == -1);  /* len not mult of 4 */
    assert(base64_decode("ab*d", 4, scratch, sizeof scratch) == -1); /* '*' not in alphabet */

    /* M5: decoded output exceeding out_cap must be rejected, not written OOB. */
    reject_over_capacity();

    /* Non-canonical padding (L6): '=' is only valid as trailing pad in the
     * final quad; the last data symbol's unused low bits must be zero. */
    assert(base64_decode("AA==AAAA", 8, scratch, sizeof scratch) == -1); /* pad in non-final quad */
    assert(base64_decode("AA=A", 4, scratch, sizeof scratch) == -1); /* pad at pos 2 not pos 3 */
    assert(base64_decode("=AAA", 4, scratch, sizeof scratch) == -1); /* pad at pos 0           */
    assert(base64_decode("A=AA", 4, scratch, sizeof scratch) == -1); /* pad at pos 1           */
    assert(base64_decode("AB==", 4, scratch, sizeof scratch) == -1); /* nonzero trailing bits  */
    assert(base64_decode("AB=A", 4, scratch, sizeof scratch) == -1); /* pad at 2 w/o 3, nonzero */
    assert(base64_decode("ABC=", 4, scratch, sizeof scratch) == -1); /* 'C'=2, low 2 bits set  */

    /* Canonical padding still decodes correctly. */
    assert(base64_decode("AA==", 4, scratch, sizeof scratch) == 1 && scratch[0] == 0x00);
    assert(base64_decode("Zg==", 4, scratch, sizeof scratch) == 1 && scratch[0] == 'f'); /* "f"  */
    assert(base64_decode("Zm8=", 4, scratch, sizeof scratch) == 2 &&
           memcmp(scratch, "fo", 2) == 0); /* "fo" */
    assert(base64_decode("AAA=", 4, scratch, sizeof scratch) == 2 && scratch[0] == 0x00 &&
           scratch[1] == 0x00);

    printf("base64 roundtrip OK\n");
    return 0;
}
