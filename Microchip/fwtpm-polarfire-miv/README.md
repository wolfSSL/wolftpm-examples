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
- Transport: mailbox + console ring + TIS register block in L2 LIM
  (`0x08000000`, 16 KB used). Hart 4 polls `mbox.cmd_ready`; the full
  Linux <-> hart 4 round-trip works under HSS AMP (verified on the Video
  Kit). The backend is build-selectable (`FWTPM_XPORT`) -- see
  "Transport options" below.
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

#### Linux <-> hart 4 round-trip (verified on HSS AMP)

Both directions work under HSS AMP. `fwtpm_smoke.py` Phase 1 (hart 4 ->
Linux read) and Phase 2 (Linux -> hart 4 command, with a nonce the server
echoes back) both pass with the default `LIM_CACHED` transport, verified
on the MPFS250T Video Kit and reproducible across runs.

An earlier standalone bring-up (bare-metal `skip-opensbi`, no HSS) saw
Phase 2 time out: hart 4's write-through L1d held a stale `cmd_ready`, and
the U54 has no L1d invalidate (pre-Zicbom) so a fence could not fix it.
That setup never configured hart 4's PMP/PMA. Under HSS AMP, HSS sets up
hart 4 (`boot_service(u54_4) :: SetupPMP` in the boot log) and the
cacheable L2 LIM mailbox is coherent for hart 4 -- so the alternate
transports below are not needed in this configuration.

##### Transport options (`FWTPM_XPORT`)

The transport backend is build-selectable. Bench results on the Video Kit:

| `FWTPM_XPORT=` | What it does | Result |
|----------------|--------------|--------|
| `LIM_CACHED` (default) | mailbox in cacheable L2 LIM | **works** (Phase 1+2 pass) |
| `DDR_NONCACHED` | mailbox in the 0x1400000000 non-cached DDR alias; `startup.S` adds a PMP entry | **works** (Phase 1+2 pass; Linux reaches the alias via `/dev/mem`) |
| `L1D_OFF` | LIM mailbox, hart 4 L1d disabled via U54 CSR `0x7C1` | **non-functional** on this U54 -- the `csrw 0x7C1` halts hart 4 before boot (`progress=0`). Unnecessary since `LIM_CACHED` is coherent. |
| `IHC` | Microchip Inter-Hart Comm stub (bitstream-gated) | stub; Phase 2 skipped |

```bash
make clean && make FWTPM_XPORT=DDR_NONCACHED
# ... flash, boot ...
sudo ./linux-client/fwtpm_smoke.py --xport ddr_noncached
```

Phase 2 writes a rolling **nonce** into `cmd_ready` that the server echoes
into `mbox.echo_nonce`; a matching echo proves hart 4 observed the new
write (not a stale line). The boot/trap breadcrumbs always stay in L2 LIM
at `0x08000000` (read them with `fwtpm_mbox_dump.py`), even when
`DDR_NONCACHED` relocates the live mailbox, so a candidate that faults
before `main()` still reports its trap.

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

`linux-client/fwtpm_mbox_dump.py` (run as `root`) decodes the mailbox +
boot-debug fields + console ring through `/dev/mem`. The L2 LIM region at
`0x08000000` is not part of Linux's System RAM map, so `/dev/mem` can map
it even under a stock `CONFIG_STRICT_DEVMEM=y` kernel -- no kernel rebuild
needed (verified on the Video Kit BSP). The only device-tree change
required is the cpu4-disable overlay so Linux releases hart 4.

For the default `LIM_CACHED` build the live mailbox and console ring sit
in LIM at `0x08000000`. Under `FWTPM_XPORT=DDR_NONCACHED` they relocate
to the non-cached DDR alias `0x1400000000`, while the boot/trap
breadcrumbs stay pinned in LIM; pass `--xport ddr_noncached` (or
`--base 0x1400000000`) to point the script at the live region, and it
still reads those breadcrumbs from LIM automatically. `fwtpm_smoke.py`
takes the same `--xport`/`--base` options.

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

### Linux client smoke test

`firmware/fwtpm-u54/linux-client/` contains two helper scripts to run
on the target as `root`:

- `fwtpm_mbox_dump.py` — decodes the mailbox + boot debug fields +
  console ring buffer from `/dev/mem`. Diagnostic, always safe.
- `fwtpm_smoke.py` — Phase 1 reads `FWTPM_TIS_REGS.magic` + `did_vid`
  (server -> Linux direction); Phase 2 drives a TIS read of `TPM_DID_VID`
  via the mailbox (Linux -> server direction, with a nonce the server
  echoes back). Both phases **pass** under HSS AMP on the default
  `LIM_CACHED` build. Pass `--xport <backend>` to match the firmware's
  `FWTPM_XPORT` build (e.g. `ddr_noncached` relocates the live mailbox).

## Roadmap

1. **Linux <-> hart 4 transport** — DONE under HSS AMP. The full
   round-trip works on the default `LIM_CACHED` transport (verified on
   the Video Kit); `DDR_NONCACHED` also works as a fully-uncached
   alternative. The earlier "Phase 2 times out" limitation was specific
   to the standalone `skip-opensbi` bring-up that did not set up hart 4's
   PMP/PMA. Optional future enhancements:
   - `IHC` (Microchip Inter-Hart Communication) for an interrupt-driven
     mailbox -- gated on the Libero bitstream containing the IHC IP.
   - A kernel doorbell ringing `CLINT_MSIP[4]` so hart 4 can WFI between
     requests instead of polling.
   - **wolfBoot-hosted path:** wolfBoot's PolarFire branch replaces HSS
     and owns the SBI on E51, with cross-hart mailboxes proven coherent
     in uncached DTIM (`0x01000000`) / non-cached DDR (SEG1) and an SBI
     vendor-EID doorbell available; tracked as a parallel investigation.
   (`FWTPM_XPORT=L1D_OFF` is a dead end: the U54 `csrw 0x7C1` halts
   hart 4 before boot, and it is unnecessary since LIM is coherent here.)
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
