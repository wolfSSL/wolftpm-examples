# Makefile
#
# Build and sign the ML-DSA verification benchmark for wolfBoot on
# PSOC Control C3. The bootloader, signing key and linker script come from
# the wolfBoot build in BUILD.md.
#
# Copyright (C) 2006-2026 wolfSSL Inc.
#
# This file is part of wolfTPM.
#
# wolfTPM is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 3 of the License, or
# (at your option) any later version.
#
# wolfTPM is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1335, USA

WOLFBOOT_DIR ?= $(HOME)/wolfBoot
WOLFSSL_DIR  ?= $(HOME)/wolfssl
SIGN_ALG     ?= --ecc256
APP_VERSION  ?= 1
APP_ADDR     ?= 0x12010100
APP_SIZE     ?= 0x16F00

CROSS   ?= arm-none-eabi-
CC       = $(CROSS)gcc
OBJCOPY  = $(CROSS)objcopy
SIZE     = $(CROSS)size

DEFS = -DTARGET_psoc_c3 -DWOLFSSL_USER_SETTINGS \
       -DPSOC_C3_UART_SCB=3 -DPSOC_C3_UART_PORT=6 \
       -DPSOC_C3_UART_PCLK_GR=4 -DPSOC_C3_UART_PCLK_IDX=6 \
       -DPSOC_C3_PCLK_HZ=48000000

CFLAGS  += -mcpu=cortex-m33 -mthumb -Os -Wall \
           -ffreestanding -ffunction-sections -fdata-sections -fno-common \
           -I. -I$(WOLFBOOT_DIR) -I$(WOLFBOOT_DIR)/include \
           -I$(WOLFSSL_DIR) $(DEFS)
LDFLAGS += -mcpu=cortex-m33 -mthumb -nostartfiles -ffreestanding \
           -Wl,-gc-sections -Wl,-Map=mldsa_bench.map -T mldsa_bench.ld

WC = $(WOLFSSL_DIR)/wolfcrypt/src
SRCS = mldsa_bench.c \
       $(WC)/wc_mldsa.c $(WC)/sha3.c $(WC)/hash.c $(WC)/sha256.c \
       $(WC)/memory.c $(WC)/wc_port.c $(WC)/logging.c $(WC)/misc.c $(WC)/asn.c $(WC)/wolfmath.c $(WC)/sp_int.c \
       $(WOLFBOOT_DIR)/hal/psoc_c3.c \
       $(WOLFBOOT_DIR)/test-app/startup_arm.c

.PHONY: all clean size
all: mldsa_bench_v$(APP_VERSION)_signed.bin size

mldsa_bench.ld: $(WOLFBOOT_DIR)/test-app/ARM-psoc_c3.ld FORCE
	sed -e "s/@WOLFBOOT_TEST_APP_ADDRESS@/$(APP_ADDR)/g" \
	    -e "s/@WOLFBOOT_TEST_APP_SIZE@/$(APP_SIZE)/g" $< > $@

mldsa_bench.elf: $(SRCS) mldsa_bench.ld
	$(CC) $(CFLAGS) $(SRCS) $(LDFLAGS) -o $@

mldsa_bench.bin: mldsa_bench.elf
	$(OBJCOPY) -O binary $< $@

mldsa_bench_v$(APP_VERSION)_signed.bin: mldsa_bench.bin
	$(WOLFBOOT_DIR)/tools/keytools/sign $(SIGN_ALG) $< \
	    $(WOLFBOOT_DIR)/wolfboot_signing_private_key.der $(APP_VERSION)

size: mldsa_bench.elf
	@$(SIZE) $<

clean:
	rm -f mldsa_bench.elf mldsa_bench.bin mldsa_bench.ld mldsa_bench.map \
	      mldsa_bench_v*_signed.bin

