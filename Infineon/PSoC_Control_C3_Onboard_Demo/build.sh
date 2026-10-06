#!/bin/bash
# Build everything, in the order BUILD.md describes.
#
# BUILD.md remains the reference: it explains each step and why. This runs
# the same commands end to end for the common case, and stops at the first
# failure rather than carrying on with a half-built tree.
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

set -u

say()  { printf '\n== %s\n' "$1"; }
die()  { printf '\nbuild.sh: %s\n' "$1" >&2; exit 1; }

# Where the sibling trees live. The script runs both from the demo directory
# inside a checkout and from the root of the source archive, so look upward
# for the directory that holds them rather than guessing a depth.
here=$(cd "$(dirname "$0")" && pwd)
root=$here
while [ "$root" != "/" ]; do
    if [ -d "$root/wolfBoot" ] && [ -d "$root/wolftpm-examples" ]; then
        break
    fi
    root=$(dirname "$root")
done
[ -d "$root/wolfBoot" ] || die "cannot find the wolfBoot tree above $here"

DEMO=$root/wolftpm-examples/Infineon/PSoC_Control_C3_Onboard_Demo
WOLFBOOT=$root/wolfBoot
WOLFSSL=$root/wolfssl
WOLFTPM=${WOLFTPM:-$root/wolfTPM}

say "trees"
printf '  wolfBoot  %s\n  wolfssl   %s\n  wolfTPM   %s\n  demo      %s\n' \
    "$WOLFBOOT" "$WOLFSSL" "$WOLFTPM" "$DEMO"

[ -d "$WOLFTPM" ] || die "no wolfTPM tree at $WOLFTPM
   Clone one beside this script, or set WOLFTPM to point at it. The SPDM act
   needs the branch carrying the Infineon adapter; BUILD.md section 1 says
   which."

say "tools"
for t in arm-none-eabi-gcc make git; do
    command -v "$t" > /dev/null || die "$t is not on PATH"
    printf '  %s\n' "$(command -v $t)"
done

# BUILD.md section 1. wolfssl is not in the archive because its history is
# larger than everything else put together.
#
# Pinned by default so the image this produces is the one whose checksum the
# documentation publishes. Set WOLFSSL_REV=master to build against the tip
# instead, and expect a different checksum.
WOLFSSL_REV=${WOLFSSL_REV:-21bb7a10f1}
if [ ! -d "$WOLFSSL" ]; then
    say "fetching wolfssl (section 1)"
    # A full wolfssl clone is over a gigabyte and most of it is history
    # this never reads. A blobless clone fetches file contents on demand,
    # which for one checkout is a small fraction of that.
    git clone --filter=blob:none https://github.com/wolfSSL/wolfssl.git \
        "$WOLFSSL" || die "could not clone wolfssl"
    git -C "$WOLFSSL" checkout --quiet "$WOLFSSL_REV" \
        || die "no such wolfssl revision: $WOLFSSL_REV"
fi
printf '  wolfssl at %s\n' "$(git -C "$WOLFSSL" rev-parse --short HEAD)"

# wolfBoot signs with key tools it builds from its own wolfssl submodule,
# which an archive cannot carry.
if [ ! -f "$WOLFBOOT/lib/wolfssl/wolfssl/version.h" ]; then
    say "fetching wolfBoot's wolfssl (section 1)"
    if [ -d "$WOLFBOOT/.git" ]; then
        git -C "$WOLFBOOT" submodule update --init lib/wolfssl \
            || die "could not init the wolfBoot wolfssl submodule"
    else
        rm -rf "$WOLFBOOT/lib/wolfssl"
        git clone --filter=blob:none https://github.com/wolfSSL/wolfssl.git \
            "$WOLFBOOT/lib/wolfssl" || die "could not clone wolfBoot's wolfssl"
        git -C "$WOLFBOOT/lib/wolfssl" checkout 4aa1ad7a5b \
            || die "could not check out the pinned wolfssl revision"
    fi
fi

# BUILD.md sections 2 and 3. The factory target builds wolfBoot first, and
# that regenerates target.h, so it is one target rather than two.
say "building (sections 2 and 3)"
cd "$DEMO" || die "no demo directory at $DEMO"
key=""
[ -f responder-key.txt ] && key=$(cat responder-key.txt)
make factory \
    WOLFBOOT_DIR="$WOLFBOOT" \
    WOLFSSL_DIR="$WOLFSSL" \
    WOLFTPM_DIR="$WOLFTPM" \
    WOLFTPM_SPDM_DIR="$WOLFTPM" \
    SPDM_RSP_PUBKEY="$key" || die "build failed"

say "result"
if command -v shasum > /dev/null; then
    shasum -a 256 spdm_acts.bin factory.bin
else
    sha256sum spdm_acts.bin factory.bin
fi
printf '\nFlash factory.bin at 0x22000000; BUILD.md section 4 has the commands.\n'
