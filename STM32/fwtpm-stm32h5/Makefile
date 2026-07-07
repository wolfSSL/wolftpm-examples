# Makefile for fwTPM STM32 Port (NUCLEO-H563ZI, TrustZone Secure)
#
# Usage:
#   make                    Build fwtpm_stm32h5.bin
#   make flash              Flash via STM32_Programmer_CLI (SWD)
#   make clean              Remove build artifacts
#
# Prerequisites:
#   - arm-none-eabi-gcc toolchain
#   - STM32Cube_FW_H5 SDK
#   - wolfSSL source tree (with wolfcrypt)
#
# Copyright (C) 2006-2025 wolfSSL Inc.

# ========================================================================= #
# Configurable paths                                                        #
# ========================================================================= #
STM32CUBE   ?= $(HOME)/STM32Cube/Repository/STM32Cube_FW_H5_V1.5.1
WOLFTPM_DIR ?= ../../../wolftpm
WOLFSSL_DIR ?= ../../../wolfssl
PREFIX      ?= arm-none-eabi-

# ========================================================================= #
# Tools                                                                     #
# ========================================================================= #
CC      = $(PREFIX)gcc
AS      = $(PREFIX)gcc -x assembler-with-cpp
LD      = $(PREFIX)gcc
OBJCOPY = $(PREFIX)objcopy
SIZE    = $(PREFIX)size

# ========================================================================= #
# CPU and build flags                                                       #
# ========================================================================= #
CPU = -mcpu=cortex-m33 -mthumb -mfpu=fpv5-sp-d16 -mfloat-abi=hard

# Only USER_SETTINGS defines on command line — all others in user_settings.h
# (incl. WOLFTPM_FWTPM_NV_APPEND_ONLY for the write-once internal-flash NV).
DEFS = -DWOLFTPM_USER_SETTINGS -DWOLFSSL_USER_SETTINGS -DSTM32H563xx

# TrustZone support: set TZEN=1 for TrustZone builds
TZEN ?= 0

# Semihosting: set SEMIHOSTING=1 to route printf through SWD debug probe
# (frees UART for mssim protocol, enables DEBUG_WOLFTPM output during commands)
SEMIHOSTING ?= 0

# Self-test: set SELFTEST=1 for CI mode (runs built-in tests, exits via BKPT)
SELFTEST ?= 0

CFLAGS  = $(CPU) $(DEFS)
ifeq ($(TZEN),1)
CFLAGS += -mcmse -DTZEN_ENABLED=1
else
CFLAGS += -DTZEN_ENABLED=0
endif
ifeq ($(SELFTEST),1)
CFLAGS += -DFWTPM_SELFTEST
endif
ifeq ($(SEMIHOSTING),1)
CFLAGS += -DARM_SEMIHOSTING -DDEBUG_WOLFTPM
CFLAGS += -Wall -Os -g -ffunction-sections -fdata-sections -fno-common
CFLAGS += -specs=rdimon.specs
else
CFLAGS += -Wall -Os -g -ffunction-sections -fdata-sections -fno-common
CFLAGS += -specs=nano.specs
endif
CFLAGS += $(EXTRA_CFLAGS)
ASFLAGS = $(CPU) -g

# Include paths
INCLUDES  = -I.
INCLUDES += -I$(WOLFTPM_DIR)
INCLUDES += -I$(WOLFSSL_DIR)
INCLUDES += -I$(STM32CUBE)/Drivers/STM32H5xx_HAL_Driver/Inc
INCLUDES += -I$(STM32CUBE)/Drivers/CMSIS/Device/ST/STM32H5xx/Include
INCLUDES += -I$(STM32CUBE)/Drivers/CMSIS/Include

CFLAGS += $(INCLUDES)

# Select linker script based on TZEN setting
ifeq ($(TZEN),1)
  LDSCRIPT = STM32H563ZI_S.ld
else
  LDSCRIPT = STM32H563ZI.ld
endif

LDFLAGS  = $(CPU) -T$(LDSCRIPT)
LDFLAGS += -Wl,--gc-sections -Wl,-Map=$(TARGET).map
ifeq ($(TZEN),1)
LDFLAGS += -Wl,--cmse-implib -Wl,--out-implib=fwtpm_nsc_lib.o
endif
ifeq ($(SEMIHOSTING),1)
LDFLAGS += --specs=rdimon.specs
else
LDFLAGS += -specs=nano.specs
endif
LDFLAGS += -lc -lm

# ========================================================================= #
# Source files                                                              #
# ========================================================================= #

# fwTPM core (platform-independent)
FWTPM_SRC = \
	$(WOLFTPM_DIR)/src/fwtpm/fwtpm.c \
	$(WOLFTPM_DIR)/src/fwtpm/fwtpm_command.c \
	$(WOLFTPM_DIR)/src/fwtpm/fwtpm_nv.c \
	$(WOLFTPM_DIR)/src/fwtpm/fwtpm_crypto.c \
	$(WOLFTPM_DIR)/src/tpm2_util.c \
	$(WOLFTPM_DIR)/src/tpm2_packet.c \
	$(WOLFTPM_DIR)/src/tpm2_param_enc.c \
	$(WOLFTPM_DIR)/src/tpm2_crypto.c

