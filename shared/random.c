#include "random.h"
#include <string.h>

void generate_secret(uint8_t *out) {
    // 20 bytes = 2.5 x uint64_t - fill in 64-bit chunks
    uint64_t r0 = get_rand_64();
    uint64_t r1 = get_rand_64();
    uint64_t r2 = get_rand_64(); // only 4 bytes used from this one

    memcpy(out, &r0, 8);
    memcpy(out + 8, &r1, 8);
    memcpy(out + 16, &r2, 4);
}