# Building the on-board demo

Every command here is written out and can be run directly, so nothing about
the build is hidden behind a wrapper. `build.sh` and `build.bat` run exactly
these steps for anyone who just wants the image.

The result is a single firmware image containing every act, and a host program
that drives it from a browser.

## What you need

| | |
| --- | --- |
| Cross compiler | `arm-none-eabi-gcc`, tested with 13.2.1 and 13.3.0 |
| Host compiler | `gcc`. wolfBoot's key tools sign the application and run on this machine |
| Flashing | SEGGER J-Link software V9.84 or newer, `JLink.exe` or `JLinkExe` |
| Host demo | Python 3.9 or later |
| Also | `git`, `make` |
| The PQ TLS tab only | `autoconf`, `automake`, `libtool` for wolfSSL's `autogen.sh` |

Two compilers, and both are needed: the cross compiler builds the firmware, and the plain one builds the tools that sign it. Leaving the host compiler out gets as far as "Building key tools" and then fails with `gcc: command not found`, which does not look like a missing package.

On Debian or Ubuntu the cross compiler is `gcc-arm-none-eabi`.

### On Windows

Install MSYS2 from https://www.msys2.org, at the default `C:\msys64`; `build.bat` looks there by name.

MSYS2 ships several environments side by side - UCRT64, MINGW64, CLANG64 and a plain MSYS - each with its own Start menu entry, its own `C:\msys64\<name>\bin`, and its own separate copy of every package. Installing a compiler into one does nothing for the others, and each shell puts only its own `bin` on `PATH`. **Pick one and stay in it.** A current MSYS2 steers you to UCRT64. The plain MSYS shell is for `pacman` only and has no compilers at all.

```
pacman -Syu
```

That closes the window partway through; reopen it and run it again until there is nothing left to do. Until the second pass finishes the package databases are half written and installing anything reports "target not found".

```
pacman -S --needed make git autotools libtool
pacman -S --needed mingw-w64-ucrt-x86_64-gcc
pacman -S --needed \
  mingw-w64-ucrt-x86_64-arm-none-eabi-toolchain
```

For MINGW64 the names are the same without `ucrt-`. Take the `-toolchain` group rather than `...-arm-none-eabi-gcc` alone, which arrives without binutils or newlib and stops at the first `#include`. `autoconf` and `automake` are not package names in MSYS2; the `autotools` group brings them.

Those `pacman` lines are split with a backslash on purpose: as one line the command wraps in the terminal and a paste can lose characters at the break, which comes back as `target not found: mingw-w64-x86`, pacman reporting the fragment it was handed. Command Prompt's equivalent, a trailing `^`, does not survive a paste at all, so the Windows commands elsewhere in this document are left on one line however long they get.

Check the shell and the compilers before going on:

```
echo $MSYSTEM
arm-none-eabi-gcc --version
gcc --version
```

## Building it all at once

Every step below is written out so it can be read and run by hand, which is
the point of this document. If you only want the image, there is one script
that runs the lot:

```
./build.sh          # Linux, macOS, MSYS2
build.bat           # Windows
```

It fetches what the archive cannot carry, builds the bootloader and the
application, signs and assembles them, and prints the checksums. The rest of
this document is what it does, step by step.

## 1. Get the sources

Four clones. wolftpm-examples is the branch carrying this demo; the rest
are upstream master. The SPDM act needs wolfTPM from a branch instead, which
is the note just below.

Put it somewhere with **no space anywhere in the path**. `make` cannot carry a space through a prerequisite list, so building under `Downloads`, or under a Windows user folder named like `WolfSSL Inc`, fails several minutes in with a `cp: ... No such file or directory` naming only the tail of the path. `build.sh` checks and refuses up front; by hand there is nothing to catch it. `C:\demo` on Windows, `~/psoc-c3-demo` elsewhere.

```
mkdir -p ~/psoc-c3-demo && cd ~/psoc-c3-demo

git clone -b master https://github.com/wolfSSL/wolfBoot.git
git -C wolfBoot submodule update --init lib/wolfssl

git clone -b psoc_c3_onboard_demo https://github.com/wolfSSL/wolftpm-examples.git

git clone https://github.com/wolfSSL/wolfssl.git
git -C wolfssl checkout 21bb7a10f1

git clone -b master https://github.com/wolfSSL/wolfTPM.git
```