# Port sources
PORT_SRC = \
	main.c \
	fwtpm_nv_flash.c \
	fwtpm_clock_stm32.c \
	fwtpm_nsc.c \
	stm32h5xx_hal_msp.c \
	stm32h5xx_it.c

ifeq ($(TZEN),1)
PORT_SRC += system_stm32h5xx_s.c
else
PORT_SRC += system_stm32h5xx.c
endif

# STM32 HAL sources (only needed modules)
HAL_DIR = $(STM32CUBE)/Drivers/STM32H5xx_HAL_Driver/Src
HAL_SRC = \
	$(HAL_DIR)/stm32h5xx_hal.c \
	$(HAL_DIR)/stm32h5xx_hal_cortex.c \
	$(HAL_DIR)/stm32h5xx_hal_flash.c \
	$(HAL_DIR)/stm32h5xx_hal_flash_ex.c \
	$(HAL_DIR)/stm32h5xx_hal_gpio.c \
	$(HAL_DIR)/stm32h5xx_hal_gtzc.c \
	$(HAL_DIR)/stm32h5xx_hal_icache.c \
	$(HAL_DIR)/stm32h5xx_hal_pwr.c \
	$(HAL_DIR)/stm32h5xx_hal_pwr_ex.c \
	$(HAL_DIR)/stm32h5xx_hal_rcc.c \
	$(HAL_DIR)/stm32h5xx_hal_rcc_ex.c \
	$(HAL_DIR)/stm32h5xx_hal_rng.c \
	$(HAL_DIR)/stm32h5xx_hal_uart.c \
	$(HAL_DIR)/stm32h5xx_hal_uart_ex.c \
	$(HAL_DIR)/stm32h5xx_hal_exti.c \
	$(HAL_DIR)/stm32h5xx_hal_dma.c \
	$(HAL_DIR)/stm32h5xx_hal_dma_ex.c

# wolfCrypt sources
WC_DIR = $(WOLFSSL_DIR)/wolfcrypt/src
WC_SRC = \
	$(WC_DIR)/aes.c \
	$(WC_DIR)/asn.c \
	$(WC_DIR)/coding.c \
	$(WC_DIR)/ecc.c \
	$(WC_DIR)/hash.c \
	$(WC_DIR)/hmac.c \
	$(WC_DIR)/logging.c \
	$(WC_DIR)/memory.c \
	$(WC_DIR)/random.c \
	$(WC_DIR)/rsa.c \
	$(WC_DIR)/sha.c \
	$(WC_DIR)/sha256.c \
	$(WC_DIR)/sha512.c \
	$(WC_DIR)/sp_cortexm.c \
	$(WC_DIR)/sp_int.c \
	$(WC_DIR)/wc_port.c \
	$(WC_DIR)/wolfmath.c \
	$(WC_DIR)/error.c \
	$(WC_DIR)/misc.c \
	$(WC_DIR)/cryptocb.c \
	$(WC_DIR)/kdf.c

# wolfSSL STM32 HAL port
WC_SRC += $(WOLFSSL_DIR)/wolfcrypt/src/port/st/stm32.c

# Startup assembly
ASM_SRC = startup_stm32h563zitx.s

# ========================================================================= #
# Build rules                                                               #
# ========================================================================= #
TARGET = fwtpm_stm32h5

ALL_SRC = $(FWTPM_SRC) $(PORT_SRC) $(HAL_SRC) $(WC_SRC)
C_OBJS  = $(patsubst %.c,%.o,$(notdir $(ALL_SRC)))
ASM_OBJS = $(patsubst %.s,%.o,$(notdir $(ASM_SRC)))
OBJS = $(C_OBJS) $(ASM_OBJS)

# VPATH for source file discovery
VPATH = . \
	$(WOLFTPM_DIR)/src/fwtpm \
	$(WOLFTPM_DIR)/src \
	$(HAL_DIR) \
	$(WC_DIR) \
	$(WOLFSSL_DIR)/wolfcrypt/src/port/st

.PHONY: all flash clean

all: $(TARGET).bin
	@$(SIZE) $(TARGET).elf

$(TARGET).elf: $(OBJS)
	$(LD) $(LDFLAGS) -o $@ $^

$(TARGET).bin: $(TARGET).elf
	$(OBJCOPY) -O binary $< $@

$(TARGET).hex: $(TARGET).elf
	$(OBJCOPY) -O ihex $< $@

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

%.o: %.s
	$(AS) $(ASFLAGS) -c -o $@ $<

flash: $(TARGET).bin
	STM32_Programmer_CLI -c port=SWD -w $< 0x08000000 -v -rst

clean:
	rm -f $(OBJS) $(TARGET).elf $(TARGET).bin $(TARGET).hex $(TARGET).map fwtpm_nsc_lib.o
