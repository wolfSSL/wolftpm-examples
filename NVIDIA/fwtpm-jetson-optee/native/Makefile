# Native dry run: the same source list and user_settings.h as the trusted
# application, built for the host so source-list and configuration problems
# surface on a fast loop instead of a cross-compile, scp and sudo cycle.

WOLFSSL ?= ../../../../wolfssl
WOLFTPM ?= ../../../../wolftpm

include ../srcs.mk

CC      ?= gcc
CFLAGS  += -O1 -g -Wall -Wextra -Wno-unused-parameter
CFLAGS  += -DWOLFSSL_USER_SETTINGS -DWOLFTPM_USER_SETTINGS -DFWTPM_TA_NATIVE
# The assembly sources are in the trusted application's list only, and an
# aarch64 host would otherwise enable them.
CFLAGS  += -DFWTPM_TA_NO_SP_ASM -DFWTPM_TA_NO_ARMASM
CFLAGS  += -I. -I.. -I$(WOLFSSL) -I$(WOLFTPM)

SRCS = main.c $(addprefix $(WOLFSSL)/,$(WC_SRCS)) \
	$(addprefix $(WOLFTPM)/,$(FWTPM_SRCS))

all: fwtpm_native

fwtpm_native: $(SRCS) ../user_settings.h ../srcs.mk Makefile
	$(CC) $(CFLAGS) -o $@ $(SRCS)

clean:
	rm -f fwtpm_native

.PHONY: all clean