**The wolfSSL revision is pinned on purpose, and the demo does not work on an
unpatched master.** wolfSSL now enforces FIPS 204 section 5.4, which requires a
Hash-ML-DSA pre-hash to carry at least as much collision strength as the
parameter set: SHA-384 or better for ML-DSA-65, SHA-512 for 87. This firmware
pairs SHA-256 with parameter set 87, because SHA-256 is the only pre-hash the
SLB9678 firmware 45.91 will sign with; the part refuses SHA-384 and SHA-512
with `TPM_RC_SIZE`. On a newer wolfSSL the signing and verification calls
return `BAD_FUNC_ARG` and the sign and PQ TLS tabs fail, unless the patch
below is applied. `build.sh` pins the same revision, and master also will not
match the checksum at the end of this document.

`WOLFSSL_MLDSA_ALLOW_WEAK_PREHASH` is defined in `user_settings_tpm.h` and on
the host configure line above. wolfTPM names that option in pull request 627,
but wolfSSL does not implement it, so `patches/wolfssl-mldsa-weak-prehash.patch`
adds it. **A newer wolfSSL therefore works, with that patch applied.** It was
verified on master `6090053cbb`: unpatched the board signs and the host
verification returns invalid in 0 ms, refused at the argument check; patched,
the same board and the same firmware return valid. The patch is deliberately
not upstream, because a build using it is not FIPS 204 compliant.

The pinned revision is kept as the tested path and the one the checksum at the
end of this document reproduces. It predates the check, so there the patch is
neither needed nor applicable.

SPDM itself is in wolfTPM. The SPDM act additionally needs the Infineon
vendor commands, which are not in a release yet, so clone the branch
carrying them in place of master in section 1; there is still only one
wolfTPM tree:

```
git clone -b infineon_submodule https://github.com/dgarske/wolfTPM.git
git -C wolfTPM submodule update --init lib/wolfSPDM
```

