# Makefile - Mi-V RV32 wolfTPM fwTPM for the PolarFire MPF300 Splash Kit
#
# Builds the wolfTPM firmware TPM (fwTPM) driven over CoreUARTapb. Requires the
# wolfSSL and wolfTPM source trees as siblings of wolftpm-examples (override
# with WOLFSSL_DIR=... / WOLFTPM_DIR=...). Default toolchain is the Microchip
# SoftConsole GCC 8.3.0 (riscv64-unknown-elf-, rv32imc): the fwTPM needs no floats
# or -O2, and this is the toolchain it was validated on. To build with a modern
# GCC (>= 12) instead, pass ARCH=rv32imc_zicsr_zifencei (csrw needs zicsr).
# GCC >= 14 used to fail here because wolfTPM's tpm2_types.h reaches an XSLEEP_MS
# branch that calls nanosleep(), which bare-metal newlib does not declare; that is
# handled now by defining XSLEEP_MS in user_settings.h, so a newer toolchain builds
# cleanly. SoftConsole remains the default only because it is the version Microchip
# ships; the PQC build in fact needs a newer GCC to fit (see README).
#
# Copyright (C) 2006-2026 wolfSSL Inc. GPLv2+ (see source headers).

CROSS_COMPILE ?= riscv64-unknown-elf-
CC      = $(CROSS_COMPILE)gcc
OBJCOPY = $(CROSS_COMPILE)objcopy
SIZE    = $(CROSS_COMPILE)size

WOLFSSL_DIR ?= ../../../../../wolfssl
WOLFTPM_DIR ?= ../../../../../wolftpm
COMMON_DIR   = ../common
TARGET       = miv-fwtpm

# Fail early with a clear message if the sibling source trees are missing.
ifeq ($(wildcard $(WOLFSSL_DIR)/wolfcrypt/src/aes.c),)
$(error wolfSSL sources not found at WOLFSSL_DIR=$(WOLFSSL_DIR) - set WOLFSSL_DIR to your wolfssl checkout)
endif
ifeq ($(wildcard $(WOLFTPM_DIR)/src/tpm2_packet.c),)
$(error wolfTPM sources not found at WOLFTPM_DIR=$(WOLFTPM_DIR) - set WOLFTPM_DIR to your wolftpm checkout)
endif

ARCH ?= rv32imc
OPT  ?= -Os

# Silence the binutils >= 2.39 RWX-segment warning (this is a single-region RAM
# image, so text/data/heap/stack intentionally share one rwx region). Probed so
# older binutils that lack the flag (SoftConsole 8.3.0) are unaffected.
NOWARN_RWX := $(shell $(CC) -Wl,--no-warn-rwx-segments -Wl,--version >/dev/null 2>&1 && echo -Wl,--no-warn-rwx-segments)

# Optional build flags (pass via EXTRA_CFLAGS):
#   -DMIV_SNVM_NV_AUTH         NV in authenticated-ciphertext (encrypted) sNVM
#                              instead of the default plaintext. Requires the
#                              FPGA design's security policy to permit
#                              authenticated sNVM writes (Libero Security Policy
#                              Manager), else the controller returns
#                              SNVM_WRITE_NOT_PERMITTED.
#   -DMIV_SNVM_BOOTCNT_TEST    enable the standalone sNVM boot-counter self-test
#                              (writes sNVM every boot; off by default)
#   -DMIV_FWTPM_PQC            ECC + post-quantum (ML-DSA / ML-KEM) TPM instead
#                              of the default RSA + ECC. Drops RSA to fit 512 KB;
#                              enables wolfTPM v1.85 support (WOLFTPM_V185) and
#                              the ML-DSA/ML-KEM settings in user_settings.h. Pair
#                              with a host wolfTPM built --enable-v185
#                              --enable-mldsa --enable-mlkem.
# e.g.: make EXTRA_CFLAGS="-DMIV_SNVM_BOOTCNT_TEST"
#       make EXTRA_CFLAGS="-DMIV_FWTPM_PQC"
#
# Link-time optimization (make OPT="-Os -flto") shrinks the image a further
# ~12 KB, but is OFF by default: SoftConsole GCC 8.3.0 is known to miscompile
# ECC P-384 at aggressive optimization on this target, so LTO must be validated
# with full ECC/RSA operations on the board before use.
EXTRA_CFLAGS ?=
EXTRA_LDFLAGS ?=

