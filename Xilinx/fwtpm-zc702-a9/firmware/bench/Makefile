# Makefile - ZC702 Cortex-A9 wolfCrypt benchmark
#
# Standalone wolfCrypt (no wolfTPM) benchmark image for a single Cortex-A9 of the
# Zynq-7000 (ZC702). Measures raw core crypto throughput and reports over the
# Cadence UART. Requires the wolfSSL source tree as a sibling of wolftpm-examples
# (override with WOLFSSL_DIR=...). Runs from DDR; loaded over JTAG on top of a
# prebuilt FSBL (ps7_init), like the hello and fwTPM images.
#
# Copyright (C) 2006-2026 wolfSSL Inc. GPLv2+ (see source headers).

CROSS_COMPILE ?= arm-none-eabi-
CC      = $(CROSS_COMPILE)gcc
OBJCOPY = $(CROSS_COMPILE)objcopy
SIZE    = $(CROSS_COMPILE)size

WOLFSSL_DIR ?= ../../../../../wolfssl
COMMON_DIR   = ../common
TARGET       = zc702-bench

ifeq ($(wildcard $(WOLFSSL_DIR)/wolfcrypt/src/aes.c),)
$(error wolfSSL sources not found at WOLFSSL_DIR=$(WOLFSSL_DIR) - set WOLFSSL_DIR)
endif

# Cortex-A9 with VFPv3-D16 hard-float (startup.S enables VFP).
ARCHFLAGS = -mcpu=cortex-a9 -mfpu=vfpv3-d16 -mfloat-abi=hard -marm
OPT      ?= -O2
EXTRA_CFLAGS ?=

CFLAGS  = $(ARCHFLAGS) $(OPT) -g3 -MMD -MP -ffunction-sections -fdata-sections \
          -ffreestanding -DWOLFSSL_USER_SETTINGS \
          -I. -I$(COMMON_DIR) -I$(WOLFSSL_DIR) $(EXTRA_CFLAGS)
APP_WARN = -Wall -Wextra

ASFLAGS  = $(ARCHFLAGS)
LDSCRIPT = zynq7000-ddr.ld
# -u _printf_float pulls in newlib float printf (benchmark prints MB/s, ops/sec).
LDFLAGS  = $(ARCHFLAGS) $(OPT) -T $(LDSCRIPT) -nostartfiles --specs=nano.specs \
           -Wl,--gc-sections -Wl,-Map=$(TARGET).map -u _printf_float

WOLF_SRCS = $(wildcard $(WOLFSSL_DIR)/wolfcrypt/src/*.c) \
            $(WOLFSSL_DIR)/wolfcrypt/benchmark/benchmark.c
APP_SRCS  = main.c \
            $(COMMON_DIR)/zynq_uart.c $(COMMON_DIR)/zynq_time.c \
            $(COMMON_DIR)/retarget.c $(COMMON_DIR)/mmu.c
ASM_SRCS  = $(COMMON_DIR)/startup.S

BUILD = build
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
