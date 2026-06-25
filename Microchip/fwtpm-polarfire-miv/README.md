# fwTPM on Microchip PolarFire SoC MPFS250T Video Kit

Firmware TPM 2.0 (from [wolfTPM](https://github.com/wolfSSL/wolfTPM) `fwtpm`
branch) on PolarFire SoC, targeting IPsec ESP long-term key storage.

The fwTPM server runs bare-metal in M-mode on U54 hart 4 while Linux
runs on harts 1-3 via HSS AMP. Build lives at `firmware/fwtpm-u54/`.

## Build: `firmware/fwtpm-u54/`

### Architecture

- HSS AMP boot: hart 0 (E51) runs HSS, harts 1–3 run Linux (S-mode under
  OpenSBI), hart 4 runs the fwTPM payload at `0x91C00000` in M-mode with
  `skip-opensbi: true`.
- Transport: mailbox + console ring + TIS register block in shared memory
  (16 KB). Hart 4 polls `mbox.cmd_ready`. Cross-hart cache coherency is the
  key constraint: the default `DDR_WCB` build places the mailbox in a
  non-cached DDR window so it is coherent for hart 4. The server -> Linux
  path is reliable; the interactive round-trip is being hardened -- see
  "Transport and cache coherency" below. Backend is build-selectable
  (`FWTPM_XPORT`).
- NV: 64 KB RAM-backed (volatile, lost on reset). FRAM driver swappable
  via the same `FWTPM_NV_HAL` interface.
- Clock: CLINT `mtime` (RDTIME CSR is illegal in M-mode on this part).

### Shared-memory layout (`fwtpm_tis_mpfs.h`)

The mailbox lives in **L2 LIM** at `0x08000000`. LIM is the part of
the 2 MB L2 cache SRAM that the cache controller is configured to
expose as scratchpad memory; every master (E51, U54_1–4, AXI/DMA)
reaches it through the L2 bus.

```
0x08000000  FWTPM_MPFS_MAILBOX   (64 B  — mailbox + boot debug + trap fields)
0x08000040  FWTPM_CONSOLE_RING   (4 KB — server stdout, readable from /dev/mem)
0x08001040  FWTPM_TIS_REGS       (~8.2 KB — TIS regs + cmd/rsp FIFOs)
0x08004000  end
```

#### Confirming HSS reserves enough LIM

The default Video Kit HSS partitions the 2 MB L2 SRAM into 4 ways
scratchpad + 8 ways cache + 4 ways LIM (= 512 KiB at
`0x08000000-0x0807FFFF`). Verify on a target by typing
`debug l2cache` at the HSS console (UART0):

```
>> debug l2cache
L2 Cache Configuration:
    L2-Scratchpad:  4 ways (512 KiB)
         L2-Cache:  8 ways (1024 KiB)
           L2-LIM:  4 ways (512 KiB)
```

We use 16 KiB at the bottom of LIM; the rest is free for additional
clients (extra TPM contexts, log ring, etc.) up to 512 KiB without
further HSS work. To grow LIM beyond 512 KiB, reduce the cache or
scratchpad way count in HSS's `mss_sw_config.h` and rebuild — see
`hart-software-services/baremetal/polarfire-soc-bare-metal-library/src/platform/mpfs_hal/common/mss_l2_cache.c`
for the way-mask layout.

#### Linux <-> hart 4 transport and cache coherency

The hard part of this port is cache coherency between cache-coherent Linux
(U54 harts 1-3 plus the shared L2) and the bare-metal hart 4. The U54 L1
data cache is write-back with **no cache-maintenance instruction** (the
core predates Zicbom), so a *cacheable* shared mailbox is not coherent for
hart 4: it stale-reads Linux's writes from its own L1d, and its writes do
not reliably reach the shared L2. The mailbox must live in genuinely
non-cached memory.

Observed on the MPFS250T Video Kit with the stock Yocto HSS:

- **server -> Linux** (hart 4 writes, Linux reads): reliable through the
  console ring and the mailbox status fields. The live demo uses this path.
- **Linux -> hart 4** (Linux writes a command, hart 4 reads it): reliable
  only when the mailbox is non-cached for hart 4. The interactive TIS
  round-trip (driving TPM commands from Linux) is still being
  hardened over the write-combine DDR window and is not yet dependable.

`FWTPM_XPORT=DDR_WCB` (the default) puts the mailbox in the 0xC0000000
non-cached DDR window, which both hart 4 and Linux access uncached.
`LIM_CACHED` works only under an HSS that configures hart-4 PMA for the LIM
region (an earlier bring-up's HSS did -- its boot log showed
`boot_service(u54_4) :: SetupPMP`); the stock Yocto HSS does not.

##### What works today: TPM 2.0 caps on hart 4

Because server -> Linux is reliable, hart 4 runs real TPM 2.0 commands
itself at startup -- `TPM2_Startup`, `TPM2_GetCapability`
(manufacturer/vendor), and `TPM2_GetRandom` (seeded by the System
Controller hardware TRNG) -- and prints the results to the console ring.
Read them from Linux:

```bash
python3 ./linux-client/fwtpm_caps.py
```

The console-ring output includes, e.g.:

```
=== fTPM TPM2_GetCapability (executed on hart 4) ===
  Manufacturer  = 0x574f4c46  "WOLF"
  VendorString  = 0x776f6c66  "wolf"
  FirmwareVer   = 0x00000000
  TPM2_GetRandom(16) = 1a 2b 3c ...
=== end capabilities ===
```

This is a genuine TPM 2.0 GetCapability -- the same query as
`wolftpm/examples/wrap/caps` -- executing inside the fTPM, observed live
from Linux, with no discrete TPM and no kernel driver. See
"Running the stock wolfTPM examples" below for the cross-compiled-client
path (pending a dependable interactive round-trip).

##### Transport options (`FWTPM_XPORT`)

| `FWTPM_XPORT=` | Mailbox location | Status (stock Yocto HSS) |
|----------------|------------------|--------------------------|
| `DDR_WCB` (default) | 0xC0000000 non-cached write-combine DDR | coherent for hart 4 + Linux; used by the caps demo. Interactive round-trip being hardened. |
| `LIM_CACHED` | cacheable L2 LIM | coherent only under an HSS that sets up hart-4 PMA for LIM; not on the stock HSS |
| `DDR_NONCACHED` | 0x1400000000 non-cached 64-bit DDR alias (+ PMP) | needs the non-cached PMA configured for hart 4 |
| `L1D_OFF` | LIM, hart 4 L1d off via U54 CSR `0x7C1` | non-functional: `csrw 0x7C1` halts hart 4 before boot (`progress=0`) |
| `IHC` | Microchip Inter-Hart Comm stub | bitstream-gated stub |

```bash
make clean && make FWTPM_XPORT=DDR_WCB
# ... flash, boot ...
python3 ./linux-client/fwtpm_caps.py
```

The boot/trap breadcrumbs always stay in L2 LIM at `0x08000000` (read with
`fwtpm_caps.py --dump`), even when the live mailbox relocates, so a candidate
that faults before `main()` still reports its trap.

### Files

```
firmware/fwtpm-u54/
├── Makefile                    SoftConsole riscv64 cross-build
├── user_settings.h             wolfSSL + wolfTPM config (SP math 64-bit, no DH)
├── mpfs250-u54.ld              Linker: 4 MB DDR @ 0x91C00000, .sbss + .bss zeroed
├── startup.S                   _start, BSS clear, mtvec / trap handler
├── main.c                      HAL init -> FWTPM_Init -> FWTPM_TIS_Init -> ServerLoop
├── mpfs_hal.{c,h}              UART4, MTIME, console ring, RNG seed, POSIX stubs
├── fwtpm_tis_mpfs.{c,h}        Shared-memory TIS HAL: init/wait/signal/cleanup
├── fwtpm_nv_ram.c              RAM-backed FWTPM_NV_HAL (read/write/erase/maxSize)
├── fwtpm_clock_mpfs.c          MTIME-based FWTPM_CLOCK_HAL get_ms()
├── fwtpm_trng_scb.c            System Controller TRNG seed (FWTPM_RNG=SCB_NONCE)
├── amp-fwtpm.yaml              HSS payload generator config
├── mpfs-fwtpm-disable-cpu4.dts Linux DTS overlay (disables cpu4)
└── .gitignore
```

### Prerequisites

- SoftConsole v2022.2 toolchain at
  `/opt/Microchip/SoftConsole-v2022.2-RISC-V-747/`
- `wolfssl` and `wolftpm` source trees as siblings of `wolftpm-examples/`
  (or override via `WOLFSSL_DIR=` / `WOLFTPM_DIR=`)
- `hss-payload-generator` on `PATH`
- A Yocto-built `u-boot.bin` for harts 1–3 (Linux) — copy into this dir
  before running the payload generator
- Microchip HSS programmed via `make BOARD=mpfs-video-kit program` from
  the `hart-software-services/` repo

### Build

```bash
cd firmware/fwtpm-u54
make
```

Produces `fwtpm_u54.elf` (~210 KB text, linked at `0x91C00000`).

### Generate the HSS AMP payload

```bash
hss-payload-generator -c amp-fwtpm.yaml payload.bin
```

`amp-fwtpm.yaml` configures:
- u54_1/2/3: `u-boot.bin` at `0x80200000` (S-mode, OpenSBI)
- u54_4: `fwtpm_u54.elf` at `0x91C00000` (M-mode, `skip-opensbi: true`)

### Flash to SD card

The HSS reads the payload from the **raw BIOS-boot partition** (the
second partition on the SD, e.g. `/dev/sdX2`) — not a filesystem.
After unmounting any auto-mounted partitions:

```bash
sudo dd if=payload.bin of=/dev/sdX2 bs=1M conv=fsync status=progress
sync
# verify the bytes actually landed (some host kernels silently no-op
# the dd if the device was just unmounted)
sudo cmp -n "$(stat -c %s payload.bin)" payload.bin /dev/sdX2 && echo OK
```

### Serial console map

| UART | Hart | Purpose |
|------|------|---------|
| UART0 | E51 | HSS console |
| UART1 | U54_1 | Linux / U-Boot |
| UART4 | U54_4 | fwTPM stdout (only if physically routed; otherwise read the console ring from Linux) |

The Video Kit may not route UART4 to a USB-CDC port — fwTPM also writes
all `printf` output to the shared-memory console ring, readable from
Linux via `/dev/mem`.

### Reading server state from Linux

`linux-client/fwtpm_caps.py` (run as `root`) reads the mailbox +
boot-debug fields + console ring through `/dev/mem`. The L2 LIM region at
`0x08000000` is not part of Linux's System RAM map, so `/dev/mem` can map
it even under a stock `CONFIG_STRICT_DEVMEM=y` kernel -- no kernel rebuild
needed (verified on the Video Kit BSP). The only device-tree change
required is the cpu4-disable overlay so Linux releases hart 4.

For the default `DDR_WCB` build the live mailbox and console ring sit in
the non-cached DDR window at `0xC0000000`, while the boot/trap breadcrumbs
stay pinned in LIM at `0x08000000`. `fwtpm_caps.py` defaults to that base; for a different build pass
`--xport <backend>` (or `--base <addr>`) to point it at the live region,
and it still reads those breadcrumbs from LIM automatically.

### Boot debug fields

`startup.S` writes `progress=0x10` at the very first instruction so even
a hung boot can be diagnosed. `main.c` advances the marker through
`0x100/0x2/0x3/0x4/0x5/0x6/0x7`. An M-mode trap handler captures
`mcause`/`mepc`/`mtval` to the mailbox and sets `progress=0xBADC0DE0`.
Trap fields are zeroed at every `_start` so stale values from a previous
boot can't confuse later runs.

### Expected boot output (read via console ring)

```
=== wolfTPM fwTPM on PolarFire SoC U54 (hart 4, M-mode) ===
wolfTPM fwTPM version 0.1.0
APB clock = 150000000 Hz
fwTPM NV: RAM-backed (65536 bytes, volatile)
wolfCrypt_Init = 0
wc_InitRng = 0
wc_RNG_GenerateBlock(32) = 0
Initializing fwTPM...
fwTPM initialized successfully
fwTPM TIS: Shared memory at 0x8000000 (16384 bytes)
fwTPM TIS: Console ring at offset 0x40 (4064 bytes)
fwTPM TIS: transport id 1, poll-mode (no MSIP doorbell)
Entering TIS server loop (waiting for commands)
fwTPM TIS: Server ready, waiting for register accesses...
```

After this point hart 4 sits polling `mbox->cmd_ready`. A Linux client
would drive a TIS register access by writing `regs->reg_addr`,
`regs->reg_len`, `regs->reg_is_write`, optionally `regs->reg_data[]`,
then setting `mbox->cmd_ready` to a nonzero nonce (the server echoes it
back in `mbox->echo_nonce`).

### Linux client: `fwtpm_caps.py`

`firmware/fwtpm-u54/linux-client/fwtpm_caps.py` is the single read-only
client, run on the target as `root`. It reads the mailbox status and the
console ring over `/dev/mem`, prints the hart-4 caps self-test output
(GetCapability manufacturer/vendor + GetRandom), and reports PASS/FAIL:

```
python3 fwtpm_caps.py            # caps demo + transport/identity checks
python3 fwtpm_caps.py --dump     # also print the full ring + breadcrumbs
```

It defaults to the `DDR_WCB` mailbox base; pass `--xport <backend>` or
`--base <addr>` to match a different `FWTPM_XPORT` build. This is the
working analogue of the cross-compiled `wolftpm/examples/wrap/caps`: the
caps run on hart 4 (the reliable server -> Linux path) rather than being
driven from Linux, since the interactive round-trip is still being
hardened.

### Running the stock wolfTPM examples (cross-compile)

The reliable capabilities demo today runs *inside the fTPM* on hart 4 (see
"What works today" above) and is read with `fwtpm_caps.py`. Running
the **stock** `wolftpm/examples/wrap/caps` binary *from Linux* against this
fTPM additionally needs the interactive Linux -> hart 4 round-trip plus a
wolfTPM IO callback that bridges TPM2 calls onto the shared-memory mailbox
(roadmap items 1-2). The cross-compile itself, for when that lands:

```bash
# RISC-V 64-bit Linux cross toolchain (Yocto SDK, or a distro
# riscv64-linux-gnu- gcc). Build wolfSSL, then wolfTPM:
export CROSS=riscv64-linux-gnu

cd wolfssl
./configure --host=$CROSS CC=$CROSS-gcc --enable-cryptocb \
    --enable-rsa --enable-ecc --disable-examples --prefix=$PWD/../rv64-root
make && make install

cd ../wolftpm
./configure --host=$CROSS CC=$CROSS-gcc \
    --with-wolfcrypt=$PWD/../rv64-root --prefix=$PWD/../rv64-root
make
file examples/wrap/caps     # ELF 64-bit LSB executable, UCB RISC-V
```

Stage `examples/wrap/caps` onto the target (over the SD rootfs or `scp`).
Until the mailbox IO bridge exists it has no fTPM endpoint to talk to;
with a `/dev/tpmrm0` shim or a custom `TPM2_IoCb` over the mailbox it runs
unchanged. The on-hart-4 self-test in `main.c` (`fwtpm_caps_selftest`) is
the working stand-in until then.

## Roadmap

1. **Reliable interactive transport** — the server -> Linux path works
   (used by the on-hart-4 caps demo); the Linux -> hart 4 round-trip needs
   the mailbox in genuinely non-cached memory. `DDR_WCB` (0xC0000000) is
   coherent for both hart 4 and Linux, but the write-combine ordering for
   the interactive path is still being hardened. The clean fix is an HSS
   that configures hart-4 coherency (an earlier bring-up's HSS did, via
   `boot_service(u54_4) :: SetupPMP`). Related directions:
   - `IHC` (Microchip Inter-Hart Communication) for an interrupt-driven
     mailbox -- gated on the Libero bitstream containing the IHC IP.
   - A kernel doorbell ringing `CLINT_MSIP[4]` so hart 4 can WFI between
     requests instead of polling.
   - **wolfBoot-hosted path:** wolfBoot's PolarFire branch replaces HSS
     and owns the SBI on E51, with cross-hart mailboxes proven coherent
     in uncached DTIM (`0x01000000`) / non-cached DDR (SEG1) and an SBI
     vendor-EID doorbell available; tracked as a parallel investigation.
   (`FWTPM_XPORT=L1D_OFF` is a dead end: the U54 `csrw 0x7C1` halts
   hart 4 before boot.)
2. **Linux userspace TIS HAL** — once (1) lands, wrap the mailbox
   behind wolfTPM's HAL callbacks so the `wolftpm/examples/` tree
   runs unchanged on the target.
3. **FRAM-backed NV** — replace `fwtpm_nv_ram.c` with an I²C/SPI
   FRAM driver so seeds and counters survive a power cycle.
4. **Hardware TRNG entropy** — DONE (default). The default
   **`FWTPM_RNG=SCB_NONCE`** backend (`fwtpm_trng_scb.c`) seeds the DRBG
   from the PolarFire SoC System Controller hardware nonce service and is
   validated on the Video Kit. The development-only `FWTPM_RNG=JITTER`
   MTIME-jitter backend (`mpfs_rng_seed_cb`) remains for bring-up but is
   **not** auto-opted-in: it must be acknowledged explicitly
   (`FWTPM_RNG=JITTER FWTPM_DEV_INSECURE_RNG=1`) or the build `#error`s,
   so the weak seed cannot be selected silently and a plain `make` is
   secure. Note: the System Controller mailbox is a shared resource, so
   confirm no other master (HSS at runtime, a Linux `mss-sys-services`
   driver) drives it while hart 4 issues requests. Under the
   wolfBoot-hosted track this is the same path wolfBoot exposes as
   `mpfs_nonce()`.
5. **PQC** — enable ML-KEM / ML-DSA in `user_settings.h` and
   benchmark on hart 4 (600 MHz RV64GC, no FPU). Note ML-DSA-65/87
   raise `FWTPM_MAX_COMMAND_SIZE` to 8192, which would also grow the
   `FWTPM_TIS_SHM_SIZE` shared region; ML-KEM + ML-DSA-44 stay at 4096.
