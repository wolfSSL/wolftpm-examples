# Makefile - SCU35 MicroBlaze V hello-world
#
# Bare-metal MicroBlaze V (RISC-V rv32imc) sanity image (banner + AXI Timer
# heartbeat) that proves the platform and the common HAL before the wolfTPM
# fwTPM build. Uses the Vitis RISC-V bare-metal toolchain.
#
# Toolchain: the Vitis RISC-V bare-metal GCC. Put its bin on PATH, e.g.
#   export PATH=/opt/Xilinx/2025.2/gnu/riscv/lin/bin:$PATH
# The multilib driver riscv64-unknown-elf-gcc targets riscv32-xilinx-elf and
# builds the rv32imc multilib used here.
#
# Copyright (C) 2006-2026 wolfSSL Inc. GPLv3+ (see source headers).

CROSS_COMPILE ?= riscv64-unknown-elf-
CC      = $(CROSS_COMPILE)gcc
OBJCOPY = $(CROSS_COMPILE)objcopy
SIZE    = $(CROSS_COMPILE)size

COMMON_DIR = ../common
TARGET     = scu35-hello

ARCH ?= rv32imc_zicsr_zifencei
OPT  ?= -O2

ARCHFLAGS = -march=$(ARCH) -mabi=ilp32 -mcmodel=medany
CFLAGS  = $(ARCHFLAGS) $(OPT) -g3 -MMD -MP -ffunction-sections -fdata-sections \
          -ffreestanding -Wall -Wextra -I. -I$(COMMON_DIR)
ASFLAGS = $(ARCHFLAGS)
LDSCRIPT = mbv-bram.ld
NOWARN_RWX := $(shell $(CC) -Wl,--no-warn-rwx-segments -Wl,--version >/dev/null 2>&1 && echo -Wl,--no-warn-rwx-segments)
LDFLAGS = $(ARCHFLAGS) $(OPT) -T $(LDSCRIPT) -nostartfiles --specs=nosys.specs \
          -Wl,--gc-sections -Wl,-Map=$(TARGET).map $(NOWARN_RWX)

APP_SRCS = main.c \
           $(COMMON_DIR)/mbv_uart.c $(COMMON_DIR)/mbv_time.c
ASM_SRCS = $(COMMON_DIR)/startup.S

BUILD    = build
APP_OBJS = $(patsubst %.c,$(BUILD)/%.o,$(notdir $(APP_SRCS))) \
           $(patsubst %.S,$(BUILD)/%.o,$(notdir $(ASM_SRCS)))

VPATH = $(COMMON_DIR)

all: $(TARGET).elf
	$(SIZE) $(TARGET).elf

$(BUILD)/%.o: %.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/%.o: %.S | $(BUILD)
	$(CC) $(ASFLAGS) -c $< -o $@

$(BUILD):
	mkdir -p $@

$(TARGET).elf: $(APP_OBJS) $(LDSCRIPT)
	$(CC) $(LDFLAGS) $(APP_OBJS) -o $@

clean:
	rm -rf $(BUILD) $(TARGET).elf $(TARGET).map

-include $(APP_OBJS:.o=.d)

.PHONY: all clean
