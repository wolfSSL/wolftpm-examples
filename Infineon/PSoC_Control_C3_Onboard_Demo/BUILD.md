# Building the on-board demo

Every command here is written out and can be run directly, so nothing about
the build is hidden behind a wrapper. `build.sh` and `build.bat` run exactly
these steps for anyone who just wants the image.

The result is a single firmware image containing every act, and a host program
that drives it from a browser.

## What you need

| | |
| --- | --- |
| Toolchain | `arm-none-eabi-gcc`, tested with 13.2.1 |
| Flashing | SEGGER J-Link software, `JLink.exe` or `JLinkExe` |
| Host demo | Python 3.9 or later |
| Also | `git`, `make` |

On Debian or Ubuntu the toolchain is `gcc-arm-none-eabi`.

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

Three trees. One is a branch that carries this work; two are upstream master.

```
mkdir -p ~/psoc-c3-demo && cd ~/psoc-c3-demo

git clone -b psoc_c3 https://github.com/wolfSSL/wolfBoot.git
git -C wolfBoot submodule update --init lib/wolfssl

git clone -b psoc_c3_onboard_demo https://github.com/wolfSSL/wolftpm-examples.git

git clone -b master https://github.com/wolfSSL/wolfssl.git

git clone -b master https://github.com/wolfSSL/wolfTPM.git
```

Master moves, so a build from it will not match the checksum at the end of
this document. The exact revisions that produced it are listed there if you
need to reproduce it byte for byte.

SPDM itself is in wolfTPM master. The SPDM act additionally needs the
Infineon SPDM adapter, which is not in a release yet. Until it is, build the
`acts` target in section 3, which covers every act except SPDM. When the
adapter lands it is wolfTPM with the adapter added, so it serves both make
variables and there is still only one wolfTPM tree.

If you were given the source archive, three of these trees are already in it
and only wolfssl needs fetching, because its history is larger than
everything else put together:

```
cd <the extracted archive>
git clone -b master https://github.com/wolfSSL/wolfssl.git
```

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
specific to one part. Section 6 explains how to read it off your own.

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

Three things will cost you an afternoon if you skip them:

- **Use a real PSC3 device name.** `-device Cortex-M33` cannot enumerate this
  part's access ports and the failure looks like a dead board. `PSC3xxF`,
  `PSC3xxE`, `PSC3M7F` and `PSC3M8F` all work.
- **Write to `0x22000000`.** The part has four address aliases for the same
  flash and only this one accepts writes, even though the code runs from
  `0x12000000`.
- **Pin the probe if more than one is attached.** Add
  `-SelectEmuBySN <serial>`. A bare invocation attaches to whichever J-Link
  it finds first, which may belong to another board entirely.

Windows users: `firmware/flash.ps1` in the package does all of the above,
including finding `JLink.exe`.

## 5. Run the demo

```
cd ~/psoc-c3-demo/wolftpm-examples/Infineon/PSoC_Control_C3_Onboard_Demo/host
pip install -r requirements.txt
python3 server.py
```

It prints the address to open and the serial port it found. Neither needs
configuring; see `host/README.md` for the settings that override them.

To drive the board without a browser:

```
python3 onboard.py --port /dev/ttyACM0 --baud 115200 sign
```

## 6. The SPDM responder key

The SPDM act pins the public key of the TPM it expects to talk to, so that a
session proves it is talking to *that* part rather than merely to something
that answers. The key differs per part, so a build for your board needs yours.

Build once with the key empty:

```
make spdm WOLFBOOT_DIR=... WOLFSSL_DIR=... WOLFTPM_DIR=... WOLFTPM_SPDM_DIR=...
```

Flash it and run the SPDM act. With no key pinned it runs in discovery mode,
reads the key out of the part and prints it:

```
{"event":"spdm.pubkey","bytes":120,"hex":"0023000C...."}
```

That is a `TPMT_PUBLIC`, not a raw point. The 96 bytes to pin are the two
48-byte coordinates inside it: skip the 22-byte header, take 48 bytes of X,
skip the 2-byte length, take 48 bytes of Y. Format them as
`0x22,0x67,0x53,...` and pass that as `SPDM_RSP_PUBKEY`, or put it in
`responder-key.txt` beside the Makefile and use the command in section 3.

## Checking you got the same thing

The responder key of section 6 is compiled into the image, so the checksum is
specific to the key that was pinned and a build for your own part will differ.
With no key pinned - the discovery build, and what this tree produces as it
stands - the result is:

```
$ shasum -a 256 spdm_acts.bin
dcf3ab08099ffafd2091d5da5977a64fff769a7a2bc7c852c8fbddd3c3e36857  spdm_acts.bin
```

That was built with `arm-none-eabi-gcc` 13.2.1 from these exact revisions:

| Tree | Revision |
| --- | --- |
| wolfBoot | `d393d43b4f` |
| wolfssl | `21bb7a10f1` |
| wolfTPM | `8d3d9687` |
| wolftpm-examples | this tree |

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
