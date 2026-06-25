# Makefile for fwTPM server on PolarFire SoC MPFS250T bare-metal U54 hart
#
# Runs fwTPM TIS server on U54 hart 4 in M-mode. Communicates with Linux
# on harts 1-3 via a shared-memory TIS block, poll-mode today (hart 4
# polls mbox.cmd_ready). CLINT MSIP doorbells are a roadmap item; see
# the README for the doorbell story.
#
# Copyright (C) 2006-2026 wolfSSL Inc.

# ========================================================================= #
# Configurable paths                                                        #
# ========================================================================= #
WOLFSSL_DIR    ?= $(CURDIR)/../../../../../wolfssl
WOLFTPM_DIR    ?= $(CURDIR)/../../../../../wolftpm
TOOLCHAIN_PATH ?= /opt/Microchip/SoftConsole-v2022.2-RISC-V-747/riscv-unknown-elf-gcc/bin
PREFIX         ?= riscv64-unknown-elf-

# ========================================================================= #
# Tools                                                                     #
# ========================================================================= #
CC      = $(TOOLCHAIN_PATH)/$(PREFIX)gcc
AS      = $(TOOLCHAIN_PATH)/$(PREFIX)gcc -x assembler-with-cpp
LD      = $(TOOLCHAIN_PATH)/$(PREFIX)gcc
OBJCOPY = $(TOOLCHAIN_PATH)/$(PREFIX)objcopy
SIZE    = $(TOOLCHAIN_PATH)/$(PREFIX)size

# ========================================================================= #
# CPU / ABI                                                                 #
# ========================================================================= #
# Newer GCC/binutils (>= ~2.36) require zicsr/zifencei spelled out for the
# csrr/csrw/fence.i used in startup.S; older toolchains (e.g. the pinned
# SoftConsole v2022.2) imply them under plain rv64imac and reject the names.
# Probe and use whichever the installed toolchain accepts.
ZICSR := $(shell echo 'int main(void){return 0;}' | \
  $(CC) -march=rv64imac_zicsr_zifencei -x c - -o /dev/null 2>/dev/null \
  && echo _zicsr_zifencei)
CPU = -march=rv64imac$(ZICSR) -mabi=lp64 -mcmodel=medany

# ========================================================================= #
# Defines                                                                   #
# ========================================================================= #
DEFS  = -DWOLFSSL_USER_SETTINGS
DEFS += -DWOLFTPM_USER_SETTINGS
DEFS += -DMPFS250T_U54_FWTPM

# Transport backend for the shared TIS mailbox. The U54 L1d is write-back
# with no cache-maintenance instruction, so the mailbox must live in
# genuinely non-cached memory to be coherent for hart 4 under the stock HSS.
#   DDR_WCB       default -- mailbox in the 0xC0000000 non-cached DDR window;
#                 coherent for hart 4 and Linux (used by the demo)
#   LIM_CACHED    cacheable L2 LIM; coherent only under an HSS that sets up
#                 hart-4 PMA for LIM (not the stock Yocto HSS)
#   DDR_NONCACHED 0x1400000000 non-cached 64-bit DDR alias (+ PMP)
#   L1D_OFF       dead end: csrw 0x7C1 halts hart 4 (see startup.S)
#   IHC           Microchip Inter-Hart Comm stub (bitstream-gated)
# Build a candidate with e.g.: make clean && make FWTPM_XPORT=LIM_CACHED
FWTPM_XPORT ?= DDR_WCB
XPORT_DEF    = -DFWTPM_XPORT_$(FWTPM_XPORT)
DEFS        += $(XPORT_DEF)

# DRBG seed source.
#   SCB_NONCE (default) System Controller hardware TRNG (fwtpm_trng_scb.c)
#   JITTER              development-only MTIME jitter (mpfs_rng_seed_cb);
#                       requires FWTPM_DEV_INSECURE_RNG=1 to acknowledge the
#                       weak seed -- the build #errors otherwise.
# Secure by default: a plain `make` uses the hardware TRNG.
FWTPM_RNG ?= SCB_NONCE
DEFS      += -DFWTPM_RNG_$(FWTPM_RNG)

