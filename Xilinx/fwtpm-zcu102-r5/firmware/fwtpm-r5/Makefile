# Makefile for fwTPM server on ZCU102 Cortex-R5 (lock-step), bare-metal.
#
# Builds the wolfSSL/wolfTPM/local-source object files using the Vitis
# 2025.2 armr5-none-eabi-gcc toolchain. wolfSSL and wolfTPM are expected
# as sibling checkouts of wolftpm-examples (override with WOLFSSL_DIR=
# / WOLFTPM_DIR= as needed).
#
# Final link to fwtpm_r5.elf requires a Vitis-generated standalone BSP
# for psu_cortexr5_0 in lock-step mode (libxil.a + xil_*.o), plus a
# libmetal.a + libopen_amp.a for the OpenAMP/RPMsg transport. See
# README for the BSP-generation step. "make" defaults to "make objects"
# which builds .o files only -- final link is "make ELF".
#
# Copyright (C) 2006-2026 wolfSSL Inc.

# ========================================================================= #
# Configurable paths                                                        #
# ========================================================================= #
WOLFSSL_DIR    ?= $(CURDIR)/../../../../../wolfssl
WOLFTPM_DIR    ?= $(CURDIR)/../../../../../wolftpm

XILINX_VITIS   ?= /opt/Xilinx/2025.2
TOOLCHAIN_PATH ?= $(XILINX_VITIS)/gnu/armr5/lin/gcc-arm-none-eabi/bin
PREFIX         ?= armr5-none-eabi-

# Source-only paths into the Vitis embeddedsw + open-amp + libmetal trees.
EMBEDDEDSW_DIR ?= $(XILINX_VITIS)/data/embeddedsw
OPENAMP_DIR    ?= $(XILINX_VITIS)/data/open-amp
LIBMETAL_DIR   ?= $(XILINX_VITIS)/data/libmetal

# Optional pre-built BSP (libxil.a, libmetal.a, libopen_amp.a).
# Generate via Vitis: see README "BSP generation".
BSP_DIR        ?= ./bsp

# ========================================================================= #
# Tools                                                                     #
# ========================================================================= #
CC      = $(TOOLCHAIN_PATH)/$(PREFIX)gcc
AS      = $(TOOLCHAIN_PATH)/$(PREFIX)gcc -x assembler-with-cpp
LD      = $(TOOLCHAIN_PATH)/$(PREFIX)gcc
OBJCOPY = $(TOOLCHAIN_PATH)/$(PREFIX)objcopy
SIZE    = $(TOOLCHAIN_PATH)/$(PREFIX)size

# ========================================================================= #
# CPU / ABI -- Cortex-R5F (ARMv7-R, VFPv3-D16)                              #
# ========================================================================= #
CPU = -mcpu=cortex-r5 -mfloat-abi=hard -mfpu=vfpv3-d16

# ========================================================================= #
# Defines                                                                   #
# ========================================================================= #
DEFS  = -DWOLFSSL_USER_SETTINGS
DEFS += -DWOLFTPM_USER_SETTINGS
DEFS += -DZCU102_R5_FWTPM
DEFS += -DARMR5
DEFS += -DUSE_AMP=1
DEFS += -DOPENAMP_REMOTE_LINUX_HOST=1
DEFS += -DXLNX_PLATFORM
DEFS += -DSDT

# ========================================================================= #
# Build flags                                                               #
# ========================================================================= #
OPT     ?= -Os
CFLAGS  = $(CPU) $(OPT) -g -Wall -Wextra -Wno-unused-parameter
CFLAGS += -ffunction-sections -fdata-sections -fno-common -fno-builtin
CFLAGS += $(DEFS)
CFLAGS += $(EXTRA_CFLAGS)
ASFLAGS = $(CPU) -g

# ========================================================================= #
# Include paths                                                             #
# ========================================================================= #
# Local
INCLUDES  = -I.
INCLUDES += -I./include

