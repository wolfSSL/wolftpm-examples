# fwTPM on AMD Zynq-7000 Cortex-A9 (ZC702) with SRAM PUF

Firmware TPM 2.0 (from [wolfTPM](https://github.com/wolfSSL/wolfTPM) `fwtpm`) running bare-metal on a single **Cortex-A9** (ARMv7-A) of an AMD/Xilinx **Zynq-7000** (ZC702). The fwTPM server is driven from a host PC over UART using the same raw swtpm + Microsoft-simulator ("mssim") framing as the STM32H5 and Mi-V ports, so the stock wolfTPM swtpm client drives it unmodified.

Its distinguishing feature is that the TPM's NV-journal integrity key is a **device-unique key derived from the Cortex-A9 on-chip-memory (OCM) SRAM power-on state**, using wolfCrypt's configurable SRAM PUF (a BCH(127,k,t) fuzzy extractor + HKDF). No root key is stored in flash: it is regenerated from silicon each boot.

## Architecture

```
+-----------------------------------------------------------+
|  Zynq-7000 (ZC702)                                        |
|                                                           |
|   Cortex-A9 core 0 (SVC, bare-metal)                      |
|   +---------------------------------------------------+   |
|   |  wolfTPM fwTPM engine (FWTPM_ProcessCommand)      |   |
|   |    NV journal  --> volatile RAM (default)         |   |
|   |                    QSPI flash (opt-in)            |   |
|   |    integrity key <-- SRAM PUF (OCM power-on)      |   |
|   |    clock       <-- MPCore Global Timer            |   |
|   |    entropy     <-- wolfCrypt MemUse (no HW TRNG)  |   |
|   +---------------------------------------------------+   |
|            ^  raw swtpm / mssim framing                   |
|            |  Cadence UART1 (0xE0001000)                  |
+------------|----------------------------------------------+
             v
     Host PC: swtpm_uart_bridge.py  <->  wolfTPM examples
```

The dual-A9 second core and PetaLinux are not used; this is a self-contained bare-metal server (the coprocessor + Linux-client model is the ZCU102 R5 example instead).

## Layout

```
firmware/
  common/            shared bare-metal A9 HAL (wolfSSL-authored, no vendor BSP)
    zynq7000.h       address book (UART, Global Timer, SLCR, QSPI, OCM, DDR)
    zynq_uart.c/.h   polled Cadence UART console driver
    zynq_time.c/.h   MPCore Global Timer time base + A9 PMU cycle counter
    startup.S        A9 SVC reset: vectors, cache/VFP bring-up, BSS, main
    mmu.c            flat MMU map (DDR Normal cacheable) - required for printf
    retarget.c       newlib stubs (printf -> UART, _sbrk heap)
  hello/             sanity image: banner + Global-Timer heartbeat
  fwtpm-a9/          the fwTPM server
    main.c           HAL registration + UART swtpm/mssim command loop
    fwtpm_clock_zynq.c  clock HAL (Global Timer) + entropy hi-res timer (PMCCNTR)
    fwtpm_nv_ram.c   volatile NV backend (default)
    fwtpm_nv_qspi.c  persistent NV + PUF helper store in QSPI (-DFWTPM_NV_QSPI)
    fwtpm_puf.c/.h   OCM SRAM PUF -> device-unique NV integrity key
    fwtpm_puf_selftest.c  synthetic PUF regression (build -DFWTPM_PUF_SELFTEST)
    user_settings.h  wolfSSL + wolfTPM configuration
    zynq7000-fwtpm.ld    linker (DDR @ 0x04000000)
    host-client/     PC-side drivers (swtpm bridge, caps/PCR/random, NV persist)
  bench/             standalone wolfCrypt benchmark (no TPM), runs from DDR
    main.c           UART/timer bring-up + current_time() + benchmark_test()
    user_settings.h  full RSA-2048 + ECC config
    Makefile         builds wolfcrypt/benchmark bare-metal
```

## Prerequisites

- `arm-none-eabi-gcc` toolchain (13.x verified).
- wolfSSL source tree (default `../../../../../wolfssl`) with SRAM PUF support (`wolfcrypt/src/puf.c`).
- wolfTPM source tree (default `../../../../../wolftpm`) with the `fwtpm` engine.
- A prebuilt Zynq-7000 FSBL (does `ps7_init`: DDR, clocks, MIO/UART) - e.g. `soc-prebuilt-firmware/zc702-zynq/zynq_fsbl.elf`.
- Xilinx `xsdb` / `hw_server` (Vitis) for the JTAG load, plus a serial terminal on the ZC702 USB-UART (a CP210x, UART1, 115200 8N1).

## Build

```bash
cd firmware/hello    && make      # sanity image (zc702-hello.elf)
cd firmware/fwtpm-a9 && make      # fwTPM server  (zc702-fwtpm.elf)
cd firmware/bench    && make      # wolfCrypt benchmark (zc702-bench.elf)

# Override the wolfSSL / wolfTPM source paths:
make WOLFTPM_DIR=/path/to/wolftpm WOLFSSL_DIR=/path/to/wolfssl
```

Optional `fwtpm-a9` build flags (`EXTRA_CFLAGS` / knobs):

- `-DFWTPM_ENABLE_PQC` - ECC + post-quantum (ML-DSA / ML-KEM) TPM instead of the default RSA + ECC. Pair with a host wolfTPM built `--enable-v185 --enable-mldsa --enable-mlkem`.
- `-DFWTPM_PUF_SELFTEST` - run the synthetic SRAM PUF regression at boot (also enables `WOLFSSL_PUF_TEST`). Set the BCH profile with `PUF_T` (7/10/13/15) and `PUF_CW` (codeword count), e.g. `make PUF_T=13 PUF_CW=32 EXTRA_CFLAGS="-DFWTPM_PUF_SELFTEST"`.

## Load and run (JTAG over FSBL)

The A9 has no PLM/PMU boot help: a prebuilt FSBL does `ps7_init` and parks, then the app is loaded over the top. Set SW10 to on-board JTAG and SW16 to JTAG boot mode, power-cycle, then with `xsdb`:

```tcl
connect
targets -set -filter {name =~ "ARM Cortex-A9 MPCore #0"}
rst -system
after 1500
targets -set -filter {name =~ "ARM Cortex-A9 MPCore #0"}
dow zynq_fsbl.elf        ;# ps7_init (DDR/UART/clocks), then parks
con
after 3000
stop
dow firmware/fwtpm-a9/zc702-fwtpm.elf
con
```

The board prints the banner, the SRAM PUF identity/profile, then the self-test (`TPM2_Startup` / `TPM2_GetRandom` `rc=0`), then serves TPM2 over UART1.

Drive it from the host (adjust the serial device):

```bash
cd firmware/fwtpm-a9/host-client
python3 fwtpm_uart_test.py /dev/ttyUSB0             # caps / PCR / GetRandom

# Or bridge to the stock wolfTPM swtpm client:
python3 swtpm_uart_bridge.py /dev/ttyUSB0 2321 &
# then from a wolfTPM build:
./examples/wrap/caps                                # over TPM_INTERFACE=swtpm
```

## SRAM PUF: test and use

- **Test (synthetic).** The `-DFWTPM_PUF_SELFTEST` build injects deterministic synthetic SRAM (`WOLFSSL_PUF_TEST`) and runs enroll -> clean reconstruct -> reconstruct at the BCH correction limit (t flips) -> over-limit (t+1 flips must fail or differ) -> bad-argument -> zeroize, printing per-step results and `Result: 0 (PASS)`. This proves the fuzzy-extractor math on the A9 silicon independent of the physical OCM. Sweep `PUF_T` / `PUF_CW` to characterize a profile.

- **Use (physical).** The default build reads an uninitialized OCM carve-out (`FWTPM_PUF_OCM_ADDR`, near the top of the high-mapped 256 KB OCM) as the PUF source. On first boot it enrolls (generating helper data + a device identity); later boots reconstruct the same stable bits from the persisted helper data, correcting the SRAM noise. The reconstructed bits HKDF-derive a 32-byte key that backs the fwTPM NV journal's integrity HMAC (`FWTPM_NV_HAL.get_integrity_key`). The BCH profile's `WC_PUF_PROFILE_ID` is persisted with the helper data and checked on reconstruct, so a build mismatch is rejected rather than silently producing a wrong key.

A **stable key across power cycles** requires persisting the helper data in non-volatile storage. With the default volatile RAM NV the helper data does not survive a reload, so each boot enrolls afresh; build with `-DFWTPM_NV_QSPI` to persist both the helper data and the NV journal in QSPI flash (see below).

## Persistent NV in QSPI (`-DFWTPM_NV_QSPI`)

`fwtpm_nv_qspi.c` stores the fwTPM NV journal and the SRAM-PUF helper data in the top two 64 KB sectors of the board's QSPI NOR flash (the same 16 MB Micron MT25Q that wolfBoot boots from): NV at `0x00FE0000`, PUF helper at `0x00FF0000`. The wolfBoot partitions end well below this, and a hard runtime guard refuses any erase/program below `0x00F00000`, so the boot image cannot be touched. NV is a RAM shadow loaded from flash at init; a write updates the shadow and rewrites the touched sector. The QSPI controller access (I/O mode for commands, Linear/XIP mode at `0xFC000000` for reads) is shared with the wolfBoot Zynq-7000 HAL.

With this backend the PUF-derived key is stable across boots: the first boot enrolls and stores the helper data; later boots reconstruct the same device identity from it. On the ZC702 this is hardware-verified end to end: boot 1 reports `(enrolled)`, boot 2 (after a reload) reports `(reconstructed)` with the same identity, and a TPM NV index written on one boot (`fwtpm_nv_persist_test.py`) reads back after a reload - the NV journal validating under the reconstructed PUF integrity key.

## Platform notes

- **No hardware TRNG.** The Zynq-7000 PS has no TRNG, so the Hash-DRBG is seeded by wolfCrypt's MemUse entropy (memory-timing jitter conditioned through SHA3-256, gated fail-closed by SP800-90B health tests). The high-resolution sampler uses the A9 PMU cycle counter (the A9 has no ARMv7 generic timer). The I-cache is enabled in `startup.S` to keep SHA3-heavy seeding fast.
- **Clock.** The MPCore 64-bit Global Timer (0xF8F00200, 333.333 MHz on the ZC702) is the monotonic ms time base.
- **Caches / MMU.** `startup.S` enables the MMU via `mmu.c` (flat identity map: DDR Normal write-back cacheable, MMIO Device, OCM Normal non-cacheable) with the I-cache, D-cache and branch prediction on. The MMU is required: with it off the A9 treats all data as Strongly-Ordered, so the unaligned accesses newlib's `printf` emits abort. There is no DMA in this build (polled UART; the optional QSPI NV backend is CPU PIO / XIP, not DMA), so full caching is safe.

## Status

| Item | Status |
|------|--------|
| A9 HAL + hello (UART, Global Timer, MMU/cache/VFP) | Hardware-validated (ZC702) |
| fwTPM over UART (self-test + swtpm/mssim server) | Hardware-validated (ZC702): manufacturer "WOLF", PCR read, GetRandom |
| SRAM PUF synthetic regression | Hardware-validated (ZC702): Result 0 (PASS) |
| SRAM PUF -> NV integrity key (physical OCM) | Hardware-validated (ZC702): enrolls a device identity, backs NV integrity |
| Persistent NV + PUF helper data in QSPI flash | Hardware-validated (ZC702): PUF reconstructs across reload, NV value persists |

Note: the A9 runs with the MMU enabled (flat map, DDR Normal write-back cacheable). This is required, not optional - with the MMU off the A9 treats all data as Strongly-Ordered, and the unaligned accesses newlib's `printf` emits fault. See `firmware/common/mmu.c`.

## Performance (measured on hardware)

Direct wolfCrypt benchmark from `firmware/bench`, run on one Cortex-A9 of the ZC702 (ARMv7-A @ 667 MHz, 32-bit portable-C SP math, `-O2`, `BENCH_EMBEDDED` 1 KB buffers). This is the full RSA-2048 + ECC set the fwTPM uses.

| Operation | Result |
|-----------|--------|
| RSA-2048 keygen | 0.14 ops/sec (7.19 s) |
| RSA-2048 sign (private) | 6.31 ops/sec (158 ms) |
| RSA-2048 verify (public) | 363.6 ops/sec (2.75 ms) |
| ECC P-256 keygen | 92.5 ops/sec (10.8 ms) |
| ECDHE P-256 agree | 92.7 ops/sec (10.8 ms) |
| ECDSA P-256 sign | 82.9 ops/sec (12.1 ms) |
| ECDSA P-256 verify | 46.0 ops/sec (21.8 ms) |
| SHA-256 | 19.3 MiB/s |
| SHA-1 | 42.7 MiB/s |
| SHA3-256 | 7.3 MiB/s |
| HMAC-SHA256 | 19.6 MiB/s |
| AES-128-CBC | 13.1 MiB/s |
| AES-256-GCM | 3.4 MiB/s |
| RNG (SHA-256 DRBG) | 7.5 MiB/s |

The benchmark image uses a deterministic bench-only RNG seed. As expected for a hardened core, this is roughly an order of magnitude faster than the SCU35 MicroBlaze V soft core (e.g. ECDSA P-256 sign 82.9 vs 8.1 ops/sec, SHA-256 19.3 vs 2.0 MiB/s). This measures raw wolfCrypt throughput; end-to-end TPM command latency additionally includes the 115200-baud UART transport.

## See also

- `Xilinx/fwtpm-zcu102-r5` - fwTPM on the ZynqMP Cortex-R5 (OpenAMP RPMsg, Linux client).
- `Microchip/miv-mpf300-splash` - fwTPM on a soft Mi-V RV32 core (UART), the standalone-UART template for this port.
- `STM32/fwtpm-stm32h5` - fwTPM on Cortex-M33 (UART).
