# Host side of the on-board demo

A browser front end for the acts in the parent directory. The board does the
cryptography; this serves a page and renders what the board reports.

The board is driven over its USB serial console: one command byte goes down,
and a stream of JSON events comes back. Nothing here talks to the TPM
directly; every private key stays on the board, including the one that signs
the PQ TLS handshake.

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
| `TPM_TLS_BIN` | Use this `board_tls` binary instead of looking in `tls/`. |
| `TPM_TLS_MLDSA` | ML-DSA parameter set the page names. Default `87`. |
| `WOLFSSL_PQTLS_LIB_DIR` | Where the patched wolfSSL's shared library is. |

## The PQ TLS tab

The TLS stack runs here, on the PC, but the private key does not exist here.
When the server reaches CertificateVerify, wolfSSL's crypto callback hands
the handshake digest to `server.py`, which asks the board to sign it in the
TPM and hands the ML-DSA signature back. The page fails the act unless at
least one signature came from the board, so it cannot pass on a software key.

This firmware signs the pre-hash form of ML-DSA. TLS 1.3 has no registered
signature algorithm for that form, so wolfSSL needs the patch in
`../patches/`, which adds one from the RFC 8446 private-use range. It is not
interoperable with an unpatched peer, and production firmware supporting pure
ML-DSA is expected to remove the need for it. Both ends of this handshake are
`board_tls`, so nothing else has to carry the patch.

One wolfSSL, with the patch applied. **There is no second clone to make**: if
you built the firmware with `build.sh` or `build.bat`, a wolfSSL is already
sitting beside the other trees, pinned to the revision the firmware was built
against, and the patch applies cleanly to it. The patch touches
`src/internal.c`, `src/tls13.c` and `wolfssl/internal.h`, none of which the
firmware compiles, so patching it cannot change the image you flashed.

Set the two paths once. `SRC` is the wolfSSL tree, `DEMO` is this example's
directory, and the install prefix is written out rather than `$HOME` so it is
obvious where it lands. Adjust the first two lines for where your tree is -
`/c/demo/psoc-c3-demo` on the Windows booth machine, wherever you unpacked it
otherwise:

```
ROOT=$HOME/psoc-c3-demo
SRC=$ROOT/wolfssl
DEMO=$ROOT/wolftpm-examples/Infineon/PSoC_Control_C3_Onboard_Demo
```

The patch lives in this example, next to this README:

```
cd $SRC
git apply $DEMO/patches/wolfssl-hash-mldsa-tls.patch
./autogen.sh
./configure --prefix=$ROOT/wolfssl-pqtls \
            --enable-cryptocb --enable-keygen --enable-experimental \
            --enable-mldsa --enable-mlkem --enable-tls-mlkem-standalone \
            --enable-certgen --enable-certreq --enable-certext \
            --enable-static --disable-shared \
            --disable-sys-ca-certs --enable-harden \
            CFLAGS="-DWOLFSSL_HASH_MLDSA_TLS"
make && make install
```

`autogen.sh` needs autoconf, automake and libtool. Under MSYS2 those come
from `pacman -S --needed autotools libtool`; `autoconf` and `automake` are
not package names there. On Windows add `LIBS="-lws2_32"` to the configure
line.

`--enable-static --disable-shared` is there because `board_tls` links statically, so the resulting binary runs on the demo machine with no wolfSSL DLL beside it. Not every MSYS2 environment builds the static library by default: if `lib/` ends up holding only `libwolfssl.dll.a`, these flags were missing and the link fails with `cannot find -lwolfssl`. Building static only also halves the build, since nothing else here wants the shared library.

`--enable-cryptocb` is the crypto callback, and it is the mechanism this whole
tab rests on: it is what hands the handshake digest to the board.
`board_tls.c` does not compile without it, failing with
`unknown type name 'wc_CryptoInfo'`.

`--enable-tls-mlkem-standalone` is not the default and the tab does not work
without it: the key exchange here is ML-KEM on its own rather than paired
with a classical curve, and wolfSSL offers the standalone groups only when
asked. `-DWOLFSSL_HASH_MLDSA_TLS` is what enables the patch's code points; a
build without it is a stock wolfSSL and refuses them, which is the check that
the patch really is opt-in.

Then the one program the tab runs:

```
cd $DEMO/host
make -C tls WOLFSSL_DIR=$ROOT/wolfssl-pqtls
```

On Windows add `CC=gcc` to that line, so the native compiler is used rather
than whatever `cc` resolves to.

Under MSYS2 that link is static, so `board_tls.exe` can be copied to the
machine the demo actually runs on without MSYS2 being installed there. Build
it dynamically and it needs `libwinpthread-1.dll` on `PATH`, which on a booth
machine is a failure that only appears when the tab is pressed.

If you linked against the shared library instead - by setting `LDFLAGS` on the `make` line, because wolfSSL was built without `--enable-static` - then `board_tls.exe` needs `libwolfssl-46.dll` at run time and reports `The code execution cannot proceed because libwolfssl-46.dll was not found`. The DLL is installed in `bin/`, not `lib/`. Copy it next to the executable, which is the first place Windows looks and needs no environment variable:

```
copy ..\..\..\..\..\wolfssl-pqtls\bin\libwolfssl-46.dll tls\
```

Or set `WOLFSSL_PQTLS_LIB_DIR` to the `bin` directory before starting the server, which the server adds to the child's `PATH`. The static build avoids the question entirely.

`build.sh` and `build.bat` in the parent directory build the firmware, not
this; it is a separate step because it needs the patched wolfSSL. There is no
TPM on this machine and nothing here needs wolfTPM.

A handshake takes a few seconds, nearly all of it moving the 4627-byte
signature over the board's 115200 baud console rather than in the TPM. The
tab shows that wait as a step of its own.

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
