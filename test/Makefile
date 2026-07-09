# Host test suite for hslock.
#
# The firmware is cross-compiled for the RP2040 and cannot run natively, but
# the hardware-independent logic can be built as a native Linux ELF and tested.
# This Makefile builds each harness against the REAL first-party sources plus
# cheap host stubs (test/stub/), and runs them under three modes:
#
#   make asan      ASan + UBSan, aborts on any error (CI gate)
#   make valgrind  plain build under valgrind, fails on error/leak
#   make coverage  --coverage build of the harness-exercised modules, run,
#                  lcov + genhtml report (branch coverage) into coverage/html/
#   make clean
#
# Invoke from the repo root as `make -C test <target>`.

# Repo layout, relative to this Makefile (test/).
ROOT      := ..
BUILD     := build
COV_DIR   := coverage

# Absolute repo root, used to filter coverage to first-party sources WITHOUT
# assuming the checkout dir is literally named "hslock". Coverage is built with
# -fprofile-abs-path, so the .info tracefiles carry absolute source paths;
# extracting "$(REPO_ROOT)/*" keeps first-party files regardless of dir name.
# Prefer git's toplevel; fall back to the parent dir for non-git checkouts.
REPO_ROOT := $(or $(shell git -C $(CURDIR) rev-parse --show-toplevel 2>/dev/null),$(abspath $(ROOT)))

# Real first-party / third-party sources exercised by the harnesses.
SHARED    := $(ROOT)/shared
BASE32    := $(ROOT)/libs/base32
BASE64    := $(ROOT)/libs/base64
QRCODEGEN := $(ROOT)/libs/qrcodegen/c

# Include roots. `-I..` resolves module-qualified includes ("hardware/buzzer.h",
# "storage/storage.h", ...); the per-module dirs resolve bare includes
# ("buzzer.h", "base32.h", ...); `-Istub` supplies host shims for Pico-SDK
# headers; `-I$(ROOT)/libs/littlefs` supplies the real lfs_util.h (lfs_crc).
INCLUDES  := -Istub -I$(ROOT) \
             -I$(ROOT)/hardware -I$(ROOT)/network -I$(ROOT)/serial \
             -I$(ROOT)/storage -I$(SHARED) -I$(BASE32) -I$(BASE64) \
             -I$(QRCODEGEN) -I$(ROOT)/libs/littlefs

# Per-harness source lists (harness + its real dependencies).
SECRET_QR_SRCS := harness_secret_qr.c $(SHARED)/random.c $(BASE32)/base32.c \
                  $(QRCODEGEN)/qrcodegen.c
BASE64_SRCS    := harness_base64.c $(BASE64)/base64.c

# TOTP harness needs the REAL HMAC-SHA1, so it builds against the system mbedtls
# (libmbedtls-dev) rather than the compile-only stub/mbedtls/md.h. `-idirafter
# stub` keeps the hardware/rtc.h stub reachable while letting the real
# <mbedtls/md.h> (a system dir) win the search for totp.c's HMAC include.
# CI: the test job must apt-install `libmbedtls-dev` for this to build.
TOTP_SRCS     := harness_totp.c $(SHARED)/totp.c
TOTP_INCLUDES := -I$(ROOT) -I$(ROOT)/hardware -I$(SHARED) -idirafter stub
TOTP_LIBS     := -lmbedcrypto

# Storage harness: storage.c + backup.c + the REAL littlefs (lfs.c/lfs_util.c),
# run against a RAM-backed XIP window the harness maps in (see harness_storage.c).
# LFS_NO_MALLOC/LFS_NO_DEBUG mirror the firmware's CMakeLists so lfs.h/lfs_util.h
# match. The shared INCLUDES already resolve stub/, storage/ and libs/littlefs/.
STORAGE_SRCS := harness_storage.c $(ROOT)/storage/storage.c $(ROOT)/storage/backup.c \
                $(ROOT)/libs/littlefs/lfs.c $(ROOT)/libs/littlefs/lfs_util.c