ARCHFLAGS = -march=$(ARCH) -mabi=ilp32 -mcmodel=medany
CFLAGS    = $(ARCHFLAGS) $(OPT) -g3 -MMD -MP -ffunction-sections -fdata-sections \
            -ffreestanding -DWOLFSSL_USER_SETTINGS -DWOLFTPM_USER_SETTINGS \
            -I. -I$(COMMON_DIR) -I$(WOLFSSL_DIR) -I$(WOLFTPM_DIR) \
            $(EXTRA_CFLAGS)
# Warnings on the port/common code only (the wolfCrypt/wolfTPM globs are noisy).
APP_WARN  = -Wall -Wextra

ASFLAGS   = $(ARCHFLAGS)
LDSCRIPT  = miv-rv32-ram.ld
# The fwTPM prints no floating point (integer-only printf), so the float printf
# support (_dtoa_r + soft-float helpers) is left out to save code space.
# EXTRA_LDFLAGS is appended last so callers can override linker-script symbols
# without restating LDFLAGS, e.g. to shrink the heap reservation:
#   make EXTRA_LDFLAGS=-Wl,--defsym=__heap_size=114688
LDFLAGS   = $(ARCHFLAGS) $(OPT) -T $(LDSCRIPT) -nostartfiles \
            --specs=nano.specs \
            -Wl,--gc-sections -Wl,-Map=$(TARGET).map $(NOWARN_RWX) \
            $(EXTRA_LDFLAGS)

# wolfCrypt
WOLF_SRCS = $(wildcard $(WOLFSSL_DIR)/wolfcrypt/src/*.c)
# wolfTPM fwTPM engine + TPM2 packet/util (no server/socket/TIS build)
TPM_SRCS  = $(WOLFTPM_DIR)/src/fwtpm/fwtpm.c \
            $(WOLFTPM_DIR)/src/fwtpm/fwtpm_command.c \
            $(WOLFTPM_DIR)/src/fwtpm/fwtpm_nv.c \
            $(WOLFTPM_DIR)/src/fwtpm/fwtpm_crypto.c \
            $(WOLFTPM_DIR)/src/tpm2_util.c \
            $(WOLFTPM_DIR)/src/tpm2_packet.c \
            $(WOLFTPM_DIR)/src/tpm2_param_enc.c \
            $(WOLFTPM_DIR)/src/tpm2_crypto.c
# Port + common
APP_SRCS  = main.c fwtpm_nv_snvm.c fwtpm_clock_miv.c \
            fwtpm_rng_sysserv.c miv_sysserv.c \
            $(COMMON_DIR)/miv_uart.c $(COMMON_DIR)/miv_time.c \
            $(COMMON_DIR)/retarget.c
ASM_SRCS  = $(COMMON_DIR)/startup.S

BUILD = build
WOLF_OBJS = $(patsubst %.c,$(BUILD)/wolf/%.o,$(notdir $(WOLF_SRCS)))
TPM_OBJS  = $(patsubst %.c,$(BUILD)/tpm/%.o,$(notdir $(TPM_SRCS)))
APP_OBJS  = $(patsubst %.c,$(BUILD)/%.o,$(notdir $(APP_SRCS))) \
            $(patsubst %.S,$(BUILD)/%.o,$(notdir $(ASM_SRCS)))

VPATH = $(COMMON_DIR):$(WOLFSSL_DIR)/wolfcrypt/src:$(WOLFTPM_DIR)/src:$(WOLFTPM_DIR)/src/fwtpm

all: $(TARGET).elf $(TARGET).hex
	$(SIZE) $(TARGET).elf

$(BUILD)/wolf/%.o: %.c | $(BUILD)/wolf
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/tpm/%.o: %.c | $(BUILD)/tpm
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/%.o: %.c | $(BUILD)
	$(CC) $(CFLAGS) $(APP_WARN) -c $< -o $@
$(BUILD)/%.o: %.S | $(BUILD)
	$(CC) $(ASFLAGS) -c $< -o $@

$(BUILD) $(BUILD)/wolf $(BUILD)/tpm:
	mkdir -p $@

$(TARGET).elf: $(APP_OBJS) $(TPM_OBJS) $(WOLF_OBJS) $(LDSCRIPT)
	$(CC) $(LDFLAGS) $(APP_OBJS) $(TPM_OBJS) $(WOLF_OBJS) -o $@

$(TARGET).hex: $(TARGET).elf
	$(OBJCOPY) -O ihex $< $@

clean:
	rm -rf $(BUILD) $(TARGET).elf $(TARGET).hex $(TARGET).map

-include $(APP_OBJS:.o=.d) $(TPM_OBJS:.o=.d) $(WOLF_OBJS:.o=.d)

.PHONY: all clean
