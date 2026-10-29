# Normal-world client, built on the board: nvidia-l4t-optee ships
# tee_client_api.h and libteec there, so no cross toolchain is needed.
#
# The shared command IDs come from the wolfTPM tree.
WOLFTPM ?= ../../../../wolftpm

CFLAGS += -Wall -Wextra -O2 -I$(WOLFTPM)
LDLIBS += -lteec

fwtpm_client: main.c
	$(CC) $(CFLAGS) -o $@ main.c $(LDLIBS)

clean:
	rm -f fwtpm_client

.PHONY: clean
