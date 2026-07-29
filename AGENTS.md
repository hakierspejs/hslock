# AGENTS.md

## Project

HSLock: firmware for a TOTP-based electronic door lock on a Raspberry Pi Pico W
(RP2040 + cyw43 WiFi). C11, built with the Pico SDK + CMake, cross-compiled for
ARM (`arm-none-eabi-gcc`). Core 0 runs networking/console/UI; core 1 owns the keypad
and talks to core 0 over a shared-memory mailbox (`shared/door_verify.h`), not the SDK
FIFO - `multicore_lockout_victim_init()` (needed for `flash_safe_execute`) claims the
FIFO exclusively, so FIFO application messages get silently discarded (see ISSUES.md
C1/C2).
Keys are TOTP secrets (RFC 6238, real mbedtls HMAC-SHA1) stored in littlefs on
flash; the serial console (`serial/console.c`, `serial/commands*.c`) is the
admin/config interface (see `docs/COMMANDS.md`, `docs/KEYPAD.md`).

## Layout

- `main.c`, `core1.c` — entry points for core 0 / core 1
- `hardware/` — GPIO drivers: buzzer, keypad, latch, led, light, clock, watchdog
- `network/` — wifi.c, ntp.c (lwIP-based)
- `serial/` — console tokenizer/dispatcher + command handlers
- `storage/` — littlefs-backed key storage + backup import/export
- `shared/` — totp.c, random.c, core0/core1 door_verify mailbox
- `libs/` — vendored deps: `littlefs` and `qrcodegen` are **git submodules**;
  `base32`/`base64` are vendored in-tree
- `test/` — native host test suite (harnesses, fuzzers, stubs for Pico SDK/lwIP/mbedtls headers)
- `docs/` — BUILD.md, COMMANDS.md, KEYPAD.md

## Building the firmware

Requires the Pico SDK (`PICO_SDK_PATH`), CMake 3.13+, `arm-none-eabi-gcc`, and
submodules checked out (`git submodule update --init --recursive`). Full setup
in `docs/BUILD.md`. If a required tool is missing, install it first (see the
`apt install` commands in `docs/BUILD.md`) rather than skipping the build.

```sh
./build.sh            # configures for pico_w, builds, copies hslock.uf2 to repo root
./build.sh --clean    # remove build/ and hslock.uf2
```

**Never run `./build.sh --flash`, `./build.sh --erase`, or any `picotool`
command that writes to a device unless the user has explicitly asked for that
exact action in their current message.** These touch real hardware: `--flash`
overwrites the firmware on a physical device, and `--erase` permanently wipes
all stored keys/config.

## Host tests (run these to verify logic changes)

The firmware can't run natively, but everything under `hardware/*`, `main.c`,
and real network I/O aside is host-testable via `test/Makefile`, which builds
the real first-party sources against stubs in `test/stub/`. Install missing
dependencies (`build-essential`, `libmbedtls-dev`, `valgrind`, `lcov`, and for
fuzzing `clang` with fuzzer/ASan/UBSan runtime) before assuming a target can't
run.

```sh
make -C test asan        # ASan+UBSan harnesses — primary correctness gate
make -C test valgrind    # same harnesses under valgrind
make -C test coverage    # lcov/genhtml report at test/coverage/html/
make -C test fuzz        # build the 5 libFuzzer harnesses (needs clang)
make -C test fuzz-run    # replay fuzz_console against its committed corpus
make -C test clean
```

See `test/FUZZING.md` for what each of the five fuzzers (`fuzz_console`,
`fuzz_backup`, `fuzz_totp`, `fuzz_storage`, `fuzz_ntp`) drives, and the
documented coverage plateau / known-unreachable residual lines before treating
a coverage gap as a bug.

CI (`.github/workflows/`) runs `ci.yml` (lint), `test.yml` (asan + valgrind +
coverage), and `pages.yml` (publishes the coverage report) — mirror these
locally before assuming a change is clean.

## Lint / format

```sh
./ci                     # clang-format -i on all *.c/*.h (excl. libs/), shellcheck on scripts
./ci --action=check      # dry-run form CI uses: non-zero exit on diff
```

Style is set by `.clang-format` (LLVM base, 4-space indent, K&R braces,
100-col, `SortIncludes: false` — include order is intentional, don't reorder
manually or let a formatter do it). Don't hand-reformat code outside what
clang-format produces.

## Notes / gotchas

- `libs/littlefs` and `libs/qrcodegen` are submodules — if they appear empty,
  run `git submodule update --init --recursive` before building.
- `serial/commands*.c` handlers are dispatched by `serial/commands.c`; new
  commands need a handler + dispatch table entry + a `docs/COMMANDS.md` entry.
- Untrusted-input parsing (console bytes, backup blobs, NTP datagrams) is the
  fuzzed surface — changes there should come with a `test/fuzz_*.c` seed or
  harness update, not just the asan/valgrind pass.
- No `LICENSE` file is currently present in the repo — ask before assuming a
  license for redistribution purposes.