STORAGE_DEFS := -DLFS_NO_MALLOC -DLFS_NO_DEBUG

# Commands harness: links serial/commands.c (the dispatcher) against SPY handler
# stubs + a stub buzzer, all inside harness_commands.c. No real key/system/
# network/backup handlers are pulled in — the harness drives commands_dispatch()
# and asserts routing / admin gating / argc validation / unknown handling. The
# shared INCLUDES resolve stub/pico + hardware/buzzer.h.
COMMANDS_SRCS := harness_commands.c $(ROOT)/serial/commands.c

# Whole-codebase coverage: EVERY first-party .c, so the report lists the whole
# firmware (the harness-exercised modules at their real %, the rest at an honest
# 0%). These are compiled instrumented but NOT linked or run — a .gcno alone is
# enough for a file to appear in the report. The terse host stubs under stub/
# stand in for the pico-sdk / cyw43 / lwip / mbedtls headers they pull.
WHOLE_SRCS := $(ROOT)/hardware/buzzer.c $(ROOT)/hardware/clock.c \
              $(ROOT)/hardware/keypad.c $(ROOT)/hardware/latch.c \
              $(ROOT)/hardware/light.c $(ROOT)/main.c \
              $(ROOT)/network/ntp.c $(ROOT)/network/wifi.c \
              $(ROOT)/serial/commands.c $(ROOT)/serial/commands_backup.c \
              $(ROOT)/serial/commands_keys.c $(ROOT)/serial/commands_network.c \
              $(ROOT)/serial/commands_system.c $(ROOT)/serial/console.c \
              $(ROOT)/shared/random.c $(ROOT)/shared/totp.c \
              $(ROOT)/storage/backup.c $(ROOT)/storage/storage.c

# LittleFS build knobs, mirrored from the top-level CMakeLists so storage.c and
# backup.c see the same lfs.h / lfs_util.h configuration the firmware does.
WHOLE_DEFS := -DLFS_NO_MALLOC -DLFS_NO_DEBUG

CC       := gcc
CSTD     := -std=c11
WARN     := -Wall -Wextra

ASAN_FLAGS := -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=all
VG_FLAGS   := -g -O0
COV_FLAGS  := -g -O0 --coverage -fprofile-abs-path

# lcov 2.x: enable branch coverage everywhere; tolerate the benign
# inconsistencies host coverage of firmware naturally produces.
#
# The valid --ignore-errors category set differs per sub-tool and per lcov
# build: Ubuntu 24.04's lcov 2.0 genhtml rejects 'gcov'/'graph'/'range'
# (there is no gcov phase in genhtml), while lcov/geninfo accept 'gcov' but
# reject 'range'. So keep two lists: the capture/combine list (lcov+geninfo)
# may carry 'gcov'; the genhtml list must not. Every category below exists in
# lcov 2.0.0, so this is robust across lcov 1.x/2.x on Debian and Ubuntu.
LCOV_RC        := --rc branch_coverage=1
LCOV_IGNORE    := --ignore-errors unused,empty,mismatch,inconsistent,source,gcov,negative
GENHTML_IGNORE := --ignore-errors unused,empty,mismatch,inconsistent,source,negative
LCOV           := lcov $(LCOV_RC) $(LCOV_IGNORE)

.PHONY: all asan valgrind coverage clean check-submodules

all: asan

# Fail early with a clear message if the git submodules aren't checked out
# (fresh clones have empty libs/qrcodegen and libs/littlefs).
check-submodules:
	@test -f $(QRCODEGEN)/qrcodegen.c && test -f $(ROOT)/libs/littlefs/lfs_util.h || { \
	    echo "ERROR: git submodules not initialized (libs/qrcodegen and/or libs/littlefs are empty)." >&2; \
	    echo "       run: git submodule update --init --recursive" >&2; \
	    exit 1; }

