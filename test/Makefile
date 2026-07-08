# Host test suite for hslock.
#
# The firmware is cross-compiled for the RP2040 and cannot run natively, but
# the hardware-independent logic can be built as a native Linux ELF and tested.
# This Makefile builds each harness against the REAL first-party sources plus
# cheap host stubs (test/stub/), and runs them under three modes:
#
#   make asan      ASan + UBSan, aborts on any error (CI gate)
#   make valgrind  plain build under valgrind, fails on error/leak
#   make coverage  --coverage build over the WHOLE first-party codebase, run,
#                  lcov + genhtml report (branch coverage) into coverage/html/
#   make clean
#
# Invoke from the repo root as `make -C test <target>`.

# Repo layout, relative to this Makefile (test/).
ROOT      := ..
BUILD     := build
COV_DIR   := coverage

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

# ---------------------------------------------------------------------------
# Whole-codebase coverage denominator (blind-spot visibility)
# ---------------------------------------------------------------------------
# COV_ZERO_SRCS: first-party firmware .c files that COMPILE host-side with the
# stubs above but are NOT exercised by any harness. Compiling them emits a
# .gcno (but no .gcda), so lcov reports their true 0% coverage instead of
# silently dropping them - blind spots stay visible in the report.
COV_ZERO_SRCS := $(ROOT)/hardware/buzzer.c $(ROOT)/hardware/clock.c \
                 $(ROOT)/hardware/keypad.c $(ROOT)/hardware/latch.c \
                 $(ROOT)/hardware/light.c $(ROOT)/main.c \
                 $(ROOT)/serial/commands.c $(ROOT)/serial/commands_backup.c \
                 $(ROOT)/serial/commands_keys.c $(ROOT)/serial/commands_network.c \
                 $(ROOT)/serial/commands_system.c $(ROOT)/serial/console.c \
                 $(ROOT)/storage/backup.c
#
# BLIND SPOTS - first-party files that CANNOT be host-compiled without heavy
# mocks (real Pico SDK / cyw43 / lwip / littlefs block device). They are
# enumerated (with reason) by coverage-summary.sh and counted as 0% toward the
# whole-codebase denominator - never silently excluded:
#   network/ntp.c    - pico/cyw43_arch.h + lwip/udp.h + lwip/dns.h
#   network/wifi.c   - pico/cyw43_arch.h
#   storage/storage.c- pico/flash.h + hardware/flash.h + hardware/sync.h + lfs cfg

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

.PHONY: all asan valgrind coverage clean

all: asan

# --- ASan + UBSan -----------------------------------------------------------
asan: $(BUILD)/asan_secret_qr $(BUILD)/asan_base64
	@echo "== running asan/ubsan harnesses =="
	$(BUILD)/asan_secret_qr
	$(BUILD)/asan_base64
	@echo "== asan OK =="

$(BUILD)/asan_secret_qr: $(SECRET_QR_SRCS) | $(BUILD)
	$(CC) $(CSTD) $(WARN) $(ASAN_FLAGS) $(INCLUDES) $(SECRET_QR_SRCS) -o $@

$(BUILD)/asan_base64: $(BASE64_SRCS) | $(BUILD)
	$(CC) $(CSTD) $(WARN) $(ASAN_FLAGS) $(INCLUDES) $(BASE64_SRCS) -o $@

# --- Valgrind ---------------------------------------------------------------
valgrind: $(BUILD)/vg_secret_qr $(BUILD)/vg_base64
	@echo "== running harnesses under valgrind =="
	valgrind --error-exitcode=99 --leak-check=full -q $(BUILD)/vg_secret_qr
	valgrind --error-exitcode=99 --leak-check=full -q $(BUILD)/vg_base64
	@echo "== valgrind OK =="

$(BUILD)/vg_secret_qr: $(SECRET_QR_SRCS) | $(BUILD)
	$(CC) $(CSTD) $(WARN) $(VG_FLAGS) $(INCLUDES) $(SECRET_QR_SRCS) -o $@

$(BUILD)/vg_base64: $(BASE64_SRCS) | $(BUILD)
	$(CC) $(CSTD) $(WARN) $(VG_FLAGS) $(INCLUDES) $(BASE64_SRCS) -o $@

# --- Coverage (whole-codebase denominator + lcov/genhtml HTML report) -------
# 1. Build & run the real harnesses (produce .gcda -> true coverage).
# 2. Compile every other host-compilable first-party .c (produce .gcno only
#    -> true 0%), so untested files are visible as blind spots.
# 3. lcov: --initial baseline (all .gcno at 0%) + post-run capture, combined,
#    filtered to first-party only, rendered to coverage/html/ with branches.
coverage: | $(COV_DIR)/obj
	@echo "== coverage: building + running harnesses =="
	$(CC) $(CSTD) $(WARN) $(COV_FLAGS) $(INCLUDES) $(SECRET_QR_SRCS) \
	    -o $(COV_DIR)/obj/cov_secret_qr
	$(CC) $(CSTD) $(WARN) $(COV_FLAGS) $(INCLUDES) $(BASE64_SRCS) \
	    -o $(COV_DIR)/obj/cov_base64
	@echo "== coverage: compiling untested first-party files (0% blind spots) =="
	@for f in $(COV_ZERO_SRCS); do \
	    o=$(COV_DIR)/obj/zero_$$(basename $$f .c).o; \
	    echo "  CC (gcno) $$f"; \
	    $(CC) $(CSTD) $(WARN) $(COV_FLAGS) $(INCLUDES) -c $$f -o $$o || exit 1; \
	done
	@echo "== coverage: capturing initial (zero) baseline =="
	$(LCOV) --capture --initial --directory $(COV_DIR)/obj \
	    --output-file $(COV_DIR)/base.info
	@echo "== coverage: running instrumented harnesses =="
	$(COV_DIR)/obj/cov_secret_qr
	$(COV_DIR)/obj/cov_base64
	@echo "== coverage: capturing run data =="
	$(LCOV) --capture --directory $(COV_DIR)/obj \
	    --output-file $(COV_DIR)/run.info
	$(LCOV) --add-tracefile $(COV_DIR)/base.info \
	    --add-tracefile $(COV_DIR)/run.info --output-file $(COV_DIR)/total.info
	@echo "== coverage: filtering to first-party sources =="
	$(LCOV) --extract $(COV_DIR)/total.info '*/hslock/*' \
	    --output-file $(COV_DIR)/first.info
	$(LCOV) --remove $(COV_DIR)/first.info \
	    '*/libs/qrcodegen/*' '*/libs/littlefs/*' '*/test/*' '/usr/*' \
	    --output-file $(COV_DIR)/coverage.info
	@echo "== coverage: generating HTML report =="
	genhtml $(LCOV_RC) $(GENHTML_IGNORE) --branch-coverage \
	    --legend --title "hslock host coverage" \
	    --output-directory $(COV_DIR)/html $(COV_DIR)/coverage.info
	@echo "== coverage: generating summary (txt + json) =="
	./coverage-summary.sh $(COV_DIR)/coverage.info $(COV_DIR)
	@echo "== coverage OK: report at $(COV_DIR)/html/index.html =="

$(BUILD):
	mkdir -p $(BUILD)

$(COV_DIR)/obj:
	mkdir -p $(COV_DIR)/obj

clean:
	rm -rf $(BUILD) $(COV_DIR) *.gcno *.gcda