wolfTPM keeps its SPDM implementation in a `lib/wolfSPDM` submodule, and the
Infineon vendor commands live there, in
[wolfSPDM pull request 38](https://github.com/wolfSSL/wolfSPDM/pull/38). The
branch above points the submodule at that commit, which is why the second
command is needed. The demo's Makefile detects either arrangement, so a
pre-submodule wolfTPM builds too. Both the branch and this section go away
once the changes are released.

If you were given the source archive, three of these trees are already in it
and only wolfssl needs fetching, because its history is larger than
everything else put together:

```
cd <the extracted archive>
git clone https://github.com/wolfSSL/wolfssl.git
git -C wolfssl checkout 21bb7a10f1
```

The same pin as section 1, and for the same reason.

wolfBoot also needs its own copy of wolfssl, and not the same revision. The
bootloader builds its signature verification and its key tools from the
revision wolfBoot pins as a submodule, which is what wolfBoot is tested
against; the firmware builds against a newer master for ML-DSA. They are
separate builds with separate configurations, so they stay separate trees.

In a git checkout of wolfBoot that is one command:

```
git -C wolfBoot submodule update --init lib/wolfssl
```

The archive has no submodules in it - `git archive` does not descend into
them - so there the same revision is fetched by hand:

```
git clone https://github.com/wolfSSL/wolfssl.git wolfBoot/lib/wolfssl
git -C wolfBoot/lib/wolfssl checkout 4aa1ad7a5b
```

## 2. Generate wolfBoot's target header

The firmware includes wolfBoot's `target.h`, which is generated from the board
configuration rather than committed. Two commands:

```
cd ~/psoc-c3-demo/wolfBoot
cp config/examples/psoc_c3.config .config
make include/target.h
```

This writes `include/target.h` and does not build the bootloader. You only
need to do it again if you change `.config`.

## 3. Build the firmware

```
cd ~/psoc-c3-demo/wolftpm-examples/Infineon/PSoC_Control_C3_Onboard_Demo

make acts APP_ADDR=0x12000000 APP_SIZE=0x20000 \
  WOLFBOOT_DIR=~/psoc-c3-demo/wolfBoot \
  WOLFSSL_DIR=~/psoc-c3-demo/wolfssl \
  WOLFTPM_DIR=~/psoc-c3-demo/wolfTPM
```

That produces `demo_acts.bin`, which is what gets flashed. It carries every
act except SPDM.

### With the SPDM act

The SPDM act needs the Infineon SPDM adapter described in section 1. That
branch is wolfTPM with the adapter added, so it serves both variables and
there is only one wolfTPM tree to keep:

```
make spdm \
  WOLFBOOT_DIR=~/psoc-c3-demo/wolfBoot \
  WOLFSSL_DIR=~/psoc-c3-demo/wolfssl \
  WOLFTPM_DIR=~/psoc-c3-demo/wolfTPM \
  WOLFTPM_SPDM_DIR=~/psoc-c3-demo/wolfTPM \
  SPDM_RSP_PUBKEY="$(cat responder-key.txt)"
```

That produces `spdm_acts.bin`, about 190 KB, carrying every act including
SPDM.

### One image with the bootloader

The demo runs as wolfBoot's application, not from the reset vector, so
wolfBoot verifies it before it runs and prints that on the console. `make
factory` builds the bootloader, signs the demo with wolfBoot's key and
assembles both into one file:

```
make factory \
  WOLFBOOT_DIR=~/psoc-c3-demo/wolfBoot \
  WOLFSSL_DIR=~/psoc-c3-demo/wolfssl \
  WOLFTPM_DIR=~/psoc-c3-demo/wolfTPM \
  WOLFTPM_SPDM_DIR=~/psoc-c3-demo/wolfTPM \
  SPDM_RSP_PUBKEY="$(cat responder-key.txt)"
```

`factory.bin` is what gets flashed. It uses `wolfboot-demo.config` in this
directory rather than wolfBoot's stock `psoc_c3.config`, because the demo
image does not fit the partitions the stock one defines; the header of that
file explains the layout.

`SPDM_RSP_PUBKEY` is the public key of the TPM fitted to your board: 96 bytes
of P-384 X followed by Y, as a comma separated list of C byte literals. It is
specific to one part; `responder-key.txt` holds the one for the SLB9678
this was developed against. Section 6 explains how to read it off your own.

Two optional flags on the `spdm` target, both diagnostic: `SPDM_TRACE=1`
records every TPM bus transfer and prints it after the act, and
`SPDM_DEBUG=1` routes wolfTPM's and wolfSPDM's own output to the console.
Neither belongs in an image you are demonstrating.

## 4. Flash it

`factory.bin` holds wolfBoot at the start and the signed demo behind it, so
the whole thing is written at one address.

```
JLinkExe -device PSC3xxF -if SWD -speed 4000 -autoconnect 1
```

then, at the J-Link prompt:

```
loadbin factory.bin 0x22000000
r
go
q
```

Four things will cost you an afternoon if you skip them:

- **Use a real PSC3 device name.** `-device Cortex-M33` cannot enumerate this
  part's access ports and the failure looks like a dead board. `PSC3xxF`,
  `PSC3xxE`, `PSC3M7F` and `PSC3M8F` all work.
- **Write to `0x22000000`.** The part has four address aliases for the same
  flash and only this one accepts writes, even though the code runs from
  `0x12000000`.
- **Pin the probe if more than one is attached.** Add
  `-SelectEmuBySN <serial>`. A bare invocation attaches to whichever J-Link
  it finds first, which may belong to another board entirely.
- **Use a J-Link new enough to know the part.** A version released before the
  PSC3 family does not recognise any of those device names, and the connect
  then stalls rather than reporting the problem. V7.96 is too old; V8.96 and
  V9.84 are known good. Several versions install side by side, so check which
  one is actually on PATH.

A script does all of the above on either platform, including finding the newest `JLink.exe` among the versions installed on Windows:

```
powershell -ExecutionPolicy Bypass -File firmware\flash.ps1    # Windows
firmware/flash.sh                                              # Linux, macOS
```

Both print `FLASH DONE`, and both fail loudly rather than quietly: J-Link Commander prints `O.K.` after a successful connect whether or not the image was then written, so they require one that follows the download. To flash an image other than the one beside them, pass the path: `-Image <path>` to the first, as the sole argument to the second. Set `TPM_BOARD_SERIAL` to pin the probe.

## 5. Run the demo

```
cd ~/psoc-c3-demo/wolftpm-examples/Infineon/PSoC_Control_C3_Onboard_Demo/host
pip install -r requirements.txt
python3 server.py
```

It prints the address to open and the serial port it found. Neither needs
configuring; see `host/README.md` for the settings that override them.

One thing does need setting here, because there is no launcher to do it:
the Console tab's Reset button drives the board through the debug probe, and
without this it reports that the host has no reset mechanism.

```
export TPM_RESET_CMD=$PWD/../firmware/reset-board.sh
```

Use the script rather than a J-Link command line. The server runs the command
from the `host` directory, and J-Link Commander exits 0 when it cannot open
its script, so a wrong relative path there is reported as a successful reset
that never happened.

To drive the board without a browser:

```
python3 onboard.py --port /dev/ttyACM0 --baud 115200 sign
```

On Windows, run `start-demo.cmd` in that same directory rather than calling
Python yourself:

```
cd psoc-c3-demo\wolftpm-examples\Infineon\PSoC_Control_C3_Onboard_Demo\host
py -m pip install -r requirements.txt
start-demo.cmd
```

It stops an older copy of the server before starting a new one, which matters
because Windows lets a second server take the port from the first rather than
refusing, leaving two of them fighting over the board's serial port. It also
points the server at `firmware\reset-board.ps1`, without which the Console
tab's Reset button reports that the host has no reset mechanism and the
bootloader output never appears.

**The window then goes blank and stays blank, which is not a hang.** Both streams are redirected to files so a booth machine started from a scheduled task still leaves something to read afterwards. The address and the serial port are at the top of `server.err`, in a second window:

```
type server.err
```

```
  Demo at http://127.0.0.1:8080
  Looking for the board on COM7
```

The port is 8080 unless something already holds it, in which case the server steps up until it finds one free, so read the number rather than assuming it.

### The post-quantum TLS tab needs one more build

Six of the seven tabs work as soon as the server starts. The TLS tab reports `board_tls is not built` until a patched wolfSSL and one small program exist. Nothing else in the demo depends on them, so this can be left until last.

The TPM signs the pre-hash form of ML-DSA and TLS 1.3 has no registered signature algorithm for that form, so wolfSSL needs the patch in `patches/`, which adds one from the RFC 8446 private-use range. **There is no second wolfSSL to clone**: the one from section 1 is already here, and the patch touches only `src/internal.c`, `src/tls13.c` and `wolfssl/internal.h`, none of which the firmware compiles, so patching it cannot change the image you flashed.

Set the two paths once, and note the prefix is an explicit directory rather than `$HOME` so it is obvious where it lands:

```
ROOT=~/psoc-c3-demo
DEMO=$ROOT/wolftpm-examples/Infineon/PSoC_Control_C3_Onboard_Demo
```

```
cd $ROOT/wolfssl
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

On Windows add `LIBS="-lws2_32"`, and run it from the UCRT64 or MINGW64 shell matching the packages installed earlier - the plain MSYS shell has no compiler and reports `configure: error: no acceptable C compiler found in $PATH`.

`--enable-static --disable-shared` is there because `board_tls` links statically, so the resulting binary runs on the demo machine with no wolfSSL DLL beside it. Not every MSYS2 environment builds the static library by default: if `lib/` ends up holding only `libwolfssl.dll.a`, these flags were missing and the link fails with `cannot find -lwolfssl`. Building static only also halves the build, since nothing else here wants the shared library.

Three of those flags are load-bearing. `--enable-cryptocb` is the crypto callback, which is the mechanism that hands the handshake digest to the board; without it `board_tls.c` does not compile at all, failing with `unknown type name 'wc_CryptoInfo'`. `--enable-tls-mlkem-standalone` offers ML-KEM on its own rather than paired with a classical curve, and without it the tab cannot negotiate `ML_KEM_768`. `-DWOLFSSL_HASH_MLDSA_TLS` turns on the patch's code points; a build without it is a stock wolfSSL and refuses them, which is the check that the patch really is opt-in.

Then the one program the tab runs:

```
cd $DEMO/host
make -C tls WOLFSSL_DIR=$ROOT/wolfssl-pqtls
```

On Windows add `CC=gcc`, so the native compiler is used rather than whatever `cc` resolves to. That link is static under MSYS2 on purpose, so `board_tls.exe` can be copied to a machine with no MSYS2 installed; `ldd tls/board_tls.exe` should list nothing outside `C:\Windows`.

If you linked against the shared library instead - by setting `LDFLAGS` on the `make` line, because wolfSSL was built without `--enable-static` - then `board_tls.exe` needs `libwolfssl-46.dll` at run time and reports `The code execution cannot proceed because libwolfssl-46.dll was not found`. The DLL is installed in `bin/`, not `lib/`. Copy it next to the executable, which is the first place Windows looks and needs no environment variable:

```
copy ..\..\..\..\..\wolfssl-pqtls\bin\libwolfssl-46.dll tls\
```

Or set `WOLFSSL_PQTLS_LIB_DIR` to the `bin` directory before starting the server, which the server adds to the child's `PATH`. The static build avoids the question entirely.

Restart the server and press the tab. It ends on **Post-quantum TLS established**, with `TLS_AES_256_GCM_SHA384`, key exchange `ML_KEM_768`, and a signature the board produced in two to three seconds.

## 6. The SPDM responder key

The SPDM act pins the public key of the TPM it expects to talk to. A key read off the part during the handshake would prove only that *something* answered; pinning proves it is the part you meant. The key is per-part, so a different module needs a different key, and a build for your board needs yours.

The build cannot do this for you. The key lives inside the TPM and only firmware running on the board can read it out, so there is no path from `build.sh` to the part.

**You will know you need this** when the SPDM tab fails at step 1:

```
{"event":"error","where":"InitWithSpdmKey","rc":"0xfffffff6"}
{"event":"spdm.step","n":1,"state":"fail"}
```

The act answers that for you. Having failed the pin, it brings the device back up without one, reads the part's key, and prints it in the same run:

```
{"event":"spdm.pubkey","bytes":120,"hex":"0023000C..."}
{"event":"log","message":"the pinned key belongs to a different part ..."}
```

A board with nothing pinned at all prints the same `spdm.pubkey` line, from the discovery path, so either way the key comes out of one run.

That hex is a `TPMT_PUBLIC`, not a key you can paste. The 96 bytes to pin are the two 48-byte coordinates inside it: skip the 22-byte header, take 48 bytes of X, skip the 2-byte length, take 48 bytes of Y. `host/spdm_pubkey.py` does that, and takes either the bare hex or the whole event line:

```
python3 host/spdm_pubkey.py '<paste the whole spdm.pubkey line>'
```

It prints the 96 bytes as `0x22,0x67,0x53,...`. Save exactly that, on one line, as `responder-key.txt` beside the Makefile; the commands in section 3 read it from there with no other change. Then rebuild and reflash, and step 1 reports "responder key pinned".

Do not slice the hex by hand. An offset wrong by two bytes produces a key that parses, builds, flashes and then fails at the handshake rather than at the parse, which looks like an entirely different problem. The script checks the length field between the coordinates rather than stepping over it, so a buffer that is not a P-384 `TPMT_PUBLIC` is rejected rather than quietly mangled.

## Checking you got the same thing

The responder key of section 6 is compiled into the image, so the checksum is
specific to the key that was pinned. This tree carries `responder-key.txt`
for the part it was developed against and the build uses it, so an unmodified
build here gives:

```
$ shasum -a 256 spdm_acts.bin
cf82901043a599ae0a62e15df594d969a7dac3aea478363b44131abb7c666798  spdm_acts.bin
```

Pinning your own part's key gives a different image, and a different
checksum, which is expected.

That was built with `arm-none-eabi-gcc` 13.2.1 from these exact revisions:

| Tree | Revision |
| --- | --- |
| wolfBoot | `d393d43b4f` |
| wolfssl | `21bb7a10f1` |
| wolfTPM | `8d3d9687` |
| wolftpm-examples | this tree |

The archive's `wolfTPM` additionally carries the Windows socket fix of
wolfTPM PR 623, which the PQ TLS tab needs on Windows. None of it compiles
into this image, so the checksum is the same with it or without it.

A different checksum is not necessarily wrong - a newer master or a different
toolchain will differ - but an identical one means the whole chain matched.

The compiler version is the usual reason. The same sources built under MSYS2
on Windows, where `arm-none-eabi-gcc` is 13.3.0 rather than 13.2.1, produce a
working image with a different checksum. Reproducing the one above needs the
toolchain version named above as well as the revisions.

`factory.bin` will not match, and is not meant to: the first `make factory`
generates a signing key if the wolfBoot tree has none, so the signature over
the demo, and the keystore built into the bootloader, are yours. The
`factory.bin` shipped in the package was signed with the key this was
developed against; either is fine to flash, but only one of them is yours.
