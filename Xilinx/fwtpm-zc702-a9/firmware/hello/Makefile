# Makefile - Zynq-7000 Cortex-A9 hello-world (ZC702)
#
# Bare-metal A9 sanity image (banner + Global-Timer heartbeat) that proves the
# JTAG-over-FSBL load flow and the common HAL before the wolfTPM fwTPM build.
# Uses the arm-none-eabi GCC toolchain (13.x verified).
#
# Copyright (C) 2006-2026 wolfSSL Inc. GPLv2+ (see source headers).

CROSS_COMPILE ?= arm-none-eabi-
CC      = $(CROSS_COMPILE)gcc
OBJCOPY = $(CROSS_COMPILE)objcopy
SIZE    = $(CROSS_COMPILE)size

COMMON_DIR = ../common
TARGET     = zc702-hello

# Cortex-A9 with VFPv3-D16 hard-float (startup.S enables VFP). NEON is present
# on the A9 too but not required here.
ARCHFLAGS = -mcpu=cortex-a9 -mfpu=vfpv3-d16 -mfloat-abi=hard -marm
OPT      ?= -O2

CFLAGS  = $(ARCHFLAGS) $(OPT) -g3 -MMD -MP -ffunction-sections -fdata-sections \
          -ffreestanding -Wall -Wextra -I. -I$(COMMON_DIR)
ASFLAGS = $(ARCHFLAGS)
LDSCRIPT = zynq7000-ddr.ld
LDFLAGS = $(ARCHFLAGS) $(OPT) -T $(LDSCRIPT) -nostartfiles --specs=nano.specs \
          -Wl,--gc-sections -Wl,-Map=$(TARGET).map

APP_SRCS = main.c \
           $(COMMON_DIR)/zynq_uart.c $(COMMON_DIR)/zynq_time.c \
           $(COMMON_DIR)/mmu.c
ASM_SRCS = $(COMMON_DIR)/startup.S

BUILD    = build
APP_OBJS = $(patsubst %.c,$(BUILD)/%.o,$(notdir $(APP_SRCS))) \
           $(patsubst %.S,$(BUILD)/%.o,$(notdir $(ASM_SRCS)))

VPATH = $(COMMON_DIR)

all: $(TARGET).elf $(TARGET).bin
	$(SIZE) $(TARGET).elf

$(BUILD)/%.o: %.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/%.o: %.S | $(BUILD)
	$(CC) $(ASFLAGS) -c $< -o $@

$(BUILD):
	mkdir -p $@

$(TARGET).elf: $(APP_OBJS) $(LDSCRIPT)
	$(CC) $(LDFLAGS) $(APP_OBJS) -o $@

$(TARGET).bin: $(TARGET).elf
	$(OBJCOPY) -O binary $< $@

clean:
	rm -rf $(BUILD) $(TARGET).elf $(TARGET).bin $(TARGET).map

-include $(APP_OBJS:.o=.d)

.PHONY: all clean