# Explicit acknowledgment for the development-only JITTER seed. Off by
# default, so a plain FWTPM_RNG=JITTER build fails the gate in mpfs_hal.c
# rather than silently shipping a predictable seed.
FWTPM_DEV_INSECURE_RNG ?= 0
ifeq ($(FWTPM_DEV_INSECURE_RNG),1)
  DEFS += -DFWTPM_DEV_INSECURE_RNG
endif

# Verbose wolfSSL/wolfTPM logging. On by default (bring-up example);
# build FWTPM_DEBUG=0 for a quiet/release-style build.
FWTPM_DEBUG ?= 1
ifeq ($(FWTPM_DEBUG),1)
  DEFS += -DFWTPM_DEBUG
endif

# ========================================================================= #
# Build flags                                                               #
# ========================================================================= #
OPT     ?= -Os
CFLAGS  = $(CPU) $(OPT) -g -Wall -Wextra -Wno-unused-parameter
CFLAGS += -ffunction-sections -fdata-sections -fno-common -fno-builtin
CFLAGS += $(DEFS)
CFLAGS += $(EXTRA_CFLAGS)
ASFLAGS = $(CPU) -g $(XPORT_DEF)

# ========================================================================= #
# Include paths                                                             #
# ========================================================================= #
INCLUDES  = -I.
INCLUDES += -I$(WOLFSSL_DIR)
INCLUDES += -I$(WOLFTPM_DIR)

CFLAGS += $(INCLUDES)

# ========================================================================= #
# Linker                                                                    #
# ========================================================================= #
LDSCRIPT = mpfs250-u54.ld
LDFLAGS  = $(CPU) -nostartfiles -Wl,--gc-sections -T$(LDSCRIPT)
LDFLAGS += -Wl,-Map=$(TARGET).map,--cref
LDFLAGS += -specs=nano.specs -specs=nosys.specs

# ========================================================================= #
# Sources                                                                   #
# ========================================================================= #
TARGET = fwtpm_u54

# Local sources
LOCAL_SRC_C  = main.c
LOCAL_SRC_C += mpfs_hal.c
LOCAL_SRC_C += fwtpm_tis_mpfs.c
LOCAL_SRC_C += fwtpm_nv_ram.c
LOCAL_SRC_C += fwtpm_clock_mpfs.c
LOCAL_SRC_C += fwtpm_trng_scb.c
LOCAL_SRC_S  = startup.S

# wolfCrypt sources
WC_SRC  = $(WOLFSSL_DIR)/wolfcrypt/src/wc_port.c
WC_SRC += $(WOLFSSL_DIR)/wolfcrypt/src/memory.c
WC_SRC += $(WOLFSSL_DIR)/wolfcrypt/src/logging.c
WC_SRC += $(WOLFSSL_DIR)/wolfcrypt/src/error.c
WC_SRC += $(WOLFSSL_DIR)/wolfcrypt/src/misc.c
WC_SRC += $(WOLFSSL_DIR)/wolfcrypt/src/random.c
WC_SRC += $(WOLFSSL_DIR)/wolfcrypt/src/hash.c
WC_SRC += $(WOLFSSL_DIR)/wolfcrypt/src/sha.c
WC_SRC += $(WOLFSSL_DIR)/wolfcrypt/src/sha256.c
WC_SRC += $(WOLFSSL_DIR)/wolfcrypt/src/sha512.c
WC_SRC += $(WOLFSSL_DIR)/wolfcrypt/src/aes.c
WC_SRC += $(WOLFSSL_DIR)/wolfcrypt/src/hmac.c
WC_SRC += $(WOLFSSL_DIR)/wolfcrypt/src/coding.c
WC_SRC += $(WOLFSSL_DIR)/wolfcrypt/src/asn.c
WC_SRC += $(WOLFSSL_DIR)/wolfcrypt/src/rsa.c
WC_SRC += $(WOLFSSL_DIR)/wolfcrypt/src/ecc.c
WC_SRC += $(WOLFSSL_DIR)/wolfcrypt/src/kdf.c
WC_SRC += $(WOLFSSL_DIR)/wolfcrypt/src/cryptocb.c
WC_SRC += $(WOLFSSL_DIR)/wolfcrypt/src/sp_int.c
WC_SRC += $(WOLFSSL_DIR)/wolfcrypt/src/sp_c64.c
WC_SRC += $(WOLFSSL_DIR)/wolfcrypt/src/wolfmath.c
WC_SRC += $(WOLFSSL_DIR)/wolfcrypt/src/signature.c
WC_SRC += $(WOLFSSL_DIR)/wolfcrypt/src/cmac.c
WC_SRC += $(WOLFSSL_DIR)/wolfcrypt/src/wc_encrypt.c
WC_SRC += $(WOLFSSL_DIR)/wolfcrypt/src/pwdbased.c

