#!/bin/bash
# deploy-to-sdcard.sh -- write the PetaLinux build artifacts onto a
# two-partition ZCU102 SD card (FAT32 boot + ext4 root). Run on the
# host as root.
#
# Usage:
#   sudo deploy-to-sdcard.sh /dev/sdX                # full deploy
#   sudo deploy-to-sdcard.sh --boot-only /dev/sdX    # only boot partition
#
# Layout the card must already have:
#   /dev/sdX1  FAT32, 256+ MiB, label "boot"
#   /dev/sdX2  ext4,  4+ GiB,   label "root" (or "rootfs" -- both accepted)
#
# Format from scratch (DESTROYS DATA, do this once):
#   sudo parted /dev/sdX mklabel msdos
#   sudo parted -a optimal /dev/sdX mkpart primary fat32 1MiB 257MiB
#   sudo parted -a optimal /dev/sdX mkpart primary ext4 257MiB 100%
#   sudo mkfs.vfat -F32 -n boot /dev/sdX1
#   sudo mkfs.ext4 -L root /dev/sdX2
#
# Copyright (C) 2006-2026 wolfSSL Inc.

set -euo pipefail

usage() {
    echo "Usage: sudo $0 [--boot-only] /dev/sdX" >&2
    exit 2
}

BOOT_ONLY=0
if [[ "${1:-}" == "--boot-only" ]]; then
    BOOT_ONLY=1; shift
fi
DEV="${1:-}"
[[ -b "$DEV" ]] || usage

if [[ "$EUID" -ne 0 ]]; then
    echo "must run as root (try: sudo $0 $@)" >&2
    exit 1
fi

# Refuse to clobber a system disk
if [[ "$DEV" =~ ^/dev/(sda|nvme0n1|mmcblk0)$ ]]; then
    echo "refusing to write to $DEV (looks like a system disk)" >&2
    exit 1
fi

PROJ="$(cd "$(dirname "$0")" && pwd)/petalinux"
IMG="$PROJ/images/linux"
[[ -f "$IMG/BOOT.BIN" ]] || { echo "no $IMG/BOOT.BIN -- run petalinux-build + petalinux-package first" >&2; exit 1; }
[[ -f "$IMG/image.ub" ]] || { echo "no $IMG/image.ub" >&2; exit 1; }

# Resolve partitions by filesystem label first (boot + root/rootfs), so
# cards partitioned with non-contiguous numbering (e.g. sdb1 + sdb4 from
# a parted extended-partition layout) work alongside the conventional
# sdX1+sdX2. The documented format recipe labels the root partition
# "root", but accept "rootfs" too (some recipes use it). Falls back to
# ${DEV}1/${DEV}2 only if no label matches (fresh card just formatted).
P1=$(readlink -f /dev/disk/by-label/boot   2>/dev/null || true)
P2=$(readlink -f /dev/disk/by-label/root   2>/dev/null || \
     readlink -f /dev/disk/by-label/rootfs 2>/dev/null || true)
[[ -b "$P1" ]] || P1="${DEV}1"
[[ -b "$P2" ]] || P2="${DEV}2"
# Sanity: both resolved partitions must live on the requested device.
[[ "$P1" == "${DEV}"* ]] || { echo "label 'boot' resolves to $P1, not on $DEV" >&2; exit 1; }
[[ "$P2" == "${DEV}"* ]] || { echo "root partition resolves to $P2 (label root/rootfs), not on $DEV" >&2; exit 1; }
[[ -b "$P1" ]] || { echo "$P1 not present (partition the card first)" >&2; exit 1; }
[[ -b "$P2" ]] || { echo "$P2 not present (partition the card first)" >&2; exit 1; }

MNT_BOOT=$(mktemp -d)
MNT_ROOT=$(mktemp -d)
cleanup() {
    umount "$MNT_BOOT" 2>/dev/null || true
    umount "$MNT_ROOT" 2>/dev/null || true
    rmdir "$MNT_BOOT" "$MNT_ROOT" 2>/dev/null || true
}
trap cleanup EXIT

echo "==> Mounting $P1 -> $MNT_BOOT"
mount "$P1" "$MNT_BOOT"

echo "==> Copying boot artifacts"
cp -v "$IMG/BOOT.BIN"  "$MNT_BOOT/"
cp -v "$IMG/image.ub"  "$MNT_BOOT/"
[[ -f "$IMG/boot.scr" ]] && cp -v "$IMG/boot.scr" "$MNT_BOOT/"
sync
umount "$MNT_BOOT"

if [[ "$BOOT_ONLY" -eq 1 ]]; then
    echo "==> Boot-only deploy complete."
    exit 0
fi

echo "==> Mounting $P2 -> $MNT_ROOT"
mount "$P2" "$MNT_ROOT"

[[ -f "$IMG/rootfs.tar.gz" ]] || { echo "no $IMG/rootfs.tar.gz" >&2; exit 1; }

# Safety net before the destructive wipe: confirm the mounted partition
# is actually an ext4 root filesystem on the requested device, not some
# other partition the label fallback may have selected. Abort otherwise.
ROOT_FSTYPE=$(findmnt -no FSTYPE "$MNT_ROOT" 2>/dev/null || true)
if [[ "$ROOT_FSTYPE" != "ext4" ]]; then
    echo "refusing to wipe $P2: filesystem is '${ROOT_FSTYPE:-unknown}', not ext4" >&2
    exit 1
fi
ROOT_LABEL=$(blkid -o value -s LABEL "$P2" 2>/dev/null || true)
case "$ROOT_LABEL" in
    root|rootfs|"") ;;  # expected labels, or unlabeled fresh card
    *) echo "refusing to wipe $P2: label '$ROOT_LABEL' is not root/rootfs" >&2; exit 1 ;;
esac

echo "==> Wiping ext4 root contents on $P2 (label='${ROOT_LABEL:-none}')"
rm -rf "$MNT_ROOT"/* "$MNT_ROOT"/.[!.]* 2>/dev/null || true

echo "==> Extracting rootfs.tar.gz to $P2 (slow)"
tar -xpf "$IMG/rootfs.tar.gz" -C "$MNT_ROOT"

sync
umount "$MNT_ROOT"
echo "==> Full deploy complete."
