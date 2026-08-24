# fwTPM on AMD Spartan UltraScale+ SCU35 (MicroBlaze V soft core)

Firmware TPM 2.0 (from [wolfTPM](https://github.com/wolfSSL/wolfTPM) `fwtpm`) on a **MicroBlaze V** (RISC-V rv32imc) soft core instantiated in the fabric of an AMD **Spartan UltraScale+ SCU35 Evaluation Kit** (`xcsu35p`) - a pure FPGA with no hardened CPU. It is the AMD analog of the PolarFire Mi-V example: the fwTPM server is driven from a host over UART with the same raw swtpm + Microsoft-simulator ("mssim") framing, so the stock wolfTPM swtpm client drives it unmodified.

## Status

The bare-metal platform HAL and a hello-world are **hardware-validated on the stock reference-design bitstream**. The **full** (RSA+ECC) fwTPM builds but is too large for this device. The **minimal ECC-only** fwTPM (`FWTPM_TINY_ECC`) is **hardware-validated on the SCU35**: ~190 KB, fits the stock 192 KB, and runs on a bitstream with the SYSMONE4 fabric TRNG added (`fpga/add_sysmon.tcl`) - TPM2_Startup and TPM2_GetRandom pass and GetRandom differs across cold boots, confirming real System-Monitor entropy. No memory enlargement is needed.

| Item | Status |
|------|--------|
| MicroBlaze-V HAL (AXI UARTLite, AXI Timer, startup, retarget) | Hardware-validated (SCU35) |
| hello-world (banner + timer heartbeat) | Hardware-validated (SCU35): banner + 1 s heartbeat at 225 MHz |
| fwTPM over UART, full (RSA+ECC) | Builds (~652 KB); too large for the xcsu35p |
| fwTPM over UART, minimal ECC-only (`FWTPM_TINY_ECC`) | **Hardware-validated (SCU35)**: ~190 KB fits the 192 KB xcsu35p; boots on the SYSMON-TRNG bitstream, TPM2_Startup + TPM2_GetRandom pass, GetRandom differs across cold boots (real entropy) |
| SYSMONE4 fabric TRNG (System Management Wizard) | Hardware-validated: live temp/VCCINT/VCCAUX ADC codes with jittering LSBs seed the Hash-DRBG |
| Persistent NV in AXI QuadSPI | Planned |
| SRAM PUF | Planned (needs an uninitialized fabric-SRAM primitive in the design) |

**Memory is tight but workable.** The `xcsu35p` has only **48 block-RAM primitives = 192 KB total**, no UltraRAM, and the SCU35 board has no external DDR. The **full** fwTPM (RSA+ECC, ~652 KB) does not fit. But the **minimal ECC-only** build (`FWTPM_TINY_ECC`) fits in ~190 KB, using: ECC-P256 only, table-free AES, no SHA-1, reduced TPM context/NV, an on-die **SYSMONE4 fabric TRNG** in place of wolfCrypt MemUse entropy (saves ~30 KB), and wolfTPM's finer per-command-group gating - the individual `FWTPM_NO_*` command-group macros, selected explicitly in `user_settings.h` (wolfTPM has no "minimal" umbrella; each gate is a deliberate choice) - compiling out the key-migration / ECDH / hash-command / context / symmetric-encrypt / clock command groups. The stock 192 KB BRAM is now sufficient; running it requires only that the bitstream instantiate the SYSMON (AXI System Management) IP for the TRNG - no memory enlargement is needed. See `fpga/README.md`.

## Platform (from the SCU35 Zephyr TRD)

- Core: MicroBlaze V, RISC-V **rv32imc** (M + C, no atomics/FPU), reset vector `0x00000000`, AXI clock **225 MHz**.
- Console: **AXI UARTLite** `axi_uartlite_1` @ `0x40700000`, the design's `serial1`, wired to the onboard USB-UART; fixed 115200 8N1. (`axi_uartlite_0` @ `0x40600000` exists in the design but goes to a header, not the USB-UART.)
- Time base: **AXI Timer** `axi_timer_0` @ `0x41C00000` (free-running 32-bit up-counter + software accumulator).
- NV flash (future): **AXI QuadSPI** `axi_quad_spi_0` @ `0x44A00000`.
- Debug: `mdm_riscv` (JTAG over the onboard FT4232H).

## Layout

```
firmware/
  common/            shared bare-metal MicroBlaze-V HAL (wolfSSL-authored)
    scu35_board.h    address book (UARTLite, Timer, QSPI, clock, BRAM)
    mbv_uart.c/.h    AXI UARTLite console driver
    mbv_time.c/.h    AXI Timer time base
    startup.S        RV32 reset/startup (reset vector 0x0)
    retarget.c       newlib stubs (printf -> UART, _sbrk heap)
  hello/             sanity image: banner + timer heartbeat (fits 192 KB)
  fwtpm-mbv/         the fwTPM server (full build ~652 KB; FWTPM_TINY_ECC fits 192 KB)
    main.c           HAL registration + UART swtpm/mssim command loop
    fwtpm_clock_mbv.c   clock HAL (AXI Timer) + entropy hi-res timer
    fwtpm_nv_ram.c   volatile NV backend
    fwtpm_trng_sysmon.c SYSMONE4 fabric TRNG seed source (-DFWTPM_TINY_HWTRNG)
    user_settings.h  wolfSSL + wolfTPM config (SP-32; MemUse or SYSMON entropy)
    mbv-bram.ld      linker (BRAM @ 0x0)
  bench/             standalone wolfCrypt benchmark (no TPM), fits 192 KB
    main.c           UART/timer bring-up + current_time() + benchmark_test()
    user_settings.h  ECC-P256/SHA-256 config (mirrors the deployed fTPM)
    Makefile         builds wolfcrypt/benchmark bare-metal
fpga/
  README.md          how to get/rebuild the bitstream + sizing analysis
  add_sysmon.tcl     overlay: add the SYSMONE4 AXI TRNG to the TRD block design
  build_sysmon.tcl   build driver: TRD + overlay -> synth/impl -> PDI
```

## Prerequisites

- The Vitis 2025.x RISC-V bare-metal toolchain: `export PATH=/opt/Xilinx/<ver>/gnu/riscv/lin/bin:$PATH` (the multilib `riscv64-unknown-elf-gcc` targets `riscv32-xilinx-elf`).
- wolfSSL and wolfTPM source trees as siblings of `wolftpm-examples` (default `../../../../../wolfssl`, `../../../../../wolftpm`).
- A programmed SCU35 bitstream and `hw_server`/`xsdb` for the JTAG load (see `fpga/README.md`).

## Build

```bash
export PATH=/opt/Xilinx/2025.2/gnu/riscv/lin/bin:$PATH

cd firmware/hello    && make      # sanity image (scu35-hello.elf, fits 192 KB)
cd firmware/fwtpm-mbv && make     # full fwTPM server (scu35-fwtpm.elf, ~652 KB)

# minimal ECC-only fwTPM that fits the 192 KB xcsu35p (~190 KB); needs the
# SYSMON TRNG bitstream from fpga/add_sysmon.tcl:
cd firmware/fwtpm-mbv && make \
    EXTRA_CFLAGS="-DFWTPM_TINY_ECC -DFWTPM_TINY_PCR8 -DFWTPM_TINY_HWTRNG" \
    EXTRA_LDFLAGS="-Wl,--defsym=__heap_size=0x3000 -Wl,--defsym=__stack_size=0x2000"

cd firmware/bench    && make      # wolfCrypt benchmark (scu35-bench.elf, ~191 KB)
```

## Run (JTAG load over the programmed bitstream)

Program the bitstream with the Vivado Hardware Manager (`program_hw_devices` on the `xcsu35p`), then JTAG-load the ELF onto the MicroBlaze V via `xsdb` (`hw_server` running): `targets -set -filter {name == "Hart #0"}`, `dow scu35-*.elf`, `rwr pc <entry>` (the ELF's `_start`), `con`. The console is the design's `serial1` = **`axi_uartlite_1` (0x40700000)**, wired to the SCU35 USB-UART; read it with `uart-monitor`. This is exactly how the hello image was validated. Drive the fwTPM from the host with the shared `swtpm_uart_bridge.py` / `fwtpm_uart_test.py` clients (see the Mi-V example's `host-client/`).

## Performance (measured on hardware)

Direct wolfCrypt benchmark from `firmware/bench`, run on the SCU35 MicroBlaze V soft core (RISC-V rv32imc @ 225 MHz, 32-bit portable-C SP math, `-O2`). The build mirrors the deployed fTPM's algorithm set (ECC-P256, SHA-256, no RSA). Symmetric/hash throughput uses `BENCH_EMBEDDED` 1 KB buffers, so it reflects per-call cost on a soft core, not a bulk-streaming rate.

| Operation | Result |
|-----------|--------|
| ECC P-256 keygen | 8.98 ops/sec (111 ms) |
| ECDHE P-256 agree | 8.99 ops/sec (111 ms) |
| ECDSA P-256 sign | 8.06 ops/sec (124 ms) |
| ECDSA P-256 verify | 4.44 ops/sec (225 ms) |
| SHA-256 | 1.98 MiB/s |
| SHA-1 | 6.19 MiB/s |
| SHA3-256 | 515 KiB/s |
| HMAC-SHA256 | 1.96 MiB/s |
| AES-128-CBC | 50 KiB/s |
| AES-256-GCM | 34 KiB/s |
| RNG (SHA-256 DRBG) | 761 KiB/s |

The benchmark image uses a deterministic bench-only RNG seed (not the SYSMON TRNG), so it runs on the stock bitstream. This measures raw wolfCrypt throughput on the core; end-to-end TPM command latency additionally includes the 115200-baud UART transport.

## See also

- `Microchip/miv-mpf300-splash` - fwTPM on a soft Mi-V RV32 core; the closest analog (same rv32 firmware shape).
- `Xilinx/fwtpm-zc702-a9` - fwTPM on the Zynq-7000 Cortex-A9 (with the wolfCrypt SRAM PUF).
