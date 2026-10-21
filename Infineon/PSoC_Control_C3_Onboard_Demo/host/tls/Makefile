# board_tls: a TLS 1.3 handshake whose server identity is held in the TPM on
# the board. Links wolfSSL only - there is no TPM on this machine, so nothing
# here needs wolfTPM.
#
# WOLFSSL_DIR must point at a wolfSSL installed with the HashML-DSA code point
# patch applied; see ../README.md.

WOLFSSL_DIR ?= $(HOME)/wolfssl-pqtls

CFLAGS  += -Wall -Wextra -O2 -I$(WOLFSSL_DIR)/include
LDFLAGS += -L$(WOLFSSL_DIR)/lib
LDLIBS  += -lwolfssl -lm

# MSYS2 and MinGW need the socket library named explicitly. Link statically
# too: the demo machine is not the machine this was built on, and a booth
# should not depend on MSYS2's libwinpthread-1.dll being on PATH. That needs
# a wolfSSL configured with --enable-static, which not every MSYS2
# environment does by default; without it the link fails with
# "cannot find -lwolfssl". To link against the shared library instead, set
# LDFLAGS on the command line, which replaces this block's -static.
ifneq (,$(filter MSYS% MINGW%,$(shell uname -s)))
LDLIBS  += -lws2_32
LDFLAGS += -static
endif

all: board_tls

board_tls: board_tls.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS) $(LDLIBS)

clean:
	rm -f board_tls board_tls.exe

.PHONY: all clean