# --- ASan + UBSan -----------------------------------------------------------
asan: check-submodules $(BUILD)/asan_secret_qr $(BUILD)/asan_base64 $(BUILD)/asan_totp \
      $(BUILD)/asan_storage $(BUILD)/asan_commands
	@echo "== running asan/ubsan harnesses =="
	$(BUILD)/asan_secret_qr
	$(BUILD)/asan_base64
	$(BUILD)/asan_totp
	$(BUILD)/asan_storage
	$(BUILD)/asan_commands
	@echo "== asan OK =="

$(BUILD)/asan_secret_qr: $(SECRET_QR_SRCS) | $(BUILD)
	$(CC) $(CSTD) $(WARN) $(ASAN_FLAGS) $(INCLUDES) $(SECRET_QR_SRCS) -o $@

$(BUILD)/asan_base64: $(BASE64_SRCS) | $(BUILD)
	$(CC) $(CSTD) $(WARN) $(ASAN_FLAGS) $(INCLUDES) $(BASE64_SRCS) -o $@

$(BUILD)/asan_totp: $(TOTP_SRCS) | $(BUILD)
	$(CC) $(CSTD) $(WARN) $(ASAN_FLAGS) $(TOTP_INCLUDES) $(TOTP_SRCS) $(TOTP_LIBS) -o $@

$(BUILD)/asan_storage: $(STORAGE_SRCS) | $(BUILD)
	$(CC) $(CSTD) $(WARN) $(ASAN_FLAGS) $(STORAGE_DEFS) $(INCLUDES) $(STORAGE_SRCS) -o $@

$(BUILD)/asan_commands: $(COMMANDS_SRCS) | $(BUILD)
	$(CC) $(CSTD) $(WARN) $(ASAN_FLAGS) $(INCLUDES) $(COMMANDS_SRCS) -o $@

# --- Valgrind ---------------------------------------------------------------
valgrind: check-submodules $(BUILD)/vg_secret_qr $(BUILD)/vg_base64 $(BUILD)/vg_totp \
          $(BUILD)/vg_storage $(BUILD)/vg_commands
	@echo "== running harnesses under valgrind =="
	valgrind --error-exitcode=99 --leak-check=full -q $(BUILD)/vg_secret_qr
	valgrind --error-exitcode=99 --leak-check=full -q $(BUILD)/vg_base64
	valgrind --error-exitcode=99 --leak-check=full -q $(BUILD)/vg_totp
	valgrind --error-exitcode=99 --leak-check=full -q $(BUILD)/vg_storage
	valgrind --error-exitcode=99 --leak-check=full -q $(BUILD)/vg_commands
	@echo "== valgrind OK =="

$(BUILD)/vg_secret_qr: $(SECRET_QR_SRCS) | $(BUILD)
	$(CC) $(CSTD) $(WARN) $(VG_FLAGS) $(INCLUDES) $(SECRET_QR_SRCS) -o $@

$(BUILD)/vg_base64: $(BASE64_SRCS) | $(BUILD)
	$(CC) $(CSTD) $(WARN) $(VG_FLAGS) $(INCLUDES) $(BASE64_SRCS) -o $@

$(BUILD)/vg_totp: $(TOTP_SRCS) | $(BUILD)
	$(CC) $(CSTD) $(WARN) $(VG_FLAGS) $(TOTP_INCLUDES) $(TOTP_SRCS) $(TOTP_LIBS) -o $@

$(BUILD)/vg_storage: $(STORAGE_SRCS) | $(BUILD)
	$(CC) $(CSTD) $(WARN) $(VG_FLAGS) $(STORAGE_DEFS) $(INCLUDES) $(STORAGE_SRCS) -o $@

$(BUILD)/vg_commands: $(COMMANDS_SRCS) | $(BUILD)
	$(CC) $(CSTD) $(WARN) $(VG_FLAGS) $(INCLUDES) $(COMMANDS_SRCS) -o $@

