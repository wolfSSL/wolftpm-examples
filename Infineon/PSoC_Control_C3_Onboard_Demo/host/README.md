# Host side of the on-board demo

A browser front end for the acts in the parent directory. The board does the
cryptography; this serves a page and renders what the board reports.

The board is driven over its USB serial console: one command byte goes down,
and a stream of JSON events comes back. Nothing here talks to the TPM, so the
page is the same whether the acts run on the board or, for the PQ TLS
handshake, which cannot run on this silicon, against wolfTPM's firmware TPM.

## Running it

Windows:

```
start-demo.cmd
```

Linux:

```
pip install -r requirements.txt
python3 server.py
```

It prints where to point a browser, and which serial port it found:

```
  Demo at http://127.0.0.1:8081
  Looking for the board on COM3
```

The serial port is found by looking for the board's J-Link console, and the
HTTP port steps past anything already using 8080. Neither needs configuring.

## Settings

All optional.

| Variable | Effect |
| --- | --- |
| `TPM_ONBOARD_PORT` | Use this serial port instead of searching. |
| `TPM_BOARD_SERIAL` | Debug probe serial to prefer when several are attached. |
| `TPM_HTTP_PORT` | Serve on exactly this port, and refuse to start if it is in use. |
| `TPM_ATTRACT` | `1` to let the demo cycle itself when nobody is pressing anything. |
| `TPM_PORT_LOG` | `1` to log every open and close of the board's serial port. |
| `TPM_TLS_DIR` | Where the PQ TLS tab's wolfTPM tree is. Default `~/wolftpm-tls-pqc`. |

## The PQ TLS tab

Every other tab runs on the board. This one does not: TLS CertificateVerify
needs pure ML-DSA and the fitted part implements only the pre-hash form, so
the handshake runs against wolfTPM's firmware TPM instead, and the tab says so
on screen.

That means it needs a second wolfTPM, built for the host rather than for the
board, which nothing else here needs. Without it the tab reports that it has
no tree rather than appearing to work.

The commands below are written for Linux and work unchanged under MSYS2 on
Windows, where the tab has also been run. Two differences on Windows: give
wolfSSL `--disable-sys-ca-certs LIBS="-lws2_32"`, because the Windows
certificate store is not wanted here and is what drags in `crypt32`; and
build the wolfTPM programs by name rather than with a bare `make`, because
wolfTPM's own unit tests use POSIX `setenv`, which MinGW does not have.

First a wolfSSL with post-quantum TLS, installed somewhere of its own:

```
git clone -b master https://github.com/wolfSSL/wolfssl.git
cd wolfssl
./autogen.sh
./configure --prefix=$HOME/wolfssl-pqtls --enable-wolftpm \
            --enable-pkcallbacks --enable-keygen --enable-experimental \
            --enable-mldsa --enable-mlkem --enable-tls-mlkem-standalone \
            --enable-certgen --enable-certreq --enable-certext \
            --enable-harden CFLAGS="-DWC_RSA_NO_PADDING"
make && make install
```

`--enable-tls-mlkem-standalone` is not the default and the tab does not work
without it: the key exchange here is ML-KEM on its own rather than paired
with a classical curve, and wolfSSL offers the standalone groups only when
asked. `--enable-wolftpm` is what brings in AES-CFB and the rest of what the
firmware TPM needs.

On Windows add `--disable-sys-ca-certs LIBS="-lws2_32"` to that line.

Then a wolfTPM against it, with the firmware TPM and the socket transport.
This is a host build and separate from the one the board firmware uses, so
it gets its own directory; if you have the source archive, copy its
`wolfTPM` tree here instead of cloning, because that one already carries the
Windows socket fix the clone below needs on Windows:

```
git clone -b master https://github.com/wolfSSL/wolfTPM.git wolftpm-tls-pqc
cd wolftpm-tls-pqc
./autogen.sh
./configure --enable-fwtpm --enable-swtpm --enable-v185 \
            --with-wolfcrypt=$HOME/wolfssl-pqtls
make
```

On Windows add `LIBS="-lws2_32"` to that line too, and build the five
programs by name instead of running a bare `make`:

```
make src/fwtpm/fwtpm_server.exe examples/wrap/caps.exe \
     examples/pqc/gen_pqc_certs.exe examples/tls/tls_server.exe \
     examples/tls/tls_client.exe
```

Leave that tree where it is and point the server at it:

```
export TPM_TLS_DIR=$HOME/wolftpm-tls-pqc
export WOLFSSL_PQTLS_LIB_DIR=$HOME/wolfssl-pqtls/lib
```

The tab starts the firmware TPM itself on port 2381 the first time it runs,
and leaves it running.

## Resetting the board and the TPM

The Reset button on the Console tab restarts the MCU and the TPM together:
their reset lines are tied, so a pin reset through the debug probe covers
both. The bootloader's output then appears on the same tab.

It needs `TPM_RESET_CMD` pointing at a debug-probe reset. With SEGGER tools
and `firmware/reset-board.jlink` from this example:

Windows

```
set TPM_RESET_CMD="C:\Program Files\SEGGER\JLink\JLink.exe" -device PSC3xxF -if SWD -speed 4000 -autoconnect 1 -CommanderScript firmware\reset-board.jlink
```

Linux

```
export TPM_RESET_CMD='JLinkExe -device PSC3xxF -if SWD -speed 4000 -autoconnect 1 -CommanderScript firmware/reset-board.jlink'
```

Add `-SelectEmuBySN <serial>` when more than one probe is attached. Without
`TPM_RESET_CMD` the button reports that the host has no reset mechanism
rather than appearing to work.

## Files

| | |
| --- | --- |
| `server.py` | The server. Reads the board, serves the page, streams events. |
| `onboard.py` | The board driver, also usable on its own: `python3 onboard.py --port COM3 sign`. |
| `x509_lite.py` | Reads the fields of an endorsement certificate that the page shows. |
| `sse_probe.py` | Runs one act and prints the events, for testing without a browser. |
| `shot.py`, `capture-tabs.sh` | Screenshot the tabs, for sharing. |

## On Linux

The server is the same code and needs nothing Windows-specific; only
`start-demo.cmd` is. Two things differ in practice:

- Your user needs access to the serial device, which usually means being in
  the `dialout` group.
- The bus lock is a real `flock` on `/tmp/tpm-bus.lock`, shared with anything
  else that drives the same part, rather than the process-local fallback
  Windows gets. If a serial monitor already owns the board's console the
  server cannot open it, and the identity reads "no TPM detected" with the
  reason.

## Notes

`x509_lite.py` exists because the server used to shell out to `openssl` for
certificate fields, and Windows has no `openssl`, so every certificate came
back unparsed there. It reads only what the page displays and validates
nothing: no signature checking and no chain building.

Only one serial handle is opened, for the life of the server, rather than one
per act. Windows releases a serial handle well after `close()` returns -
seconds, and sometimes more than twenty - so reopening per act makes acts fail
with access denied under ordinary use.