# ---------------------------------------------------------------------------
# On-board TPM identity read. Drives the TPM from the MCU over I2C with no
# host involved, using wolfBoot's I2C driver and a stock wolfTPM.
# ---------------------------------------------------------------------------
WOLFTPM_DIR ?= $(HOME)/wolfTPM
TPM_DEFS = -DTARGET_psoc_c3 -DWOLFBOOT_TPM_I2C -DWOLFSSL_USER_SETTINGS \
           -DWOLFTPM_USER_SETTINGS \
           -DPSOC_C3_UART_SCB=3 -DPSOC_C3_UART_PORT=6 \
           -DPSOC_C3_UART_PCLK_GR=4 -DPSOC_C3_UART_PCLK_IDX=6 \
           -DPSOC_C3_PCLK_HZ=48000000 \
           -DPSOC_C3_I2C_SCB=0 -DPSOC_C3_I2C_PORT=9 \
           -DPSOC_C3_I2C_SCL_PIN=0 -DPSOC_C3_I2C_SDA_PIN=2 \
           -DPSOC_C3_I2C_HSIOM_SEL=15 -DPSOC_C3_I2C_HZ=100000

TPM_CFLAGS = -mcpu=cortex-m33 -mthumb -Os -Wall \
             -ffreestanding -ffunction-sections -fdata-sections -fno-common \
             -I. -I$(WOLFBOOT_DIR) -I$(WOLFBOOT_DIR)/include \
             -I$(WOLFSSL_DIR) -I$(WOLFTPM_DIR) $(TPM_DEFS) \
             -include user_settings_tpm.h

WT = $(WOLFTPM_DIR)/src
TPM_SRCS = tpm_identity.c tpm_io_i2c_c3.c rng_seed.c \
           $(WT)/tpm2.c $(WT)/tpm2_packet.c $(WT)/tpm2_tis.c \
           $(WT)/tpm2_wrap.c $(WT)/tpm2_crypto.c $(WT)/tpm2_util.c \
           $(WT)/tpm2_param_enc.c \
           $(WC)/sha256.c $(WC)/sha512.c $(WC)/hash.c $(WC)/memory.c \
           $(WC)/wc_port.c $(WC)/logging.c $(WC)/misc.c $(WC)/asn.c \
           $(WC)/coding.c $(WC)/aes.c $(WC)/hmac.c $(WC)/random.c \
           $(WC)/ecc.c $(WC)/sp_int.c $(WC)/sp_cortexm.c $(WC)/wolfmath.c \
           $(WOLFBOOT_DIR)/hal/psoc_c3.c \
           $(WOLFBOOT_DIR)/hal/i2c/i2c_drv_psoc_c3.c \
           $(WOLFBOOT_DIR)/test-app/startup_arm.c

.PHONY: tpm tpm-clean
tpm: tpm_identity.bin
	@$(SIZE) tpm_identity.elf

tpm_identity.ld: $(WOLFBOOT_DIR)/test-app/ARM-psoc_c3.ld FORCE
	sed -e "s/@WOLFBOOT_TEST_APP_ADDRESS@/$(APP_ADDR)/g" \
	    -e "s/@WOLFBOOT_TEST_APP_SIZE@/$(APP_SIZE)/g" $< > $@

tpm_identity.elf: $(TPM_SRCS) tpm_identity.ld
	$(CC) $(TPM_CFLAGS) $(TPM_SRCS) -mcpu=cortex-m33 -mthumb -nostartfiles \
	    -ffreestanding -Wl,-gc-sections -Wl,-Map=tpm_identity.map \
	    -T tpm_identity.ld -o $@

tpm_identity.bin: tpm_identity.elf
	$(OBJCOPY) -O binary $< $@

tpm-clean:
	rm -f tpm_identity.elf tpm_identity.bin tpm_identity.ld tpm_identity.map