# --- Coverage (lcov/genhtml HTML report for the WHOLE first-party codebase) --
# Build & run the real harnesses with --coverage (producing .gcda), AND compile
# every first-party .c instrumented but unlinked (compile-only, producing .gcno
# only). A --initial capture turns the whole .gcno set into a 0% baseline; the
# run capture carries the executed lines; merging the two yields a report that
# spans the whole firmware — the harness-exercised modules (base32/base64/
# random/...) at their real %, every other first-party file at an honest 0%.
# Everything outside the first-party tree (vendored libs, the harnesses, /usr)
# is filtered out.
coverage: check-submodules | $(COV_DIR)/obj
	@echo "== coverage: building + running harnesses =="
	$(CC) $(CSTD) $(WARN) $(COV_FLAGS) $(INCLUDES) $(SECRET_QR_SRCS) \
	    -o $(COV_DIR)/obj/cov_secret_qr
	$(CC) $(CSTD) $(WARN) $(COV_FLAGS) $(INCLUDES) $(BASE64_SRCS) \
	    -o $(COV_DIR)/obj/cov_base64
	$(CC) $(CSTD) $(WARN) $(COV_FLAGS) $(TOTP_INCLUDES) $(TOTP_SRCS) $(TOTP_LIBS) \
	    -o $(COV_DIR)/obj/cov_totp
	$(CC) $(CSTD) $(WARN) $(COV_FLAGS) $(STORAGE_DEFS) $(INCLUDES) $(STORAGE_SRCS) \
	    -o $(COV_DIR)/obj/cov_storage
	$(CC) $(CSTD) $(WARN) $(COV_FLAGS) $(INCLUDES) $(COMMANDS_SRCS) \
	    -o $(COV_DIR)/obj/cov_commands
	@echo "== coverage: running instrumented harnesses =="
	$(COV_DIR)/obj/cov_secret_qr
	$(COV_DIR)/obj/cov_base64
	$(COV_DIR)/obj/cov_totp
	$(COV_DIR)/obj/cov_storage
	$(COV_DIR)/obj/cov_commands
	@echo "== coverage: compiling whole first-party codebase (compile-only) =="
	@for src in $(WHOLE_SRCS); do \
	    obj=$(COV_DIR)/obj/whole_$$(echo $$src | tr '/.' '__').o; \
	    echo "  CC $$src"; \
	    $(CC) $(CSTD) $(WARN) $(COV_FLAGS) $(WHOLE_DEFS) $(INCLUDES) \
	        -c $$src -o $$obj || exit 1; \
	done
	@echo "== coverage: capturing baseline (all .gcno at 0%) + run data =="
	$(LCOV) --capture --initial --directory $(COV_DIR)/obj \
	    --output-file $(COV_DIR)/baseline.info
	$(LCOV) --capture --directory $(COV_DIR)/obj \
	    --output-file $(COV_DIR)/run.info
	$(LCOV) --add-tracefile $(COV_DIR)/baseline.info \
	    --add-tracefile $(COV_DIR)/run.info \
	    --output-file $(COV_DIR)/merged.info
	@echo "== coverage: filtering to first-party sources =="
	$(LCOV) --extract $(COV_DIR)/merged.info '$(REPO_ROOT)/*' \
	    --output-file $(COV_DIR)/first.info
	$(LCOV) --remove $(COV_DIR)/first.info \
	    '*/libs/qrcodegen/*' '*/libs/littlefs/*' '*/test/*' '/usr/*' \
	    --output-file $(COV_DIR)/coverage.info
	@echo "== coverage: generating HTML report =="
	genhtml $(LCOV_RC) $(GENHTML_IGNORE) --branch-coverage \
	    --legend --title "hslock host coverage" \
	    --output-directory $(COV_DIR)/html $(COV_DIR)/coverage.info
	@echo "== coverage summary =="
	$(LCOV) --summary $(COV_DIR)/coverage.info
	@echo "== coverage OK: report at $(COV_DIR)/html/index.html =="

$(BUILD):
	mkdir -p $(BUILD)

$(COV_DIR)/obj:
	mkdir -p $(COV_DIR)/obj

clean:
	rm -rf $(BUILD) $(COV_DIR) *.gcno *.gcda
