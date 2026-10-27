#!/bin/bash
# Build the PQ TLS tab's helper, host/tls/board_tls.
#
# This is the host half only: a patched wolfSSL and one small program. It
# needs no cross compiler, no wolfBoot and no wolfTPM, so it runs on a
# machine that only drives the demo. build.sh builds the firmware and is a
# separate step; host/README.md explains each command this runs and why.
#
# Linux, macOS, and Windows from the MSYS2 MINGW64 shell.
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
die()  { printf '\nbuild-tls.sh: %s\n' "$1" >&2; exit 1; }

force=0
for arg in "$@"; do
    case "$arg" in
        -f|--force) force=1 ;;
        -h|--help)
            cat <<'EOF'
usage: build-tls.sh [-f]

Builds host/tls/board_tls, which the PQ TLS tab runs.

  -f, --force   rebuild wolfSSL even if a patched one is already installed

Environment:
  ROOT          where the wolfssl and wolfssl-pqtls trees live
                (default: the directory holding this checkout)
  WOLFSSL_REV   wolfssl revision to build (default: the pinned one)
  CC            compiler to use
EOF
            exit 0 ;;
        *) die "unknown argument: $arg" ;;
    esac
done

DEMO=$(cd "$(dirname "$0")" && pwd)
# The demo sits two levels inside the wolftpm-examples checkout, and the
# sibling trees live beside that checkout - the layout build.sh and the
# source archive both use.
ROOT=${ROOT:-$(cd "$DEMO/../../.." && pwd)}
WOLFSSL=${WOLFSSL_SRC:-$ROOT/wolfssl}
PREFIX=${WOLFSSL_PQTLS:-$ROOT/wolfssl-pqtls}
PATCH=$DEMO/patches/wolfssl-hash-mldsa-tls.patch
WEAK_PATCH=$DEMO/patches/wolfssl-mldsa-weak-prehash.patch

say "trees"
printf '  wolfssl source   %s\n  wolfssl install  %s\n  demo             %s\n' \
    "$WOLFSSL" "$PREFIX" "$DEMO"

# autotools cannot build from a path containing a space, and the failure
# arrives several minutes in as a missing file whose name is the tail of the
# path. Say so now instead.
case "$ROOT" in
    *\ *) die "this path has a space in it:
   $ROOT
   autotools cannot build from here. Move the tree somewhere without a
   space - C:\\demo or ~/demo - and run this again." ;;
esac

# MSYS2 ships two compilers. The MSYS one at /usr/bin/gcc targets the POSIX
# emulation layer and cannot link a library the MinGW compiler built; the
# link fails with a wall of undefined __imp__errno, which reads as a broken
# wolfSSL rather than as the wrong compiler. Whichever builds wolfSSL has to
# build board_tls too, so pick one here and use it for both.
CONF_LIBS=""
case "$(uname -s)" in
    MSYS*|MINGW*)
        CC=${CC:-/mingw64/bin/gcc}
        CONF_LIBS="LIBS=-lws2_32"
        ;;
    *)
        CC=${CC:-cc}
        ;;
esac
export CC

say "tools"
for t in git make autoreconf "$CC"; do
    command -v "$t" > /dev/null || die "$t is not on PATH
   Install the autotools and a compiler:
     macOS    xcode-select --install && brew install autoconf automake libtool
     Debian   sudo apt install build-essential autoconf automake libtool git
     MSYS2    pacman -S --needed autotools libtool mingw-w64-x86_64-gcc git"
    printf '  %s\n' "$(command -v "$t")"
done

# Homebrew installs GNU libtool under a g prefix, because macOS already has
# an unrelated tool of that name. Point autoreconf at it when that is the
# only one present.
if ! command -v libtoolize > /dev/null && command -v glibtoolize > /dev/null
then
    export LIBTOOLIZE=glibtoolize
    printf '  %s (as libtoolize)\n' "$(command -v glibtoolize)"
fi

# Pinned so the handshake runs against the revision this was tested with,
# and the same one build.sh pins for the firmware. A newer tree works, with
# the second patch below.
WOLFSSL_REV=${WOLFSSL_REV:-21bb7a10f1}
if [ ! -d "$WOLFSSL" ]; then
    say "fetching wolfssl"
    # A full clone is over a gigabyte and most of it is history this never
    # reads; a blobless clone fetches contents on demand.
    git clone --filter=blob:none https://github.com/wolfSSL/wolfssl.git \
        "$WOLFSSL" || die "could not clone wolfssl"
