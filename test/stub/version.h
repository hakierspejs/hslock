#ifndef STUB_VERSION_H
#define STUB_VERSION_H

/* Host stub for the CMake-generated version.h consumed by `status`. */

#define GIT_HASH   "hoststub"
#define GIT_DIRTY  0
#define BUILD_DATE "host"

/* 2023-01-01 00:00:00 UTC - comfortably below the 1700000000-era timestamps
 * network/ntp.c's tests exercise, and within NTP_SANE_MAX_AGE_S (20 years) of
 * them, so ntp_recv_cb()'s sanity band doesn't reject the fuzz/unit harnesses'
 * fixed test vectors. */
#define BUILD_UNIX_TIME 1672531200UL

#endif