# wolfSSL / wolfTPM
INCLUDES += -I$(WOLFSSL_DIR)
INCLUDES += -I$(WOLFTPM_DIR)

# Xilinx standalone BSP common + cortexr5
STANDALONE = $(EMBEDDEDSW_DIR)/lib/bsp/standalone_v9_4/src
INCLUDES += -I$(STANDALONE)/common
INCLUDES += -I$(STANDALONE)/arm/common
INCLUDES += -I$(STANDALONE)/arm/common/gcc
INCLUDES += -I$(STANDALONE)/arm/cortexr5
INCLUDES += -I$(STANDALONE)/arm/cortexr5/gcc

# QSPI / IPI / OpenAMP / libmetal headers (Tier-2 only -- these depend
# on a Vitis-generated BSP "include/" tree that exposes the metal/* and
# openamp/* namespaces. Add via WITH_BSP=1 when building "elf").
TIER2_INCLUDES  =
TIER2_INCLUDES += -I$(EMBEDDEDSW_DIR)/XilinxProcessorIPLib/drivers/qspipsu_v1_22/src
TIER2_INCLUDES += -I$(EMBEDDEDSW_DIR)/XilinxProcessorIPLib/drivers/ipipsu_v2_18/src
TIER2_INCLUDES += -I$(EMBEDDEDSW_DIR)/XilinxProcessorIPLib/drivers/scugic_v5_6/src
TIER2_INCLUDES += -I$(BSP_DIR)/psu_cortexr5_0/include

CFLAGS += $(INCLUDES)

# ========================================================================= #
# Linker                                                                    #
# ========================================================================= #
# Default: I+D cache (FWTPM_DCACHE=1). Code and R5-private data are
# cached while the APU-shared rsc_table + trace_buffer (grouped into a
# non-cacheable window by lscript-dcache.ld) and the vrings stay
# non-cacheable, so MemUse seeding / FWTPM_Init runs in ~3 s instead of
# ~30 s. Hardware-validated. Build "make FWTPM_DCACHE=0 ..." for the
# conservative I-cache-only map (D-cache off, ~30 s seeding).
#
# PQC builds (FWTPM_ENABLE_PQC=1) default to the I-cache-only map: the
# ML-KEM/ML-DSA code plus the D-cache shared-NC window overflow the 1 MiB
# DDR carveout. (PQC is experimental and cannot transit the single-frame
# rpmsg transport anyway -- see user_settings.h.)
ifeq ($(FWTPM_ENABLE_PQC),1)
FWTPM_DCACHE ?= 0
else
FWTPM_DCACHE ?= 1
endif
ifeq ($(FWTPM_DCACHE),1)
DEFS += -DFWTPM_DCACHE
LDSCRIPT = lscript-dcache.ld
else
LDSCRIPT = lscript.ld
endif
LDFLAGS  = $(CPU) -nostartfiles -Wl,--gc-sections -T$(LDSCRIPT)
LDFLAGS += -Wl,-Map=$(TARGET).map,--cref
LDFLAGS += -L$(BSP_DIR)/psu_cortexr5_0/lib
LDFLAGS += -Wl,--start-group -lxil -lxilstandalone -lxiltimer -lopen_amp -lmetal -lc -lgcc -Wl,--end-group
LDFLAGS += -specs=nosys.specs

# ========================================================================= #
# Sources                                                                   #
# ========================================================================= #
TARGET = fwtpm_r5

# Tier-1 local sources -- no vendor (XQspiPsu / metal / openamp) headers.
# These compile clean with just the standalone-bsp common headers.
# BSP libxilstandalone.a provides _boot, _vector_table, _startup. We
# do not ship our own boot.S in Tier-2 builds (kept for reference).
LOCAL_SRC_C  = main.c
LOCAL_SRC_C += mpu_setup.c
LOCAL_SRC_C += fwtpm_clock_zynqmp.c
# Custom R5 reset entry: bypasses libxilstandalone _boot which faults
# on its MPU/cache init under the rproc-loaded layout. boot.S also
# pins .vectors at TCM atcm0 (VA 0x00000000) where the R5 cold reset
# vector fetches.
LOCAL_SRC_S  = boot.S