# ---------------------------------------------------------------------------
# The sign-and-verify act, run entirely on the board.
# ---------------------------------------------------------------------------
ONB_SRCS = mldsa_onboard.c act_sign.c demo_util.c tpm_io_i2c_c3.c heap.c rng_seed.c \
           $(WT)/tpm2.c $(WT)/tpm2_packet.c $(WT)/tpm2_tis.c \
           $(WT)/tpm2_wrap.c $(WT)/tpm2_crypto.c $(WT)/tpm2_util.c \
           $(WT)/tpm2_param_enc.c \
           $(WC)/sha256.c $(WC)/sha512.c $(WC)/sha3.c $(WC)/hash.c \
           $(WC)/memory.c $(WC)/wc_port.c $(WC)/logging.c $(WC)/misc.c \
           $(WC)/asn.c $(WC)/coding.c $(WC)/aes.c $(WC)/hmac.c \
           $(WC)/random.c $(WC)/ecc.c $(WC)/sp_int.c $(WC)/sp_cortexm.c \
           $(WC)/wolfmath.c $(WC)/wc_mldsa.c \
           $(WOLFBOOT_DIR)/hal/psoc_c3.c \
           $(WOLFBOOT_DIR)/hal/i2c/i2c_drv_psoc_c3.c \
           $(WOLFBOOT_DIR)/test-app/startup_arm.c

.PHONY: onboard onboard-clean
onboard: mldsa_onboard.bin
	@$(SIZE) mldsa_onboard.elf

mldsa_onboard.ld: $(WOLFBOOT_DIR)/test-app/ARM-psoc_c3.ld FORCE
	sed -e "s/@WOLFBOOT_TEST_APP_ADDRESS@/$(APP_ADDR)/g" \
	    -e "s/@WOLFBOOT_TEST_APP_SIZE@/$(APP_SIZE)/g" $< > $@

mldsa_onboard.elf: $(ONB_SRCS) mldsa_onboard.ld
	$(CC) $(TPM_CFLAGS) $(ONB_SRCS) -mcpu=cortex-m33 -mthumb -nostartfiles \
	    -ffreestanding -Wl,-gc-sections -Wl,-Map=mldsa_onboard.map \
	    -T mldsa_onboard.ld -o $@

mldsa_onboard.bin: mldsa_onboard.elf
	$(OBJCOPY) -O binary $< $@

onboard-clean:
	rm -f mldsa_onboard.elf mldsa_onboard.bin mldsa_onboard.ld mldsa_onboard.map

# ---------------------------------------------------------------------------
# All the demo acts in one image, selected by a command byte on the console.
# ---------------------------------------------------------------------------
ACTS_SRCS = $(subst mldsa_onboard.c,demo_acts.c,$(ONB_SRCS))

.PHONY: acts acts-clean
acts: demo_acts.bin
	@$(SIZE) demo_acts.elf

demo_acts.ld: $(WOLFBOOT_DIR)/test-app/ARM-psoc_c3.ld FORCE
	sed -e "s/@WOLFBOOT_TEST_APP_ADDRESS@/$(APP_ADDR)/g" \
	    -e "s/@WOLFBOOT_TEST_APP_SIZE@/$(APP_SIZE)/g" $< > $@

demo_acts.elf: $(ACTS_SRCS) demo_acts.ld
	$(CC) $(TPM_CFLAGS) $(ACTS_SRCS) -mcpu=cortex-m33 -mthumb -nostartfiles \
	    -ffreestanding -Wl,-gc-sections -Wl,-Map=demo_acts.map \
	    -T demo_acts.ld -o $@

demo_acts.bin: demo_acts.elf
	$(OBJCOPY) -O binary $< $@

acts-clean:
	rm -f demo_acts.elf demo_acts.bin demo_acts.ld demo_acts.map

# ---------------------------------------------------------------------------
# The acts image with the SPDM layer added. Built against a wolfTPM that
# carries the SPDM sources, which the stock one does not, so it takes its own
# source directory. SPDM needs SHA-384, ECC P-384, AES-GCM and HKDF; the
# other images enable none of them.
#
# The image does not fit the 128 KB window the other acts use, so it gets a
# 256 KB one. The part has 512 KB.
# ---------------------------------------------------------------------------
# The SPDM branch is wolfTPM with the Infineon adapter added, so it builds
# every act and there is no second tree to keep in step. Point this at a
# plain master clone only if you are building the SPDM act against one.
WOLFTPM_SPDM_DIR ?= $(HOME)/wolfTPM
SPDM_APP_ADDR    ?= 0x12008100
SPDM_APP_SIZE    ?= 0x37F00