# wolfTPM fwTPM sources
TPM_SRC  = $(WOLFTPM_DIR)/src/tpm2.c
TPM_SRC += $(WOLFTPM_DIR)/src/tpm2_packet.c
TPM_SRC += $(WOLFTPM_DIR)/src/tpm2_param_enc.c
TPM_SRC += $(WOLFTPM_DIR)/src/tpm2_crypto.c
TPM_SRC += $(WOLFTPM_DIR)/src/tpm2_util.c
TPM_SRC += $(WOLFTPM_DIR)/src/tpm2_cryptocb.c
TPM_SRC += $(WOLFTPM_DIR)/src/fwtpm/fwtpm.c
TPM_SRC += $(WOLFTPM_DIR)/src/fwtpm/fwtpm_command.c
TPM_SRC += $(WOLFTPM_DIR)/src/fwtpm/fwtpm_crypto.c
TPM_SRC += $(WOLFTPM_DIR)/src/fwtpm/fwtpm_nv.c
TPM_SRC += $(WOLFTPM_DIR)/src/fwtpm/fwtpm_tis.c

# Combined
SRC_C = $(LOCAL_SRC_C) $(WC_SRC) $(TPM_SRC)
SRC_S = $(LOCAL_SRC_S)

# Object list
BUILD_DIR = build
OBJ_C = $(addprefix $(BUILD_DIR)/,$(notdir $(SRC_C:.c=.o)))
OBJ_S = $(addprefix $(BUILD_DIR)/,$(notdir $(SRC_S:.S=.o)))
OBJS  = $(OBJ_C) $(OBJ_S)

vpath %.c $(sort $(dir $(SRC_C)))
vpath %.S $(sort $(dir $(SRC_S)))

# ========================================================================= #
# Rules                                                                     #
# ========================================================================= #
.PHONY: all clean size help

all: $(TARGET).elf

$(BUILD_DIR):
	@mkdir -p $@

$(BUILD_DIR)/%.o: %.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: %.S | $(BUILD_DIR)
	$(AS) $(ASFLAGS) -c $< -o $@

$(TARGET).elf: $(OBJS) $(LDSCRIPT)
	$(LD) $(LDFLAGS) $(OBJS) -o $@
	$(SIZE) $@

size: $(TARGET).elf
	$(SIZE) -A -x $<

clean:
	rm -rf $(BUILD_DIR) $(TARGET).elf $(TARGET).map

help:
	@echo "Targets:"
	@echo "  make              Build $(TARGET).elf"
	@echo "  make size         Report section sizes"
	@echo "  make clean"
	@echo ""
	@echo "Variables:"
	@echo "  FWTPM_XPORT=      Transport backend:"
	@echo "                    DDR_WCB (default) | LIM_CACHED |"
	@echo "                    DDR_NONCACHED | L1D_OFF | IHC"
	@echo "  FWTPM_RNG=        DRBG seed source:"
	@echo "                    SCB_NONCE (default, HW TRNG) | JITTER (dev-only)"
	@echo "  FWTPM_DEV_INSECURE_RNG=1  acknowledge the JITTER dev seed (else #error)"
	@echo "  FWTPM_DEBUG=      Verbose wolfSSL/wolfTPM logging:"
	@echo "                    1 (default) | 0 (quiet/release)"
	@echo "  Example: make clean && make FWTPM_RNG=JITTER FWTPM_DEV_INSECURE_RNG=1"
