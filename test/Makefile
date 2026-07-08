# Host test suite for hslock.
#
# The firmware is cross-compiled for the RP2040 and cannot run natively, but
# the hardware-independent logic can be built as a native Linux ELF and tested.
# This Makefile builds each harness against the REAL first-party sources plus
# cheap host stubs (test/stub/), and runs them under three modes:
#
#   make asan      ASan + UBSan, aborts on any error (CI gate)
#   make valgrind  plain build under valgrind, fails on error/leak
#   make coverage  --coverage build, run to emit .gcda (report is a later step)
#   make clean
#
# Invoke from the repo root as `make -C test <target>`.

# Repo layout, relative to this Makefile (test/).
ROOT      := ..
BUILD     := build

# Real first-party / third-party sources exercised by the harnesses.
SHARED    := $(ROOT)/shared
BASE32    := $(ROOT)/libs/base32
BASE64    := $(ROOT)/libs/base64
QRCODEGEN := $(ROOT)/libs/qrcodegen/c

INCLUDES  := -Istub -I$(SHARED) -I$(BASE32) -I$(BASE64) -I$(QRCODEGEN)

# Per-harness source lists (harness + its real dependencies).
SECRET_QR_SRCS := harness_secret_qr.c $(SHARED)/random.c $(BASE32)/base32.c \
                  $(QRCODEGEN)/qrcodegen.c
BASE64_SRCS    := harness_base64.c $(BASE64)/base64.c

CC       := gcc
CSTD     := -std=c11
WARN     := -Wall -Wextra

ASAN_FLAGS := -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=all
VG_FLAGS   := -g -O0
COV_FLAGS  := -g -O0 --coverage

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

# --- Coverage (minimal; full lcov/genhtml report is a later step) -----------
coverage: $(BUILD)/cov_secret_qr $(BUILD)/cov_base64
	@echo "== running harnesses with coverage instrumentation =="
	$(BUILD)/cov_secret_qr
	$(BUILD)/cov_base64
	@echo "== coverage run OK (.gcda emitted under $(BUILD)) =="

$(BUILD)/cov_secret_qr: $(SECRET_QR_SRCS) | $(BUILD)
	$(CC) $(CSTD) $(WARN) $(COV_FLAGS) $(INCLUDES) $(SECRET_QR_SRCS) -o $@

$(BUILD)/cov_base64: $(BASE64_SRCS) | $(BUILD)
	$(CC) $(CSTD) $(WARN) $(COV_FLAGS) $(INCLUDES) $(BASE64_SRCS) -o $@

$(BUILD):
	mkdir -p $(BUILD)

clean:
	rm -rf $(BUILD) coverage *.gcno *.gcda