fi
want=$(git -C "$WOLFSSL" rev-parse --verify --quiet "$WOLFSSL_REV^{commit}" \
    2>/dev/null)
if [ -z "$want" ] || [ "$(git -C "$WOLFSSL" rev-parse HEAD)" != "$want" ]; then
    git -C "$WOLFSSL" checkout --quiet "$WOLFSSL_REV" || die \
"wolfssl is not at $WOLFSSL_REV and could not be moved there.
   Check that revision out by hand, or set WOLFSSL_REV to the one you want."
fi
printf '  wolfssl at %s\n' "$(git -C "$WOLFSSL" rev-parse --short HEAD)"

# Re-running the script has to be safe, so a patch that is already in the
# tree is not an error.
apply_patch() {
    name=$(basename "$1")
    if git -C "$WOLFSSL" apply --check "$1" 2> /dev/null; then
        git -C "$WOLFSSL" apply "$1" || die "could not apply $name"
        printf '  %s applied\n' "$name"
    elif git -C "$WOLFSSL" apply --reverse --check "$1" 2> /dev/null; then
        printf '  %s already applied\n' "$name"
    else
        die "$name neither applies nor is already applied.
   The tree has its own changes in the files that patch touches. Restore
   them, or check out the pinned revision into a clean tree, and run this
   again."
    fi
}

# TLS 1.3 has no signature algorithm for the pre-hash form of ML-DSA, which
# is the only form this firmware signs, so the handshake needs a code point
# from the RFC 8446 private use range. Neither patch touches a file the
# firmware compiles, so patching a tree that built the firmware cannot
# change the image that was flashed.
say "patching wolfssl"
apply_patch "$PATCH"
# The second patch relaxes the FIPS 204 section 5.4 check that a pre-hash
# carry the collision strength of the parameter set. The part signs SHA-256
# only, which set 87 cannot satisfy, so a wolfSSL that enforces it rejects
# the signature at the argument check. The pinned revision predates the
# check and has nothing to relax, which is why the function it patches is
# the test for whether it is wanted at all.
if grep -q mldsa_check_hash_strength "$WOLFSSL/wolfcrypt/src/wc_mldsa.c" \
        2> /dev/null; then
    apply_patch "$WEAK_PATCH"
else
    printf '  %s not needed at this revision\n' "$(basename "$WEAK_PATCH")"
fi

# options.h records the defines the library was built with, so it is also
# how to tell a patched install from a stock one.
if [ "$force" -eq 0 ] && [ -f "$PREFIX/lib/libwolfssl.a" ] && \
   grep -q WOLFSSL_HASH_MLDSA_TLS "$PREFIX/include/wolfssl/options.h" \
       2> /dev/null; then
    say "wolfssl is already installed at $PREFIX - skipping (-f rebuilds)"
else
    say "building wolfssl (several minutes)"
    cd "$WOLFSSL" || die "no wolfssl tree at $WOLFSSL"
    ./autogen.sh || die "autogen.sh failed"
    # --enable-cryptocb is the mechanism the whole tab rests on: it is what
    # hands the handshake digest to the board. --enable-tls-mlkem-standalone
    # is not the default and the key exchange here is ML-KEM on its own
    # rather than paired with a classical curve. Static only, so board_tls
    # carries the library with it and a booth machine needs no DLL.
    cf="-DWOLFSSL_HASH_MLDSA_TLS -DWOLFSSL_MLDSA_ALLOW_WEAK_PREHASH"
    ./configure --prefix="$PREFIX" \
                --enable-cryptocb --enable-keygen --enable-experimental \
                --enable-mldsa --enable-mlkem --enable-tls-mlkem-standalone \
                --enable-certgen --enable-certreq --enable-certext \
                --enable-static --disable-shared \
                --disable-sys-ca-certs --enable-harden \
                CFLAGS="$cf" $CONF_LIBS \
        || die "configure failed"
    make || die "wolfssl build failed"
    make install || die "wolfssl install failed"
fi

say "building board_tls"
make -C "$DEMO/host/tls" clean > /dev/null 2>&1
make -C "$DEMO/host/tls" WOLFSSL_DIR="$PREFIX" || die "board_tls build failed"

say "result"
tool=$DEMO/host/tls/board_tls
[ -f "$tool" ] || tool=$tool.exe
[ -f "$tool" ] || die "board_tls was not produced"
printf '  %s\n' "$tool"
printf '\nRestart the host server and press Run on the PQ TLS tab.\n'