# Tier-2 local sources -- need a generated BSP for vendor headers.
TIER2_SRC_C  = fwtpm_nv_qspi.c
TIER2_SRC_C += fwtpm_nv_ram.c
TIER2_SRC_C += fwtpm_rpmsg.c
TIER2_SRC_C += rsc_table.c
TIER2_SRC_C += platform_info.c
TIER2_SRC_C += pmu_eemi.c
TIER2_SRC_C += bsp_stubs.c

# wolfCrypt sources (matches polarfire port)
WC_SRC  = $(WOLFSSL_DIR)/wolfcrypt/src/wc_port.c
WC_SRC += $(WOLFSSL_DIR)/wolfcrypt/src/memory.c
WC_SRC += $(WOLFSSL_DIR)/wolfcrypt/src/logging.c
WC_SRC += $(WOLFSSL_DIR)/wolfcrypt/src/error.c
WC_SRC += $(WOLFSSL_DIR)/wolfcrypt/src/misc.c
WC_SRC += $(WOLFSSL_DIR)/wolfcrypt/src/random.c
# MemUse entropy source (HAVE_ENTROPY_MEMUSE) -- jitter conditioned
# through SHA3-256 with SP800-90B health tests, feeds the HASH_DRBG.
WC_SRC += $(WOLFSSL_DIR)/wolfcrypt/src/wolfentropy.c
WC_SRC += $(WOLFSSL_DIR)/wolfcrypt/src/hash.c
WC_SRC += $(WOLFSSL_DIR)/wolfcrypt/src/sha.c
WC_SRC += $(WOLFSSL_DIR)/wolfcrypt/src/sha256.c
WC_SRC += $(WOLFSSL_DIR)/wolfcrypt/src/sha512.c
WC_SRC += $(WOLFSSL_DIR)/wolfcrypt/src/sha3.c
WC_SRC += $(WOLFSSL_DIR)/wolfcrypt/src/aes.c
WC_SRC += $(WOLFSSL_DIR)/wolfcrypt/src/hmac.c
WC_SRC += $(WOLFSSL_DIR)/wolfcrypt/src/coding.c
WC_SRC += $(WOLFSSL_DIR)/wolfcrypt/src/asn.c
WC_SRC += $(WOLFSSL_DIR)/wolfcrypt/src/rsa.c
WC_SRC += $(WOLFSSL_DIR)/wolfcrypt/src/ecc.c
WC_SRC += $(WOLFSSL_DIR)/wolfcrypt/src/kdf.c
WC_SRC += $(WOLFSSL_DIR)/wolfcrypt/src/cryptocb.c
WC_SRC += $(WOLFSSL_DIR)/wolfcrypt/src/sp_int.c
WC_SRC += $(WOLFSSL_DIR)/wolfcrypt/src/sp_c32.c
WC_SRC += $(WOLFSSL_DIR)/wolfcrypt/src/wolfmath.c
WC_SRC += $(WOLFSSL_DIR)/wolfcrypt/src/signature.c
WC_SRC += $(WOLFSSL_DIR)/wolfcrypt/src/cmac.c
WC_SRC += $(WOLFSSL_DIR)/wolfcrypt/src/wc_encrypt.c
WC_SRC += $(WOLFSSL_DIR)/wolfcrypt/src/pwdbased.c

# Post-quantum (TCG TPM 2.0 v1.85): ML-KEM (Kyber) + ML-DSA (Dilithium).
# OFF by default: the rpmsg transport carries only one ~496-byte frame
# per message and has no fragmentation layer, so PQC payloads cannot
# round-trip (see user_settings.h Post-Quantum block). Build with
# "make FWTPM_ENABLE_PQC=1" to define FWTPM_ENABLE_PQC and compile the
# algorithms in once a fragmentation layer exists. ARMv7-R has no
# wolfCrypt ASM path for these; the C source compiles cleanly on
# armr5-none-eabi.
ifeq ($(FWTPM_ENABLE_PQC),1)
DEFS += -DFWTPM_ENABLE_PQC
WC_SRC += $(WOLFSSL_DIR)/wolfcrypt/src/wc_mlkem.c
WC_SRC += $(WOLFSSL_DIR)/wolfcrypt/src/wc_mlkem_poly.c
WC_SRC += $(WOLFSSL_DIR)/wolfcrypt/src/wc_mldsa.c
endif

