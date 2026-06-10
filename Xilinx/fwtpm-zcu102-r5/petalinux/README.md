# PetaLinux 2025.2 image for ZCU102 fwTPM

This directory holds a self-contained PetaLinux 2025.2 project that
boots a stock-XSA ZCU102, configures the RPU cluster for **lock-step
mode**, reserves a 1 MiB DDR carveout for the R5 fwTPM firmware
(matching `firmware/fwtpm-r5/lscript.ld`), and installs:

- `/lib/firmware/fwtpm_r5.elf` -- the R5 firmware blob (the recipe
  installs to `${nonarch_base_libdir}/firmware`, i.e. `/lib/firmware`)
- `/usr/bin/fwtpm_rpmsg_test` -- the smoke-test client

No vendor- or customer-private hardware definitions are referenced.
Everything is built from the public stock ZCU102 `.xsa` shipped under
`<vitis>/data/embeddedsw/lib/fixed_hwplatforms/zcu102.xsa`.

## What this image enables that stock PetaLinux does not

| Feature                     | Stock 2025.1 SD                       | This image                           |
|-----------------------------|---------------------------------------|--------------------------------------|
| RPU cluster mode            | split (remoteproc0 + remoteproc1)     | **lock-step** (remoteproc0 only)     |
| `rproc_0_reserved` size     | 384 KiB                               | **1 MiB** (fits full fwtpm_r5.elf)   |
| QSPI `fwtpm-nv` partition   | not declared                          | **64 KiB at 0x07FF0000**             |
| OpenAMP kernel options      | mostly default                        | `CONFIG_RPMSG_CHAR=y` + ZynqMP IPI   |
| `/lib/firmware/fwtpm_r5.elf` | absent                                | **staged at boot**                   |
| `/usr/bin/fwtpm_rpmsg_test` | absent                                | **installed**                        |

## Prerequisites

- PetaLinux 2025.2 host install (default: `~/petalinux/2025.2/`)
- Built R5 ELF at `../firmware/fwtpm-r5/fwtpm_r5.elf` (Tier-2 link --
  see `../firmware/fwtpm-r5/README.md`)
- ~30 GB free disk for build artifacts; 30-60 min first build

## Build flow (reproducible from a clean checkout)

```bash
# 1) Source the PetaLinux 2025.2 environment.
source ~/petalinux/2025.2/settings.sh

# 2) From wolftpm-examples/Xilinx/fwtpm-zcu102-r5/, create the project.
#    (The committed petalinux/ tree already has all our overlays, so
#    on a fresh checkout you skip petalinux-create and instead do
#    petalinux-config to import the stock XSA into project-spec/hw-description.)
cd Xilinx/fwtpm-zcu102-r5

# Stage the public stock ZCU102 hardware archive.
mkdir -p hw
cp /opt/Xilinx/2025.2/data/embeddedsw/lib/fixed_hwplatforms/zcu102.xsa hw/

# Import the XSA into the project (silent, no menuconfig).
cd petalinux
petalinux-config --silentconfig --get-hw-description=../hw/

# 3) Stage the current firmware ELF for the recipe. This links the ELF
#    (needs the BSP under ../firmware/fwtpm-r5/bsp/) and copies it into
#    the recipe's files/ dir, so the image bundles the firmware that
#    matches the current source rather than a stale blob. Re-run this
#    whenever the firmware changes, before rebuilding the image.
make -C ../firmware/fwtpm-r5 stage

# 4) Build kernel, rootfs, U-Boot, FSBL, etc. (slow first time)
petalinux-build

# 5) Package the boot image.
petalinux-package boot --u-boot --force
# Output:
#   images/linux/BOOT.BIN            (FSBL + PMUFW + ATF + U-Boot)
#   images/linux/image.ub            (kernel + DTB + ramdisk-FIT)
#   images/linux/boot.scr            (U-Boot script)
#   images/linux/rootfs.tar.gz       (root filesystem)
```