WTS = $(WOLFTPM_SPDM_DIR)/src
SP  = $(WTS)/spdm

SPDM_DEFS = -DWOLFTPM_SPDM -DWOLFTPM_SPDM_TCG -DWOLFSPDM_INFINEON=1 \
            -DAPP_NEED_RNG -DAPP_HEAP_SIZE=24576 \
            -DWOLFSSL_SHA384 -DWOLFSSL_SHA512 -DHAVE_ECC384 \
            -DWOLFSSL_SP_384 -DHAVE_AESGCM -DHAVE_AES_DECRYPT \
            -DHAVE_ECC_DHE -DHAVE_ECC_SIGN -DHAVE_ECC_VERIFY \
            -DHAVE_ECC_KEY_EXPORT -DHAVE_ECC_KEY_IMPORT -DWOLFSSL_KEY_GEN \
            -DECC_TIMING_RESISTANT -DWOLFSSL_PUBLIC_MP

# Same flags as the other TPM images but pointed at the SPDM wolfTPM, and
# with kdf.c added: the TCG handshake derives its session keys with HKDF.
SPDM_CFLAGS = -mcpu=cortex-m33 -mthumb -Os -Wall \
              -ffreestanding -ffunction-sections -fdata-sections -fno-common \
              -I. -I$(WOLFBOOT_DIR) -I$(WOLFBOOT_DIR)/include \
              -I$(WOLFSSL_DIR) -I$(WOLFTPM_SPDM_DIR) \
              $(TPM_DEFS) $(SPDM_DEFS) -include user_settings_tpm.h

# Set SPDM_TRACE=1 to record every TPM bus transfer and print it after the
# act. Diagnostic only - it costs 4 KB of RAM and a long console dump.
# The responder key to pin, as a comma separated byte list: the P-384 X||Y
# of the fitted part, 96 bytes. Leave empty and the act runs in discovery
# mode and prints the key, which is how this value is obtained.
SPDM_RSP_PUBKEY ?=
ifneq ($(SPDM_RSP_PUBKEY),)
  SPDM_DEFS += -DDEMO_SPDM_RSP_PUBKEY='$(SPDM_RSP_PUBKEY)'
endif

# A diagnostic run should give up and print rather than sit on the bus for
# the full five minutes a key generation is allowed.
# wolfTPM and wolfSPDM report through printf, so a debug build also needs a
# console sink. Costs flash; diagnostic only.
SPDM_DEBUG ?= 0
ifeq ($(SPDM_DEBUG),1)
  SPDM_DEFS += -DDEBUG_WOLFTPM
endif

SPDM_TRACE ?= 0
ifeq ($(SPDM_TRACE),1)
  SPDM_DEFS += -DDEMO_I2C_TRACE -DTPM_TIMEOUT_MS=30000 \
               -DTPM_TIMEOUT_TRIES=20000
endif

SPDM_SRCS = $(subst $(WOLFTPM_DIR)/src/,$(WTS)/,$(ACTS_SRCS)) \
            act_spdm.c i2c_trace.c fault_report.c console_write.c $(WC)/kdf.c \
            $(WTS)/tpm2_spdm.c $(SP)/spdm_context.c $(SP)/spdm_crypto.c \
            $(SP)/spdm_infineon.c $(SP)/spdm_kdf.c $(SP)/spdm_msg.c \
            $(SP)/spdm_secured.c $(SP)/spdm_session.c $(SP)/spdm_tcg.c \
            $(SP)/spdm_transcript.c

.PHONY: spdm spdm-clean
spdm: spdm_acts.bin
	@$(SIZE) spdm_acts.elf

spdm_acts.ld: $(WOLFBOOT_DIR)/test-app/ARM-psoc_c3.ld FORCE
	sed -e "s/@WOLFBOOT_TEST_APP_ADDRESS@/$(SPDM_APP_ADDR)/g" \
	    -e "s/@WOLFBOOT_TEST_APP_SIZE@/$(SPDM_APP_SIZE)/g" $< > $@

