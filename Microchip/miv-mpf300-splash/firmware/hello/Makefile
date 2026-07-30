# Makefile - Mi-V RV32 hello-world for the PolarFire MPF300 Splash Kit
#
# Builds a bare-metal rv32imc image that prints a banner over CoreUARTapb and
# blinks the LEDs. JTAG-load the resulting .elf/.hex to LSRAM to run.
#
# Default toolchain: the xPack riscv-none-elf GCC (>= 13; put its bin on PATH),
# consistent with the wolfCrypt and fwTPM builds. startup.S's csrw needs zicsr in
# -march on GCC >= 12, hence ARCH = rv32imc_zicsr_zifencei. For the SoftConsole
# GCC 8.3.0 instead: make CROSS_COMPILE=riscv64-unknown-elf- ARCH=rv32imc
#
# Copyright (C) 2006-2026 wolfSSL Inc. GPLv2+ (see source headers).

CROSS_COMPILE ?= riscv-none-elf-
CC      = $(CROSS_COMPILE)gcc
OBJCOPY = $(CROSS_COMPILE)objcopy
OBJDUMP = $(CROSS_COMPILE)objdump
SIZE    = $(CROSS_COMPILE)size

# Silence the binutils >= 2.39 RWX-segment warning (single-region RAM image);
# probed so older binutils lacking the flag are unaffected.
NOWARN_RWX := $(shell $(CC) -Wl,--no-warn-rwx-segments -Wl,--version >/dev/null 2>&1 && echo -Wl,--no-warn-rwx-segments)

COMMON_DIR = ../common
TARGET     = miv-hello

ARCH      ?= rv32imc_zicsr_zifencei
ARCHFLAGS  = -march=$(ARCH) -mabi=ilp32 -mcmodel=medany
CFLAGS     = $(ARCHFLAGS) -Os -g3 -Wall -Wextra -MMD -MP \
             -ffunction-sections -fdata-sections \
             -ffreestanding -I. -I$(COMMON_DIR)
ASFLAGS    = $(ARCHFLAGS) -I$(COMMON_DIR)
LDSCRIPT   = miv-rv32-ram.ld
LDFLAGS    = $(ARCHFLAGS) -T $(LDSCRIPT) -nostartfiles \
             --specs=nano.specs \
             -Wl,--gc-sections -Wl,-Map=$(TARGET).map $(NOWARN_RWX)

SRCS_C = main.c \
         $(COMMON_DIR)/miv_uart.c \
         $(COMMON_DIR)/miv_gpio.c \
         $(COMMON_DIR)/miv_time.c \
         $(COMMON_DIR)/retarget.c
SRCS_S = $(COMMON_DIR)/startup.S

OBJS = $(notdir $(SRCS_C:.c=.o)) $(notdir $(SRCS_S:.S=.o))
VPATH = $(COMMON_DIR)

all: $(TARGET).elf $(TARGET).hex $(TARGET).lst
	$(SIZE) $(TARGET).elf

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.S
	$(CC) $(ASFLAGS) -c $< -o $@

$(TARGET).elf: $(OBJS) $(LDSCRIPT)
	$(CC) $(LDFLAGS) $(OBJS) -o $@

$(TARGET).hex: $(TARGET).elf
	$(OBJCOPY) -O ihex $< $@

$(TARGET).lst: $(TARGET).elf
	$(OBJDUMP) -S -d $< > $@

clean:
	rm -f $(OBJS) $(OBJS:.o=.d) $(TARGET).elf $(TARGET).hex $(TARGET).lst $(TARGET).map

-include $(OBJS:.o=.d)

.PHONY: all clean
