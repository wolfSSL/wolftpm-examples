# Makefile - Zynq-7000 Cortex-A9 wolfTPM fwTPM (ZC702)
#
# Builds the wolfTPM firmware TPM (fwTPM) served over the Cadence UART. Requires
# the wolfSSL and wolfTPM source trees as siblings of wolftpm-examples (override
# with WOLFSSL_DIR=... / WOLFTPM_DIR=...). Toolchain: arm-none-eabi GCC (13.x
# verified). The firmware is JTAG-loaded over a prebuilt FSBL (see README).
#
# Copyright (C) 2006-2026 wolfSSL Inc. GPLv2+ (see source headers).

CROSS_COMPILE ?= arm-none-eabi-
CC      = $(CROSS_COMPILE)gcc
OBJCOPY = $(CROSS_COMPILE)objcopy
SIZE    = $(CROSS_COMPILE)size

WOLFSSL_DIR ?= ../../../../../wolfssl
WOLFTPM_DIR ?= ../../../../../wolftpm
COMMON_DIR   = ../common
TARGET       = zc702-fwtpm

# Fail early with a clear message if the sibling source trees are missing.
ifeq ($(wildcard $(WOLFSSL_DIR)/wolfcrypt/src/aes.c),)
$(error wolfSSL sources not found at WOLFSSL_DIR=$(WOLFSSL_DIR) - set WOLFSSL_DIR to your wolfssl checkout)
endif
ifeq ($(wildcard $(WOLFTPM_DIR)/src/tpm2_packet.c),)
$(error wolfTPM sources not found at WOLFTPM_DIR=$(WOLFTPM_DIR) - set WOLFTPM_DIR to your wolftpm checkout)
endif

# Cortex-A9 with VFPv3-D16 hard-float (startup.S enables VFP).
ARCHFLAGS = -mcpu=cortex-a9 -mfpu=vfpv3-d16 -mfloat-abi=hard -marm
OPT      ?= -O2

# Optional build flags (pass via EXTRA_CFLAGS):
#   -DFWTPM_ENABLE_PQC   ECC + post-quantum (ML-DSA / ML-KEM) TPM instead of the
#                        default RSA + ECC (see user_settings.h). Pair with a
#                        host wolfTPM built --enable-v185 --enable-mldsa
#                        --enable-mlkem.
#   -DFWTPM_PUF_SELFTEST run the synthetic SRAM PUF regression at boot (also
#                        enables WOLFSSL_PUF_TEST). Set the BCH profile with
#                        PUF_T (7/10/13/15) and PUF_CW (codeword count), e.g.
#                        make PUF_T=13 PUF_CW=32 EXTRA_CFLAGS="-DFWTPM_PUF_SELFTEST"
#   -DFWTPM_NV_QSPI      persistent NV + PUF helper data in the QSPI flash (top
#                        two 64 KB sectors) instead of the default volatile RAM
#                        NV. Makes the PUF-derived key stable across power cycles.
EXTRA_CFLAGS ?=

# SRAM PUF BCH profile knobs (override the puf.h defaults t=10, cw=16).
ifdef PUF_T
EXTRA_CFLAGS += -DWC_PUF_BCH_T=$(PUF_T)
endif
ifdef PUF_CW
EXTRA_CFLAGS += -DWC_PUF_NUM_CODEWORDS=$(PUF_CW)
endif

CFLAGS  = $(ARCHFLAGS) $(OPT) -g3 -MMD -MP -ffunction-sections -fdata-sections \
          -ffreestanding -DWOLFSSL_USER_SETTINGS -DWOLFTPM_USER_SETTINGS \
          -I. -I$(COMMON_DIR) -I$(WOLFSSL_DIR) -I$(WOLFTPM_DIR) \
          $(EXTRA_CFLAGS)
# Warnings on the port/common code only (the wolfCrypt/wolfTPM globs are noisy).
APP_WARN = -Wall -Wextra

ASFLAGS  = $(ARCHFLAGS)
LDSCRIPT = zynq7000-fwtpm.ld
# The fwTPM prints no floating point (integer-only printf), so leave the float
# printf support out to save code space.
LDFLAGS  = $(ARCHFLAGS) $(OPT) -T $(LDSCRIPT) -nostartfiles --specs=nano.specs \
           -Wl,--gc-sections -Wl,-Map=$(TARGET).map

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
APP_SRCS  = main.c fwtpm_nv_ram.c fwtpm_nv_qspi.c fwtpm_clock_zynq.c \
            fwtpm_puf.c fwtpm_puf_selftest.c \
            $(COMMON_DIR)/zynq_uart.c $(COMMON_DIR)/zynq_time.c \
            $(COMMON_DIR)/retarget.c $(COMMON_DIR)/mmu.c
ASM_SRCS  = $(COMMON_DIR)/startup.S

BUILD = build
WOLF_OBJS = $(patsubst %.c,$(BUILD)/wolf/%.o,$(notdir $(WOLF_SRCS)))
TPM_OBJS  = $(patsubst %.c,$(BUILD)/tpm/%.o,$(notdir $(TPM_SRCS)))
APP_OBJS  = $(patsubst %.c,$(BUILD)/%.o,$(notdir $(APP_SRCS))) \
            $(patsubst %.S,$(BUILD)/%.o,$(notdir $(ASM_SRCS)))

VPATH = $(COMMON_DIR):$(WOLFSSL_DIR)/wolfcrypt/src:$(WOLFTPM_DIR)/src:$(WOLFTPM_DIR)/src/fwtpm

all: $(TARGET).elf $(TARGET).bin
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

$(TARGET).bin: $(TARGET).elf
	$(OBJCOPY) -O binary $< $@

clean:
	rm -rf $(BUILD) $(TARGET).elf $(TARGET).bin $(TARGET).map

-include $(APP_OBJS:.o=.d) $(TPM_OBJS:.o=.d) $(WOLF_OBJS:.o=.d)

.PHONY: all clean
