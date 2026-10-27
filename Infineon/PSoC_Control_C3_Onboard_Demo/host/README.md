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
| `TPM_RESET_CMD` | Command that resets the board through the debug probe, for the Console tab's Reset button. |
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

### Building it

`../build-tls.sh` runs the whole of the rest of this section: it fetches and pins wolfSSL if there is not one beside the checkout already, applies the patch, installs the library into `wolfssl-pqtls`, and builds `board_tls`. It needs no cross compiler, no wolfBoot and no board, so it runs on a machine that only drives the demo. Re-running it is cheap, because it skips a patched wolfSSL that is already installed; pass `-f` to rebuild that too.

It needs a compiler, git, make and the GNU autotools:

```
macOS    xcode-select --install && brew install autoconf automake libtool
Debian   sudo apt install build-essential autoconf automake libtool git
MSYS2    pacman -S --needed autotools libtool mingw-w64-x86_64-gcc git
```

On Windows run it from the MSYS2 MINGW64 shell. Everything below is what it
does, for anyone who would rather run the steps by hand or needs to change
one of them.

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
            CFLAGS="-DWOLFSSL_HASH_MLDSA_TLS -DWOLFSSL_MLDSA_ALLOW_WEAK_PREHASH"
make && make install
```

The pinned revision needs only that one patch. A newer wolfSSL enforces FIPS 204 section 5.4, which requires a pre-hash to carry the collision strength of the parameter set, and the part signs SHA-256 only - so on a tree that has `mldsa_check_hash_strength` in `wolfcrypt/src/wc_mldsa.c`, apply `patches/wolfssl-mldsa-weak-prehash.patch` as well. The second define above is what it reads; it does nothing at the pin. BUILD.md section 1 has the longer version of this.

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

On Windows build this from the MSYS2 MINGW64 shell and pass
`CC=/mingw64/bin/gcc`. The MSYS gcc at `/usr/bin/gcc` targets the POSIX
emulation layer and cannot link a library the MinGW compiler built: the
link fails with a wall of `undefined reference to __imp__errno`,
`__imp__stricmp` and `__mingw_vfprintf`, which reads as a broken wolfSSL
rather than as the wrong compiler. Whichever of the two built wolfSSL
above has to build this as well.

On macOS `autogen.sh` needs the GNU autotools, which Homebrew supplies. It
installs GNU libtool as `glibtoolize`, because macOS already has an unrelated
tool of that name; if `autoreconf` reports `libtoolize` missing, run it as
`LIBTOOLIZE=glibtoolize ./autogen.sh`. Nothing else differs - the static link
leaves no library-path question at run time, unlike Windows below.

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

### Checking the build with no board attached

The two endpoints can be run by hand, and they get as far as the one
operation only the TPM can do. The public key here is random, which is
enough: the handshake fails before anything verifies it.

```
cd host/tls
./board_tls -gencert -pub=$(head -c 2592 /dev/urandom | od -An -tx1 | tr -d ' \n')
./board_tls -server -p=11598 < /dev/null &
./board_tls -client -p=11598
```

A good build writes the two certificates, then the server prints `SIGNREQ`
and 64 hex characters - the SHA-256 digest it wants signed - followed by
`no answer to SIGNREQ` and `accept failed -248`, and the client reports
`connect failed -308: error state on socket`. Those failures are the
expected result with nothing on the other end of the delegation. Reaching
them proves the library has ML-DSA-87 certificate generation, that the
ML-KEM group and the patch's code point both negotiated, and that the
handshake ran as far as asking for the signature. Delete `host/tls/certs`
afterwards; the tab issues its own.

A client that reports `UseKeyShare failed` instead was built against a
wolfSSL without `--enable-tls-mlkem-standalone`. To check that the library
is the patched one rather than a stock install that happens to be on the
same path, look for the define it was built with:

```
grep WOLFSSL_HASH_MLDSA_TLS ../../../../../wolfssl-pqtls/include/wolfssl/options.h
```

A handshake takes a few seconds, nearly all of it moving the 4627-byte
signature over the board's 115200 baud console rather than in the TPM. The
tab shows that wait as a step of its own.

## Resetting the board and the TPM

The Reset button on the Console tab restarts the MCU and the TPM together:
their reset lines are tied, so a pin reset through the debug probe covers
both. The bootloader's output then appears on the same tab.

It needs `TPM_RESET_CMD` pointing at a debug-probe reset. Use the scripts
beside the example rather than a J-Link command line; `start-demo.cmd`
already does this on Windows, so only the other platforms need the variable
set by hand, from this directory:

```
export TPM_RESET_CMD=$PWD/../firmware/reset-board.sh
```

The Windows equivalent, if you start the server yourself instead of through
`start-demo.cmd`:

```
set TPM_RESET_CMD=powershell -NoProfile -ExecutionPolicy Bypass -File "%CD%\..\firmware\reset-board.ps1"
```

Both take an optional probe serial, or read `TPM_BOARD_SERIAL`, for a bench
with more than one attached.

Two reasons not to write the J-Link invocation out by hand here. The server
runs this command from the `host` directory, so a `-CommanderScript
firmware/reset-board.jlink` resolves against the wrong directory; and
J-Link Commander **exits 0 when it cannot open its script**, so the failure
is silent and the server reports a reset that never happened. The scripts
use an absolute path and `-ExitOnError 1`. Without `TPM_RESET_CMD` at all,
the button says the host has no reset mechanism rather than appearing to
work.

## Files

| | |
| --- | --- |
| `server.py` | The server. Reads the board, serves the page, streams events. |
| `onboard.py` | The board driver, also usable on its own: `python3 onboard.py --port COM3 sign`. |
| `x509_lite.py` | Reads the fields of an endorsement certificate that the page shows. |
| `sse_probe.py` | Runs one act and prints the events, for testing without a browser. |
| `shot.py`, `capture-tabs.sh` | Screenshot the tabs, for sharing. |

## On Linux and macOS

The server is the same code and needs nothing Windows-specific; only
`start-demo.cmd` is. Three things differ in practice:

- On Linux your user needs access to the serial device, which usually means
  being in the `dialout` group. macOS needs nothing: the J-Link console is
  readable as the logged-in user.
- On macOS the port is `/dev/cu.usbmodem...`, which is what autodetection
  returns. If you set `TPM_ONBOARD_PORT` by hand, use that name and not the
  `/dev/tty.usbmodem...` one, which waits for a carrier the J-Link does not
  assert.
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
