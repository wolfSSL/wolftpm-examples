# Makefile - SCU35 MicroBlaze V wolfCrypt benchmark
#
# Standalone wolfCrypt (no wolfTPM) benchmark image for the MicroBlaze V soft
# core. Measures raw core crypto throughput and reports over the AXI UARTLite.
# Requires the wolfSSL source tree as a sibling of wolftpm-examples (override
# with WOLFSSL_DIR=...). Uses the Vitis RISC-V bare-metal toolchain.
#
#   export PATH=/opt/Xilinx/2025.2/gnu/riscv/lin/bin:$PATH
#
# Copyright (C) 2006-2026 wolfSSL Inc. GPLv3+ (see source headers).

CROSS_COMPILE ?= riscv64-unknown-elf-
CC      = $(CROSS_COMPILE)gcc
SIZE    = $(CROSS_COMPILE)size

WOLFSSL_DIR ?= ../../../../../wolfssl
COMMON_DIR   = ../common
TARGET       = scu35-bench

ifeq ($(wildcard $(WOLFSSL_DIR)/wolfcrypt/src/aes.c),)
$(error wolfSSL sources not found at WOLFSSL_DIR=$(WOLFSSL_DIR) - set WOLFSSL_DIR)
endif

ARCH ?= rv32imc_zicsr_zifencei
OPT  ?= -O2
EXTRA_CFLAGS ?=
EXTRA_LDFLAGS ?=

ARCHFLAGS = -march=$(ARCH) -mabi=ilp32 -mcmodel=medany
CFLAGS    = $(ARCHFLAGS) $(OPT) -g3 -MMD -MP -ffunction-sections -fdata-sections \
            -ffreestanding -DWOLFSSL_USER_SETTINGS \
            -I. -I$(COMMON_DIR) -I$(WOLFSSL_DIR) $(EXTRA_CFLAGS)
APP_WARN  = -Wall -Wextra

ASFLAGS   = $(ARCHFLAGS)
LDSCRIPT  = mbv-bram.ld
NOWARN_RWX := $(shell $(CC) -Wl,--no-warn-rwx-segments -Wl,--version >/dev/null 2>&1 && echo -Wl,--no-warn-rwx-segments)
# -u _printf_float pulls in newlib float printf (benchmark prints MB/s, ops/sec).
LDFLAGS   = $(ARCHFLAGS) $(OPT) -T $(LDSCRIPT) -nostartfiles --specs=nosys.specs \
            -Wl,--gc-sections -Wl,-Map=$(TARGET).map -u _printf_float \
            $(NOWARN_RWX) $(EXTRA_LDFLAGS)

WOLF_SRCS = $(wildcard $(WOLFSSL_DIR)/wolfcrypt/src/*.c) \
            $(WOLFSSL_DIR)/wolfcrypt/benchmark/benchmark.c
APP_SRCS  = main.c \
            $(COMMON_DIR)/mbv_uart.c $(COMMON_DIR)/mbv_time.c \
            $(COMMON_DIR)/retarget.c
ASM_SRCS  = $(COMMON_DIR)/startup.S

BUILD     = build
WOLF_OBJS = $(patsubst %.c,$(BUILD)/wolf/%.o,$(notdir $(WOLF_SRCS)))
APP_OBJS  = $(patsubst %.c,$(BUILD)/%.o,$(notdir $(APP_SRCS))) \
            $(patsubst %.S,$(BUILD)/%.o,$(notdir $(ASM_SRCS)))

VPATH = $(COMMON_DIR):$(WOLFSSL_DIR)/wolfcrypt/src:$(WOLFSSL_DIR)/wolfcrypt/benchmark

all: $(TARGET).elf
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

clean:
	rm -rf $(BUILD) $(TARGET).elf $(TARGET).map

-include $(APP_OBJS:.o=.d) $(WOLF_OBJS:.o=.d)

.PHONY: all clean
