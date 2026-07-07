# Makefile for fwtpm_rpmsg_test + fwtpm_pqc_test (PetaLinux/aarch64).
#
# Build with the PetaLinux SDK environment sourced, or override CC=
# with a cross compiler (e.g. aarch64-linux-gnu-gcc).
#
# fwtpm_rpmsg_test is a self-contained raw-TPM2 wire-format smoke test.
# fwtpm_caps is a self-contained decoded GetCapability hello-world
# (rpmsg-transport analogue of wolfTPM's examples/wrap/caps).
# fwtpm_pqc_test uses the wolfTPM2 wrapper API and requires the wolfTPM
# + wolfSSL libraries to be available on the target (rootfs) or via
# WOLFSSL_DIR=/WOLFTPM_DIR= (link against -lwolfssl -lwolftpm).
#
# Copyright (C) 2006-2026 wolfSSL Inc.

CC      ?= $(CROSS_COMPILE)gcc
CFLAGS  ?= -O2 -Wall -Wextra
TARGETS  = fwtpm_rpmsg_test fwtpm_caps

# Optional PQC round-trip client. Enabled when WITH_PQC=1 is passed
# and wolftpm/wolfssl libs are reachable (PetaLinux SDK normally has
# them once the bbappends install -lwolfssl + -lwolftpm into the rootfs).
PQC_CFLAGS  ?=
PQC_LDFLAGS ?= -lwolftpm -lwolfssl

ifeq ($(WITH_PQC),1)
TARGETS += fwtpm_pqc_test
endif

all: $(TARGETS)

fwtpm_rpmsg_test: fwtpm_rpmsg_test.c
	$(CC) $(CFLAGS) -o $@ $<

fwtpm_caps: fwtpm_caps.c
	$(CC) $(CFLAGS) -o $@ $<

fwtpm_pqc_test: fwtpm_pqc_test.c
	$(CC) $(CFLAGS) $(PQC_CFLAGS) -o $@ $< $(PQC_LDFLAGS)

clean:
	rm -f fwtpm_rpmsg_test fwtpm_caps fwtpm_pqc_test

.PHONY: all clean
