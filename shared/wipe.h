#ifndef WIPE_H
#define WIPE_H

#include <stddef.h>

// Securely zero a buffer that held key material / secrets.
//
// A plain memset() on a buffer that is not read again is a dead store the
// compiler is free to elide, leaving secrets resident in BSS/stack. Writing
// through a `volatile` pointer forces the store to happen, so seeds don't
// linger after a command handler returns.
static inline void secure_wipe(void *p, size_t n) {
    volatile unsigned char *vp = (volatile unsigned char *)p;
    while (n--) {
        *vp++ = 0;
    }
}

#endif