# wolfTPM fwTPM sources (no fwtpm_tis.c -- transport is rpmsg, not TIS)
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

# Default (Tier-1): wolfSSL + wolfTPM + the BSP-free local sources.
SRC_C = $(LOCAL_SRC_C) $(WC_SRC) $(TPM_SRC)
SRC_S = $(LOCAL_SRC_S)

# Tier-2 adds the vendor-header-touching local sources. Used by
# "make elf" when WITH_BSP=1 (default for the elf target).
ifeq ($(WITH_BSP),1)
SRC_C += $(TIER2_SRC_C)
CFLAGS += $(TIER2_INCLUDES)
endif

BUILD_DIR = build
OBJ_C = $(addprefix $(BUILD_DIR)/,$(notdir $(SRC_C:.c=.o)))
OBJ_S = $(addprefix $(BUILD_DIR)/,$(notdir $(SRC_S:.S=.o)))
OBJS  = $(OBJ_C) $(OBJ_S)

vpath %.c $(sort $(dir $(SRC_C)))
vpath %.S $(sort $(dir $(SRC_S)))

# ========================================================================= #
# Rules                                                                     #
# ========================================================================= #
.PHONY: all objects elf stage clean size help

# Where the PetaLinux fwtpm-r5 recipe expects the prebuilt ELF. The
# blob is gitignored and staged from here; keep it in sync with the
# firmware so a rebuilt image does not ship a stale ELF.
PETALINUX_FW_DIR ?= ../../petalinux/project-spec/meta-user/recipes-bsp/fwtpm-r5/files

# Default: object-only build (compile-clean check). Final ELF link
# requires a generated BSP -- run "make elf" once $(BSP_DIR) is staged.
all: objects

objects: $(OBJS)
	@echo "==> Built $(words $(OBJS)) object files in $(BUILD_DIR)/"

elf:
	$(MAKE) WITH_BSP=1 $(TARGET).elf

# Link (if needed) and copy the ELF into the PetaLinux recipe's files/
# dir so "petalinux-build" bundles the current firmware into the image.
# Run this whenever the firmware changes, before rebuilding the image.
stage: elf
	@install -d $(PETALINUX_FW_DIR)
	cp -v $(TARGET).elf $(PETALINUX_FW_DIR)/$(TARGET).elf
	@echo "==> Staged $(TARGET).elf for the PetaLinux fwtpm-r5 recipe"

$(BUILD_DIR):
	@mkdir -p $@

$(BUILD_DIR)/%.o: %.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: %.S | $(BUILD_DIR)
	$(AS) $(ASFLAGS) -c $< -o $@

$(TARGET).elf: $(OBJS) $(LDSCRIPT)
	$(LD) $(OBJS) $(LDFLAGS) -o $@
	$(SIZE) $@

size: $(TARGET).elf
	$(SIZE) -A -x $<

clean:
	rm -rf $(BUILD_DIR) $(TARGET).elf $(TARGET).map

help:
	@echo "Targets:"
	@echo "  make           Compile all .o files (default, BSP-free)"
	@echo "  make objects   Same as default"
	@echo "  make elf       Link $(TARGET).elf (requires generated BSP at $(BSP_DIR))"
	@echo "  make stage     Link + copy ELF into the PetaLinux recipe (before petalinux-build)"
	@echo "  make size      Report section sizes"
	@echo "  make clean"
	@echo
	@echo "Override paths via WOLFSSL_DIR= / WOLFTPM_DIR= / XILINX_VITIS= / BSP_DIR="
