# fwTPM on Xilinx ZCU102 Cortex-R5 (Lock-Step)

Firmware TPM 2.0 (from [wolfTPM](https://github.com/wolfSSL/wolfTPM) `fwtpm`
branch) on the Xilinx Zynq UltraScale+ MPSoC ZCU102. The fwTPM server runs
bare-metal on the Cortex-R5 RPU pair in **lock-step mode** (RPU0+RPU1 paired
into a single safety-class processor, 256 KB merged TCM). NV defaults to a
volatile 64 KB DDR mirror; building with `-DFWTPM_NV_QSPI` switches it to a
persistent 64 KB QSPI flash partition. PetaLinux on the A53 APU acts as TPM
client over **OpenAMP RPMsg** loaded via Linux `remoteproc`.

## Architecture

```
+---------------------------------------------------+
|  ZCU102                                           |
|                                                   |
|  +-----------------+      OpenAMP RPMsg           |
|  |  APU (A53 x4)   |<------------------+          |
|  |  PetaLinux      |   IPI doorbell    |          |
|  |  remoteproc-r5  |   vrings in DDR   v          |
|  +-----------------+      +---------------------+ |
|                           |  RPU (R5 lock-step) | |
|                           |  bare-metal fwTPM   | |
|                           |  XQspiPsu NV (QSPI) | |
|                           +---------------------+ |
|                                    ^              |
|                                    |              |
|                              +-----+------+       |
|                              |  QSPI flash|       |
|                              |  fwtpm-nv  |       |
|                              |  64 KB     |       |
|                              +------------+       |
+---------------------------------------------------+
```

## Tested Targets

| Board  | Cluster Mode | Status                                                | Date       |
|--------|--------------|-------------------------------------------------------|------------|
| ZCU102 | R5 lock-step | Tier-1 + Tier-2 ELF link verified                     | 2026-05-06 |
| ZCU102 | (split, stock SD) | remoteproc loads ELF; resource_table + rpmsg vdev visible | 2026-05-07 |
| ZCU102 | R5 lock-step | Custom PetaLinux 2025.2 image (1 MiB carveout) builds | 2026-05-07 |
| ZCU102 | R5 lock-step | IRQ-driven rpmsg + 4-cmd smoke (`fwtpm_rpmsg_test`)   | 2026-05-15 |
| ZCU102 | R5 lock-step | PQC build (`FWTPM_ENABLE_PQC`, opt-in) advertises PT_ML_PARAMETER_SETS=0x3F (ML-KEM 512/768/1024 + ML-DSA 44/65/87); default build is PQC-off (transport cannot carry PQC payloads) | 2026-05-15 |

## Files

```
firmware/fwtpm-r5/
  Makefile                  Vitis 2025.2 armr5-none-eabi cross-build
  user_settings.h           wolfSSL + wolfTPM config (SP-32 math, no DH)
  lscript.ld                code/data/heap/stack in 1 MiB DDR carveout
  boot.S                    R5 reset entry, BSS zero, jump to main()
  mpu_setup.c               2-region MPU (Normal DDR/TCM + Device IPI)
  main.c                    MPU init -> HAL init -> FWTPM_Init -> rpmsg server
  fwtpm_clock_zynqmp.c      FWTPM_CLOCK_HAL via R5 PMU CCNT
  fwtpm_nv_qspi.c           FWTPM_NV_HAL backed by XQspiPsu (Tier-2,
                            -DFWTPM_NV_QSPI; calls pmu_request_node
                            before LookupConfig so PMU brings up clock)
  fwtpm_nv_ram.c            FWTPM_NV_HAL volatile DDR (default)
  pmu_eemi.c                Minimal EEMI client over RPU0 IPI for
                            PM_REQUEST_NODE / PM_RELEASE_NODE (Tier-2)
  fwtpm_rpmsg.c             OpenAMP rpmsg endpoint (Tier-2)
  rsc_table.c               remoteproc resource table (Tier-2)
  platform_info.c           ZCU102 IPI/carveout glue (Tier-2)
  zcu102_r5.h               local HAL prototypes
  include/xparameters.h     hand-rolled BSP-free parameters
petalinux/                  PetaLinux 2025.2 project (lock-step + carveouts)
  project-spec/configs/config              ext4-on-SD root, ZCU102 default
  project-spec/meta-user/conf/petalinuxbsp.conf       IMAGE_INSTALL adds
  project-spec/meta-user/recipes-bsp/device-tree/files/system-user.dtsi
                                            1 MiB rproc + lockstep + QSPI nv
  project-spec/meta-user/recipes-bsp/fwtpm-r5/        /lib/firmware/ stage
  project-spec/meta-user/recipes-apps/fwtpm-rpmsg-test/  APU smoke client
  project-spec/meta-user/recipes-kernel/linux/linux-xlnx/openamp.cfg
                                            REMOTEPROC + RPMSG_CHAR + IPI
  README.md                                 reproducible build/deploy steps
deploy-to-sdcard.sh         Two-partition (FAT32+ext4) SD writer (host-side)
linux-client/
  fwtpm_rpmsg_test.c        APU TPM smoke test over /dev/rpmsg_ctrl0
                            (--pqc adds PT_ML_PARAMETER_SETS probe)
  fwtpm_pqc_test.c          ML-KEM-768 encap/decap + ML-DSA-65
                            sign/verify via wolfTPM2 wrappers over
                            an rpmsg TPM2_IoCb (EXPERIMENTAL: needs the
                            FWTPM_ENABLE_PQC firmware + a transport
                            fragmentation layer not yet implemented)
  build-pqc-test.sh         Reproducible cross-compile recipe for
                            fwtpm_pqc_test (aarch64-static-linked
                            against sibling wolfssl + wolftpm with
                            --enable-pqc + simple 5-arg ioCb)
```

Tier-1 = compiles with no Vitis BSP, against the wolfSSL/wolfTPM sibling
checkouts. Tier-2 = needs a generated Vitis BSP for `psu_cortexr5_0`
(bspconfig.h, libxil.a) plus libmetal + open-amp built libraries.

## Prerequisites

- Vitis 2025.2 (provides the `armr5-none-eabi` toolchain at
  `/opt/Xilinx/2025.2/gnu/armr5/lin/gcc-arm-none-eabi/`)
- `wolfssl` and `wolftpm` source trees as siblings of `wolftpm-examples`
  (default sibling layout, override via `WOLFSSL_DIR=` / `WOLFTPM_DIR=`)
- A stock ZCU102 `.xsa` (default ZCU102 PetaLinux BSP works -- no PL changes)
- PetaLinux 2025.2 (for the APU side)

## Build

The default `make` target builds Tier-1 only -- all wolfSSL, wolfTPM, and
the BSP-free local sources to .o files. This is the compile-clean check
that does not need a Vitis BSP.

```bash
cd firmware/fwtpm-r5
make            # Tier-1 .o files only (compile-clean check)
make help       # full target list
```

Final ELF link requires Tier-2 sources and a generated standalone BSP.

### BSP generation (Tier-2)

Vitis 2025.2 ships only the Unified flow (xsct is deprecated and missing
its Eclipse backend). Use the Vitis Python CLI:

```python
# build_bsp.py
import vitis
ws = "/tmp/zcu102_r5_bsp/ws"
xsa = "/opt/Xilinx/2025.2/data/embeddedsw/lib/fixed_hwplatforms/zcu102.xsa"
client = vitis.create_client(workspace=ws)
plat = client.create_platform_component(
    name="zcu102_r5_ls", hw_design=xsa,
    cpu="psu_cortexr5_0", os="standalone",
    domain_name="domain_r5", compiler="gcc")
dom = plat.get_domain("domain_r5")
dom.set_lib(lib_name="libmetal")
dom.set_lib(lib_name="openamp")
plat.build()
```

```bash
source /opt/Xilinx/2025.2/Vitis/settings64.sh
vitis -s build_bsp.py
```

The BSP appears at
`<ws>/zcu102_r5_ls/psu_cortexr5_0/domain_r5/bsp/`. Symlink (or copy)
that tree into `firmware/fwtpm-r5/bsp/psu_cortexr5_0`:

```bash
ln -s <ws>/zcu102_r5_ls/psu_cortexr5_0/domain_r5/bsp \
      firmware/fwtpm-r5/bsp/psu_cortexr5_0
```

The BSP includes `libxil.a`, `libxilstandalone.a`, `libxiltimer.a`,
`libmetal.a`, `libopen_amp.a` plus headers under `include/{metal,openamp}`.

Note: lockstep is configured at the *platform* layer in 2025.2 SDT
flow (cluster-mode = 1 in the device tree consumed by Linux remoteproc),
not at BSP generation time. The R5 ELF itself is identical for split
or lock-step; the difference is only how the cluster is wired up by
the APU side.

Then:

```bash
cd firmware/fwtpm-r5
make elf        # Builds Tier-1 + Tier-2, links fwtpm_r5.elf
```

## Boot Flow

1. ZCU102 boots PetaLinux on A53 (FSBL -> PMUFW -> ATF -> U-Boot -> Linux).
2. R5 cluster is held in lock-step reset by FSBL.
3. From userspace, `remoteproc` loads `fwtpm_r5.elf` and starts the R5:

```bash
echo fwtpm_r5.elf > /sys/class/remoteproc/remoteproc0/firmware
echo start         > /sys/class/remoteproc/remoteproc0/state
```

4. R5 banner is visible on PSU_UART1 and via the trace ring:

```bash
cat /sys/kernel/debug/remoteproc/remoteproc0/trace0
```

Expected:

```
mpu: enabled
=== wolfTPM fwTPM on ZCU102 R5 (lock-step) ===
step 1..8: NV + clock HAL setup OK
step 10: FWTPM_Init done
fwTPM initialized successfully
step 11: rpmsg init
step 12: rpmsg init done
rpmsg ready, waiting for commands
```

5. Linux client connects:

```bash
/usr/bin/fwtpm_rpmsg_test
```

Expected output (with rpmsg working end-to-end):

```
TPM2_Startup    tag=0x8001 size=10 rc=0x00000000  OK
TPM2_SelfTest   tag=0x8001 size=10 rc=0x00000000  OK
TPM2_GetRandom  tag=0x8001 size=44 rc=0x00000000  OK
TPM2_GetCap     tag=0x8001 size=27 rc=0x00000000  OK
all 4 tests passed
```

## Memory Map

| Region            | Address      | Size    | Notes                       |
|-------------------|--------------|---------|-----------------------------|
| TCM (lock-step)   | 0x00000000   | 256 KiB | unused; available for hot   |
|                   |              |         | code or vector table        |
| R5 firmware DDR   | 0x3ED00000   | 1 MiB   | text + rodata + data + bss  |
|                   |              |         | + heap (256K) + stack (64K) |
| RPMsg vring 0     | 0x3EE00000   | 16 KiB  | R5 -> APU                   |
| RPMsg vring 1     | 0x3EE04000   | 16 KiB  | APU -> R5                   |
| RPMsg buffer pool | 0x3EE08000   | 256 KiB | shared payload buffers      |
| QSPI fwtpm-nv     | 0x07FF0000   | 64 KiB  | persistent NV, opt-in       |

## RNG / Entropy

wolfCrypt's HASH_DRBG is seeded by the MemUse entropy source
(`HAVE_ENTROPY_MEMUSE`): memory-access and timer jitter conditioned
through SHA3-256, with SP800-90B RCT/APT health tests that fail closed
(seeding -- and `FWTPM_Init` -- aborts rather than emit weak keys). The
high-resolution time source is the R5 PMU cycle counter, supplied via
`CUSTOM_ENTROPY_TIMEHIRES` (`fwtpm_clock_zynqmp.c`); monotonic ms time
comes from TTC3. The Zynq UltraScale+ CSU TRNG is APU/PMU-side and not
directly reachable from the lock-step R5; mixing a hardware TRNG in via
a PMU mailbox service remains a possible future enhancement.

## Known Issues / Roadmap

- **PetaLinux integration**: complete in `petalinux/`. See
  `petalinux/README.md` for build flow. Includes the rproc carveout
  + lock-step DTS overlay, OpenAMP kernel fragment, fwtpm-r5 firmware
  staging recipe, and fwtpm-rpmsg-test app recipe.
- **MPU**: `mpu_setup.c` programs two regions before `main()` body
  runs -- a 2 GiB Normal-non-cacheable region covering TCM + DDR
  (needed for `LDREX`/`STREX` to complete behind libmetal's atomics),
  and a 256 MiB Device region at `0xF0000000` spanning the entire
  LPD peripheral aperture (GIC400 at `0xF9000000` + IPI at
  `0xFF300000` + UART/QSPI/CSU/PMU at `0xFF000000+`). Earlier
  revisions used a 32 MiB Device region at `0xFE000000` which
  excluded the GIC -- the first GIC register access in `gic_init()`
  faulted into the silent `_data_abort` spin. Caches stay off in V1
  so `.trace_buffer` remains coherent with the APU without explicit
  dcache flushes. Follow-on: enable I/D caches with per-region
  attributes once the rpmsg transport is stable.
- **IRQ-driven rpmsg**: `platform_info.c::gic_init()` initializes
  XScuGic and connects an `ipi_isr` for IPI_1 (GIC SPI 33,
  `XPAR_PSU_IPI_1_INT_ID = 65`). `boot.S` `_irq` trampoline
  saves caller-saved registers and dispatches via `_irq_dispatch`.
  The server loop in `fwtpm_rpmsg.c::zynqmp_r5_rpmsg_serve()`
  blocks in `wfi` until the ISR posts, then drains both vrings
  via `remoteproc_get_notification(RSC_NOTIFY_ID_ANY)`. R5 sits
  near 0% CPU between commands instead of busy-polling.
- **IPI mailbox DT**: `system-user.dtsi` declares `zynqmp_ipi1`
  with one `ipi_mailbox_rpu0` child (host `xlnx,ipi-id=7`, dest
  `xlnx,ipi-id=1`), and r5f@0 binds it via `mboxes`/`mbox-names`.
  Reg layout matches the OpenAMP V6 reference DT.
- **NV is RAM-backed by default; persistent QSPI is opt-in via
  `-DFWTPM_NV_QSPI`**. With Linux's `spi-zynqmp-qspi` driver
  disabled in DTS, PMUFW power-gates the QSPI controller and the
  raw R5 driver hangs at the first WIP poll (observed on hardware,
  trace breadcrumbs `qspi: A..E` in `fwtpm_nv_qspi.c`). The QSPI
  HAL now front-loads a `PM_REQUEST_NODE(NODE_QSPI)` EEMI call to
  PMUFW via the RPU0 IPI block (`pmu_eemi.c`). PMU brings up the
  QSPI clock + power domain, then `XQspiPsu_LookupConfig` /
  `XQspiPsu_PolledTransfer` see a live controller. Build with
  `make elf EXTRA_CFLAGS=-DFWTPM_NV_QSPI`; verify on hardware
  by defining a TPM NV index, write data, power-cycle, and
  re-read. An rpmsg-mediated NV proxy (Linux owns MTD, R5 sends
  read/write requests over a second endpoint) is a cleaner
  alternative for production.
- **rpmsg round-trip verified end-to-end on hardware**:
  ```
  Startup         tag=0x8001 size=10 rc=0x00000000  OK
  SelfTest        tag=0x8001 size=10 rc=0x00000000  OK
  GetRandom(32)   tag=0x8001 size=44 rc=0x00000000  OK
  GetCapability   tag=0x8001 size=27 rc=0x00000000  OK
  all 4 tests passed
  ```
  All four TPM2 commands round-trip Linux APU <-> R5 RPU via
  OpenAMP rpmsg + IPI mailbox.
- **Linux client over rpmsg-char**: `linux-client/fwtpm_rpmsg_test.c`
  is the V1 smoke test (Startup/SelfTest/GetRandom/GetCapability).
  A wolfTPM `--enable-swtpm=rpmsg` transport that runs the full
  example suite is a follow-on.
- **DEBUG_WOLFTPM uses bare `printf`/`fprintf(stderr, ...)`.**
  Bare-metal R5 has no working newlib stdio (no `_write` backend),
  so DEBUG-build logging in `fwtpm_nv.c` hangs at the first
  message. Worked around in `user_settings.h` (DEBUG_WOLFTPM = 0).
  Filed as a wolfTPM-side cleanup candidate: route the debug prints
  through `WOLFSSL_MSG`/`WOLFSSL_MSG_EX` (which respect
  `wolfSSL_SetLoggingCb`) so embedded ports can hook them.
- **PQC (TCG TPM 2.0 v1.85) is OFF by default; opt-in and
  experimental.** `WOLFTPM_V185` + `WOLFSSL_HAVE_MLKEM` +
  `HAVE_DILITHIUM` are gated behind `FWTPM_ENABLE_PQC` (see
  `firmware/fwtpm-r5/user_settings.h` and the `Makefile`). The
  default build is plain TPM 2.0 and advertises no ML parameter
  sets. The reason is transport: the OpenAMP rpmsg pool is 256 x
  512-byte buffers, so a single message carries only ~496 bytes of
  payload, while every ML-KEM / ML-DSA payload (e.g. ML-KEM-768
  ciphertext 1088 B, ML-DSA-65 signature 3309 B) exceeds one frame.
  This transport has no fragmentation/reassembly layer, so PQC
  cannot round-trip end-to-end yet; the firmware now rejects any
  response larger than one frame with a structured `TPM_RC_SIZE`
  error rather than truncating (`fwtpm_rpmsg.c`).

  To experiment with the PQC build (advertisement + on-R5 keygen),
  rebuild the firmware with PQC enabled:

  ```bash
  cd firmware/fwtpm-r5 && make FWTPM_ENABLE_PQC=1 elf
  ```

  The Linux helper `linux-client/fwtpm_pqc_test.c` (built via
  `linux-client/build-pqc-test.sh`) drives ML-KEM-768 encap/decap
  and ML-DSA-65 sign/verify over the rpmsg `TPM2_IoCb`. It is
  **experimental and will not complete over the current single-frame
  transport** until fragmentation lands; it is kept for that
  follow-on. The wolfTPM 3.10 combo `--enable-pqc --enable-fwtpm`
  forces `WOLFTPM_ADV_IO` (TIS 6-arg cb), incompatible with the
  simple rpmsg send/receive cb, so the client is built without
  `--enable-fwtpm`. The image ships no SSH server and no sudo, so
  copy the static binary onto the SD card's root filesystem during
  deploy and run it as **root** on the serial console:

  ```
  cd linux-client && ./build-pqc-test.sh
  # copy /tmp/fwtpm_pqc_test onto the card's rootfs (e.g. /usr/bin)
  # then, on the board's root console:
  /tmp/fwtpm_pqc_test
  ```

## See Also

- [STM32H5 fwTPM port](../STM32/fwtpm-stm32h5/) -- single-MCU mssim/UART
- [PolarFire SoC fwTPM port](../Microchip/fwtpm-polarfire-miv/) -- AMP
  RISC-V hart-4 with shared-memory TIS
- [wolfTPM fwTPM HAL guide](https://github.com/wolfSSL/wolftpm/blob/master/src/fwtpm/ports/README.md)