## SD card layout

ZCU102 boots from the SD card via the FSBL when boot-mode dip switches
are set to SD-1.8V (mode 5 = `0b1000` on SW6). Two-partition layout:

| Partition | Filesystem | Contents                                       |
|-----------|------------|------------------------------------------------|
| 1 (boot)  | FAT32      | `BOOT.BIN`, `image.ub`, `boot.scr`             |
| 2 (root)  | ext4       | extracted `rootfs.tar.gz`                      |

A helper script is provided at `../deploy-to-sdcard.sh`. Run on the
host (requires sudo for raw block-device access):

```bash
# Find the SD card -- look for the USB-attached disk in lsblk
lsblk

# Full deploy (boot partition + rootfs):
sudo ../deploy-to-sdcard.sh /dev/sdX
```

## Boot verification

After plugging the SD into the ZCU102 and powering on, the APU console
(USB-UART CP2108 channel 0, 115200 8N1) shows the standard PetaLinux
boot.

> Security note: this image ships **local-console-only**. There is no
> SSH server, no network login, no default password, and no `sudo`. The
> serial console (UART0) auto-logs-in as `root` so the operator can
> drive remoteproc locally; `root` has no password set and the
> `petalinux` account is `passwd-expire` (must set a password on first
> use). Because the only privileged path is the physical UART, there is
> no remote attack surface. If you need networked admin, provision your
> own SSH server and keys at deployment. The commands below run as
> **root** on the serial console; there is no `sudo`, so do not prefix
> them with it.

At the auto-login root serial console:

```bash
# Lock-step proof: a single remoteproc instance with the lockstep DT.
ls /sys/class/remoteproc/
# -> remoteproc0       (only -- not remoteproc1)
cat /proc/device-tree/zynqmp-rpu/xlnx,cluster-mode | xxd | head -1
# -> 00000000: 0000 0001                                ....

# fwTPM firmware is staged (recipe installs to /lib/firmware)
ls -la /lib/firmware/fwtpm_r5.elf

# Load and start the R5 (as root, no sudo)
echo fwtpm_r5.elf > /sys/class/remoteproc/remoteproc0/firmware
echo start         > /sys/class/remoteproc/remoteproc0/state

# Confirm it booted
dmesg | tail -10
# Expect: "remote processor ffe00000.r5f is now up"
#         "virtio_rpmsg_bus virtio0: rpmsg host is online"

# Smoke test
fwtpm_rpmsg_test
# Expect: 4 OK lines
```

## Re-building just the kernel + DTB after a config tweak

```bash
petalinux-build -c kernel
petalinux-build      # re-link the FIT image
sudo ../deploy-to-sdcard.sh --boot-only /dev/sdX
```

A full rootfs rebuild is required when adding/removing rootfs packages
(`fwtpm-r5`, `fwtpm-rpmsg-test`, etc.).

## Files of interest

```
petalinux/project-spec/
  configs/config                                   # rootfs = ext4 SD
  meta-user/conf/petalinuxbsp.conf                 # IMAGE_INSTALL adds
  meta-user/recipes-bsp/device-tree/files/
    system-user.dtsi                               # 1 MiB carveout, lockstep
  meta-user/recipes-bsp/fwtpm-r5/
    fwtpm-r5_0.1.0.bb                              # /lib/firmware/ stage
    files/fwtpm_r5.elf                             # built ELF (copied in)
  meta-user/recipes-apps/fwtpm-rpmsg-test/
    fwtpm-rpmsg-test_0.1.0.bb                      # APU smoke-test client
    files/fwtpm_rpmsg_test.c                       # source (copy of linux-client/)
  meta-user/recipes-kernel/linux/
    linux-xlnx_%.bbappend                          # adds openamp.cfg
    linux-xlnx/openamp.cfg                         # remoteproc + rpmsg + IPI
```
