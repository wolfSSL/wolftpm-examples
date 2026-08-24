# Makefile - SCU35 MicroBlaze V wolfTPM fwTPM
#
# Builds the wolfTPM firmware TPM (fwTPM) served over the AXI UARTLite. Requires
# the wolfSSL and wolfTPM source trees as siblings of wolftpm-examples (override
# with WOLFSSL_DIR=... / WOLFTPM_DIR=...). Uses the Vitis RISC-V bare-metal
# toolchain (multilib riscv64-unknown-elf-gcc, targets riscv32-xilinx-elf).
#
#   export PATH=/opt/Xilinx/2025.2/gnu/riscv/lin/bin:$PATH
#
# NOTE: the default (RSA+ECC) image is ~652 KB and needs a larger device. The
# minimal ECC-only build (EXTRA_CFLAGS="-DFWTPM_TINY_ECC -DFWTPM_TINY_PCR8 -DFWTPM_TINY_HWTRNG",
# reduced-heap link below) is ~190 KB and fits the stock 192 KB xcsu35p - see
# fpga/README.md. The SYSMON TRNG build needs the bitstream from add_sysmon.tcl.
#
# Copyright (C) 2006-2026 wolfSSL Inc. GPLv3+ (see source headers).

CROSS_COMPILE ?= riscv64-unknown-elf-
CC      = $(CROSS_COMPILE)gcc
OBJCOPY = $(CROSS_COMPILE)objcopy
SIZE    = $(CROSS_COMPILE)size

WOLFSSL_DIR ?= ../../../../../wolfssl
WOLFTPM_DIR ?= ../../../../../wolftpm
COMMON_DIR   = ../common
TARGET       = scu35-fwtpm

ifeq ($(wildcard $(WOLFSSL_DIR)/wolfcrypt/src/aes.c),)
$(error wolfSSL sources not found at WOLFSSL_DIR=$(WOLFSSL_DIR) - set WOLFSSL_DIR to your wolfssl checkout)
endif
ifeq ($(wildcard $(WOLFTPM_DIR)/src/tpm2_packet.c),)
$(error wolfTPM sources not found at WOLFTPM_DIR=$(WOLFTPM_DIR) - set WOLFTPM_DIR to your wolftpm checkout)
endif

ARCH ?= rv32imc_zicsr_zifencei
OPT  ?= -Os
# Optional build flags (EXTRA_CFLAGS):
#   -DFWTPM_ENABLE_PQC   ECC + ML-DSA / ML-KEM instead of RSA (larger).
#   -DFWTPM_TINY_ECC     minimal ECC-P256-only fTPM: no RSA, no P-384, SHA-256
#                        only (no SHA-1), table-free AES, reduced context slots,
#                        and the policy / attestation / credential /
#                        dictionary-attack / parameter-encryption command groups
#                        gated out. Also selects wolfTPM's finer per-command-group
#                        gates explicitly (key-migration / ECDH / hash-cmd /
#                        context / sym-encrypt / clock) in user_settings.h - no
#                        umbrella macro. Code drops from ~271 KB to ~144 KB.
#   -DFWTPM_TINY_HWTRNG  seed the DRBG from the fabric SYSMONE4 System Monitor
#                        (see fwtpm_trng_sysmon.c) instead of wolfCrypt MemUse
#                        entropy, removing ~30 KB. Requires the add_sysmon.tcl
#                        bitstream. This is what brings the image under 192 KB.
#   -DFWTPM_TINY_PCR8    lower IMPLEMENTATION_PCR from 24 to 8, shrinking the PCR
#                        arrays (~2.7 KB). Needed for the fitting build below.
#
# The fitting build for the stock 192 KB xcsu35p (measured ~190 KB, ~1.5 KB
# spare) uses all three flags plus the reduced-heap link:
#   make EXTRA_CFLAGS="-DFWTPM_TINY_ECC -DFWTPM_TINY_PCR8 -DFWTPM_TINY_HWTRNG" \
#        EXTRA_LDFLAGS="-Wl,--defsym=__heap_size=0x3000 -Wl,--defsym=__stack_size=0x2000"
EXTRA_CFLAGS ?=
EXTRA_LDFLAGS ?=

ARCHFLAGS = -march=$(ARCH) -mabi=ilp32 -mcmodel=medany
CFLAGS    = $(ARCHFLAGS) $(OPT) -g3 -MMD -MP -ffunction-sections -fdata-sections \
            -ffreestanding -DWOLFSSL_USER_SETTINGS -DWOLFTPM_USER_SETTINGS \
            -I. -I$(COMMON_DIR) -I$(WOLFSSL_DIR) -I$(WOLFTPM_DIR) \
            $(EXTRA_CFLAGS)
APP_WARN  = -Wall -Wextra

ASFLAGS   = $(ARCHFLAGS)
LDSCRIPT  = mbv-bram.ld
NOWARN_RWX := $(shell $(CC) -Wl,--no-warn-rwx-segments -Wl,--version >/dev/null 2>&1 && echo -Wl,--no-warn-rwx-segments)
LDFLAGS   = $(ARCHFLAGS) $(OPT) -T $(LDSCRIPT) -nostartfiles --specs=nosys.specs \
            -Wl,--gc-sections -Wl,-Map=$(TARGET).map $(NOWARN_RWX) $(EXTRA_LDFLAGS)

WOLF_SRCS = $(wildcard $(WOLFSSL_DIR)/wolfcrypt/src/*.c)
TPM_SRCS  = $(WOLFTPM_DIR)/src/fwtpm/fwtpm.c \
            $(WOLFTPM_DIR)/src/fwtpm/fwtpm_command.c \
            $(WOLFTPM_DIR)/src/fwtpm/fwtpm_nv.c \
            $(WOLFTPM_DIR)/src/fwtpm/fwtpm_crypto.c \
            $(WOLFTPM_DIR)/src/tpm2_util.c \
            $(WOLFTPM_DIR)/src/tpm2_packet.c \
            $(WOLFTPM_DIR)/src/tpm2_param_enc.c \
            $(WOLFTPM_DIR)/src/tpm2_crypto.c
APP_SRCS  = main.c fwtpm_nv_ram.c fwtpm_clock_mbv.c fwtpm_trng_sysmon.c \
            $(COMMON_DIR)/mbv_uart.c $(COMMON_DIR)/mbv_time.c \
            $(COMMON_DIR)/retarget.c
ASM_SRCS  = $(COMMON_DIR)/startup.S

BUILD = build
WOLF_OBJS = $(patsubst %.c,$(BUILD)/wolf/%.o,$(notdir $(WOLF_SRCS)))
TPM_OBJS  = $(patsubst %.c,$(BUILD)/tpm/%.o,$(notdir $(TPM_SRCS)))
APP_OBJS  = $(patsubst %.c,$(BUILD)/%.o,$(notdir $(APP_SRCS))) \
            $(patsubst %.S,$(BUILD)/%.o,$(notdir $(ASM_SRCS)))

VPATH = $(COMMON_DIR):$(WOLFSSL_DIR)/wolfcrypt/src:$(WOLFTPM_DIR)/src:$(WOLFTPM_DIR)/src/fwtpm

all: $(TARGET).elf
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

clean:
	rm -rf $(BUILD) $(TARGET).elf $(TARGET).map

-include $(APP_OBJS:.o=.d) $(TPM_OBJS:.o=.d) $(WOLF_OBJS:.o=.d)

.PHONY: all clean