spdm_acts.elf: $(SPDM_SRCS) spdm_acts.ld
	$(CC) $(SPDM_CFLAGS) $(SPDM_SRCS) -mcpu=cortex-m33 -mthumb \
	    -nostartfiles -ffreestanding -Wl,-gc-sections \
	    -Wl,-Map=spdm_acts.map -T spdm_acts.ld -o $@

spdm_acts.bin: spdm_acts.elf
	$(OBJCOPY) -O binary $< $@

spdm-clean:
	rm -f spdm_acts.elf spdm_acts.bin spdm_acts.ld spdm_acts.map

# ---------------------------------------------------------------------------
# One flashable image: wolfBoot, then the demo signed as its application.
#
# The demo used to be written at 0x12000000, which is wolfBoot's own address,
# so it replaced the bootloader and nothing verified it. Here wolfBoot boots,
# checks the signature and prints its progress on the same console the demo
# then uses.
# ---------------------------------------------------------------------------
WOLFBOOT_CONFIG ?= wolfboot-demo.config
SIGN_KEY        ?= $(WOLFBOOT_DIR)/wolfboot_signing_private_key.der

# wolfBoot takes its own root from $(PWD), which make -C does not change, so
# without this it looks for its key tools under this directory instead. The
# symptom only appears on a tree whose signing key has not been generated
# yet, because otherwise the keygen step never runs.
WOLFBOOT_MAKE = $(MAKE) -C $(WOLFBOOT_DIR) WOLFBOOT_ROOT=$(WOLFBOOT_DIR)

# The application needs wolfBoot's target.h and signing key, and the wolfboot
# target deletes and regenerates both. Under -j that ordering is not
# expressed by the dependency graph, so serialise the whole file rather than
# leave a race that only shows up on a fast machine.
.NOTPARALLEL:

.PHONY: factory factory-clean wolfboot
wolfboot:
	cp $(WOLFBOOT_CONFIG) $(WOLFBOOT_DIR)/.config
	rm -f $(WOLFBOOT_DIR)/include/target.h
	$(WOLFBOOT_MAKE) clean
	$(WOLFBOOT_MAKE) keys
	$(WOLFBOOT_MAKE) wolfboot.bin

# The signature depends on the key as much as on the image. Without the key
# here, a regenerated key leaves this target looking current and the board
# ends up running an image signed by a key wolfBoot no longer holds.
spdm_acts_v$(APP_VERSION)_signed.bin: spdm_acts.bin $(SIGN_KEY)
	$(WOLFBOOT_DIR)/tools/keytools/sign $(SIGN_ALG) $< $(SIGN_KEY) $(APP_VERSION)

# The factory target runs the wolfboot target first and that generates the
# key, but asking for the signed image on its own has no other route to one.
# wolfBoot's keygen declines to overwrite an existing src/keystore.c, asks on
# the console and is invoked with `|| true`, so a tree holding a keystore but
# no key reports nothing and leaves the sign step to fail on a missing file.
$(SIGN_KEY):
	cp $(WOLFBOOT_CONFIG) $(WOLFBOOT_DIR)/.config
	$(WOLFBOOT_MAKE) keys
	@test -f $@ || { echo "$@ not generated;" \
	    "run: make -C $(WOLFBOOT_DIR) keysclean"; false; }

factory: wolfboot spdm_acts_v$(APP_VERSION)_signed.bin
	$(WOLFBOOT_DIR)/tools/bin-assemble/bin-assemble factory.bin \
	    0x0     $(WOLFBOOT_DIR)/wolfboot.bin \
	    0x8000  spdm_acts_v$(APP_VERSION)_signed.bin
	@echo "factory.bin: wolfBoot + signed demo, flash at 0x22000000"
	@ls -l factory.bin

factory-clean:
	rm -f factory.bin spdm_acts_v*_signed.bin

# A generated linker script carries the load address and size, which come
# from make variables rather than from any file, so a timestamp cannot tell
# whether it is current. Regenerate it every build.
.PHONY: FORCE
FORCE:
