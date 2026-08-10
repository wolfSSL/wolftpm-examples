# Makefile - Mi-V RV32 wolfCrypt test + benchmark
#
# Builds wolfcrypt_test and benchmark_test for the PolarFire MPF300 Splash Kit
# Mi-V soft core. Requires the wolfSSL source tree as a sibling of
# wolftpm-examples (override with WOLFSSL_DIR=...).
#
# Includes the full suite the fwTPM uses - RSA, ECC (P-256/P-384), AES, SHA-2/3,
# HMAC, CMAC - plus post-quantum ML-DSA (Dilithium) and ML-KEM (Kyber), all in
# user_settings.h (no build flag needed).
#
# Default build uses the xPack riscv-none-elf GCC (>= 13) at -O2 with LTO - the
# optimized profile. Put the xPack bin on PATH (add xpack-.../bin). A modern GCC
# is required: the bundled SoftConsole GCC 8.3.0 miscompiles the P-384 SP-math at
# -O2 AND crashes the benchmark's float printf, so use it only for a quick -Os
# smoke build of wolfcrypt_test (no benchmark):
#
#   make CROSS_COMPILE=riscv64-unknown-elf- OPT=-Os ARCH=rv32imc
#
# (GCC >= 12 needs zicsr/zifencei in -march; the RNG hook prototype in
# user_settings.h keeps GCC 15's stricter implicit-decl check happy.)
#
# Copyright (C) 2006-2026 wolfSSL Inc. GPLv2+ (see source headers).

CROSS_COMPILE ?= riscv-none-elf-
CC      = $(CROSS_COMPILE)gcc
OBJCOPY = $(CROSS_COMPILE)objcopy
OBJDUMP = $(CROSS_COMPILE)objdump
SIZE    = $(CROSS_COMPILE)size

WOLFSSL_DIR ?= ../../../../../wolfssl
COMMON_DIR   = ../common
TARGET       = miv-wolfcrypt-test

# Fail early with a clear message if the sibling wolfSSL tree is missing.
ifeq ($(wildcard $(WOLFSSL_DIR)/wolfcrypt/src/aes.c),)
$(error wolfSSL sources not found at WOLFSSL_DIR=$(WOLFSSL_DIR) - set WOLFSSL_DIR to your wolfssl checkout)
endif

ARCH ?= rv32imc_zicsr_zifencei
OPT  ?= -O2 -flto

# EXTRA_CFLAGS passes extra defines. For a wolfcrypt_test-only build (no
# benchmark, hence no float printf - lets the SoftConsole -Os fallback run):
#   make EXTRA_CFLAGS=-DNO_CRYPT_BENCHMARK
EXTRA_CFLAGS ?=
ifneq (,$(findstring NO_CRYPT_BENCHMARK,$(EXTRA_CFLAGS)))
  BENCH_SRC    =
  PRINTF_FLOAT =
else
  BENCH_SRC    = $(WOLFSSL_DIR)/wolfcrypt/benchmark/benchmark.c
  PRINTF_FLOAT = -u _printf_float
endif
# Silence the binutils >= 2.39 RWX-segment warning (single-region RAM image);
# probed so older binutils lacking the flag (SoftConsole 8.3.0) are unaffected.
NOWARN_RWX := $(shell $(CC) -Wl,--no-warn-rwx-segments -Wl,--version >/dev/null 2>&1 && echo -Wl,--no-warn-rwx-segments)

ARCHFLAGS = -march=$(ARCH) -mabi=ilp32 -mcmodel=medany
CFLAGS    = $(ARCHFLAGS) $(OPT) -g3 -MMD -MP -ffunction-sections -fdata-sections \
            -ffreestanding -DWOLFSSL_USER_SETTINGS \
            -I. -I$(COMMON_DIR) -I$(WOLFSSL_DIR) $(EXTRA_CFLAGS)
# Warnings on the port/common code only (the wolfCrypt glob is noisy).
APP_WARN  = -Wall -Wextra
ASFLAGS   = $(ARCHFLAGS)
LDSCRIPT  = miv-rv32-ram.ld
LDFLAGS   = $(ARCHFLAGS) $(OPT) -T $(LDSCRIPT) -nostartfiles \
            --specs=nano.specs $(PRINTF_FLOAT) \
            -Wl,--gc-sections -Wl,-Map=$(TARGET).map $(NOWARN_RWX)

WOLF_SRCS = $(wildcard $(WOLFSSL_DIR)/wolfcrypt/src/*.c) \
            $(WOLFSSL_DIR)/wolfcrypt/test/test.c \
            $(BENCH_SRC)

APP_SRCS  = main.c \
            $(COMMON_DIR)/miv_uart.c \
            $(COMMON_DIR)/miv_time.c \
            $(COMMON_DIR)/retarget.c
ASM_SRCS  = $(COMMON_DIR)/startup.S

# Build objects in ./build to avoid basename clashes across wolfcrypt/src.
BUILD = build
WOLF_OBJS = $(patsubst %.c,$(BUILD)/wolf/%.o,$(notdir $(WOLF_SRCS)))
APP_OBJS  = $(patsubst %.c,$(BUILD)/%.o,$(notdir $(APP_SRCS))) \
            $(patsubst %.S,$(BUILD)/%.o,$(notdir $(ASM_SRCS)))

VPATH = $(COMMON_DIR):$(WOLFSSL_DIR)/wolfcrypt/src:$(WOLFSSL_DIR)/wolfcrypt/test:$(WOLFSSL_DIR)/wolfcrypt/benchmark

all: $(TARGET).elf $(TARGET).hex
	$(SIZE) $(TARGET).elf

$(BUILD)/wolf/%.o: %.c | $(BUILD)/wolf
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: %.c | $(BUILD)
	$(CC) $(CFLAGS) $(APP_WARN) -c $< -o $@

$(BUILD)/%.o: %.S | $(BUILD)
	$(CC) $(ASFLAGS) -c $< -o $@

$(BUILD) $(BUILD)/wolf:
	mkdir -p $@

$(TARGET).elf: $(APP_OBJS) $(WOLF_OBJS) $(LDSCRIPT)
	$(CC) $(LDFLAGS) $(APP_OBJS) $(WOLF_OBJS) -o $@

$(TARGET).hex: $(TARGET).elf
	$(OBJCOPY) -O ihex $< $@

clean:
	rm -rf $(BUILD) $(TARGET).elf $(TARGET).hex $(TARGET).map

-include $(APP_OBJS:.o=.d) $(WOLF_OBJS:.o=.d)

.PHONY: all clean
