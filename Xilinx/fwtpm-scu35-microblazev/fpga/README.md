# SCU35 FPGA design for the MicroBlaze V fwTPM

The firmware in this example runs on a MicroBlaze V system in the SCU35 fabric. The hardware platform is AMD's **"SCU35 Zephyr RTOS IO" Target Reference Design (TRD)**, which already instantiates everything the fwTPM needs: a MicroBlaze V (rv32imc) core, AXI UARTLite (console), AXI Timer, AXI QuadSPI, GPIO, and the `mdm_riscv` JTAG debug module. This example does not redistribute the bitstream; obtain the TRD from AMD.

## Memory: the full fwTPM does not fit, but the minimal ECC build does

The stock TRD gives the MicroBlaze V **192 KB** of local BRAM (`0x00000000-0x0002FFFF`), sized for its ~154 KB Zephyr image. The `xcsu35p` has only **48 block-RAM (RAMB36) primitives = 192 KB total**, no UltraRAM, and the board has no external DDR - enlarging the local memory does not help (verified with Vivado 2025.1: a single 512 KB LMB bank alone exceeds the device's BRAM).

- **Full fwTPM (RSA+ECC, ~652 KB):** does not fit; needs a larger AMD device (a bigger Spartan UltraScale+ part or one with external DDR, ~768 KB of code/data memory). The firmware here builds and is ready for such a target.
- **Minimal ECC-only fwTPM (`FWTPM_TINY_ECC`, ~190 KB):** **fits the stock 192 KB BRAM** - no memory enlargement required. See the sizing breakdown below. The only bitstream change it needs is the SYSMON (AXI System Management) IP for the hardware TRNG.

The stock bitstream as shipped also runs the `hello` image (already hardware-validated).

### Can an ECC-only fTPM fit 192 KB? Yes - measured.

It took the full stack of reductions - ECC-only, table-free AES, no SHA-1,
reduced context/NV, a **fabric hardware TRNG (SYSMONE4)** replacing MemUse, and
**wolfTPM's finer per-command-group gating** - but the ECC-only fTPM now fits.
Measured on this toolchain (rv32imc, `-Os`, reduced-heap link):

| Build | Total | Code (text) |
|-------|-------|-------------|
| Default (RSA + ECC, MemUse entropy) | ~652 KB | ~271 KB |
| `-DFWTPM_TINY_ECC` (all cuts, MemUse) | ~254 KB | ~172 KB |
| `-DFWTPM_TINY_ECC -DFWTPM_TINY_PCR8 -DFWTPM_TINY_HWTRNG` (SYSMON TRNG + command gates) | **~190 KB** | **~144 KB** |
| Device budget | **192 KB** | |

The last row **fits with ~1.5 KB to spare.** Two changes closed the final ~62 KB
gap: replacing wolfCrypt MemUse entropy with the on-die SYSMONE4 System Monitor
(saves the 16 KB entropy state + SHA-3, ~30 KB total) and wolfTPM's finer command
gating - the individual `FWTPM_NO_*` command-group macros the example selects
explicitly under `FWTPM_TINY_ECC` in `user_settings.h` (wolfTPM has no umbrella
macro; each gate is a deliberate choice) - which compiles out the key-migration /
ECDH / hash-command / context / symmetric-encrypt / clock command groups
(~20 KB). Both are documented below and in the firmware's `user_settings.h`.

### The two levers that closed the gap

`FWTPM_TINY_ECC` first applies every firmware-side reduction: ECC-P256 only (no
RSA, no P-384), SHA-256 only (no SHA-1), table-free AES, reduced fwTPM context
slots and buffers, an 8 KB NV store, and wolfTPM's `FWTPM_NO_POLICY` /
`NO_ATTESTATION` / `NO_CREDENTIAL` / `NO_DA` / `NO_PARAM_ENC` command gating.
That alone takes the image from 652 KB down to ~254 KB (a 61% cut) - but it is
still ~62 KB over the 192 KB device with MemUse entropy in the picture. Two
further changes close that last gap:

1. **Fabric hardware TRNG (SYSMONE4).** Building with `-DFWTPM_TINY_HWTRNG`
   replaces wolfCrypt's MemUse entropy (a 16 KB memory-jitter state plus the
   SHA-3 conditioner and SP800-90B health-test buffers) with the on-die System
   Monitor read over an AXI System Management Wizard. This removes ~30 KB of
   code+RAM and provides real electrical noise. The wizard is not in the stock
   TRD, so `add_sysmon.tcl` adds it (see "Rebuild the bitstream" below).
2. **wolfTPM finer command gating.** Under `FWTPM_TINY_ECC` the example selects
   the individual `FWTPM_NO_*` command-group macros explicitly in
   `user_settings.h` (wolfTPM has no umbrella macro - each is a deliberate
   choice). It compiles out the key-migration,
   ECDH, hash/HMAC-command, context save/load, symmetric-encrypt and clock
   command groups (~20 KB) that a minimal ECC attestation + NV fTPM does not
   need, while keeping Startup / GetCapability / GetRandom / PCR / Create /
   Load / Sign / VerifySignature / NV / sessions.

Together these bring the image to ~190 KB - it fits the stock 192 KB BRAM with
~1.5 KB to spare, so no memory enlargement is needed.

Constraints worth noting: **AES cannot be removed** (the fwTPM
context-protection key and AES-GCM require it), an **RSA-only** build is larger
(RSA code + 1280-byte key slots vs 256), and an **ML-DSA-only** build is larger
still (Dilithium code + multi-KB keys) - so ECC-P256 is the algorithm that fits.

## Rebuild the bitstream with the SYSMON TRNG

The minimal build reads the System Monitor at `0x44A30000`, which the stock TRD
does not instantiate. Two wolfSSL-authored scripts add it without modifying any
AMD source file:

- `add_sysmon.tcl` - overlay that adds the AXI System Management Wizard
  (SYSMONE4, AXI4-Lite, continuous sequencer over temp/VCCINT/VCCAUX) to the
  block design and maps it at `0x44A30000`.
- `build_sysmon.tcl` - build driver that creates the project, sources the TRD's
  `config_bd.tcl` unmodified, applies the overlay, then synthesizes and
  implements to a device image.

```bash
export XILINX_VIVADO=/tools/Xilinx/2025.1/Vivado
cd fpga
$XILINX_VIVADO/bin/vivado -mode batch -notrace -source build_sysmon.tcl \
    -tclargs -trd /path/to/scu35-zephyr-rtos-io-trd/hw -jobs 8
# -> build_sysmon/scu35_sysmon_wrapper.pdi
```

If you only need to run the `hello` image or a non-TRNG firmware subset, the
stock TRD bitstream is sufficient and no rebuild is needed.

### Hardware validation (SCU35, Vivado 2025.1)

The rebuilt bitstream and the ~190 KB minimal fwTPM were validated on the board:

- Reading the System Monitor over JTAG returns live, plausible ADC codes whose
  low bits jitter run-to-run - temperature `~0xA0xx`, VCCINT, VCCAUX - confirming
  the wizard is configured, the register map (`0x400/0x404/0x408`) is correct,
  and the noise source is real (not a stuck/zero read that would give a constant
  DRBG seed).
- The fwTPM boots on `Hart #0`, `TPM2_Startup` and `TPM2_GetRandom` return
  `rc=0x00000000`, and `TPM2_GetRandom` returns **different** bytes across cold
  reloads - end-to-end proof the Hash-DRBG is seeded from the SYSMON entropy.
- Note: JTAG PDI programming needs the board in JTAG boot mode; if `device
  program` reports `ROM State 0xD, Error 0x94D000`, power-cycle the board into
  JTAG boot mode and retry.

## Program the bitstream and load firmware

1. Program the PDI to the `xcsu35p` (Vivado Hardware Manager, or `xsdb` `device program <wrapper>.pdi` with the board in JTAG boot mode). Use `build_sysmon/scu35_sysmon_wrapper.pdi` for the minimal fwTPM (SYSMON TRNG); the stock TRD PDI otherwise.
2. Start `hw_server`; with `xsdb`, select the MicroBlaze V RISC-V core target, `dow` the firmware ELF, set the PC to `0x0`, and `con`.
3. Read the console (`axi_uartlite_1`, the design's `serial1` = ttyUSB24) over the SCU35 FT4232H UART with `uart-monitor`.

## Address map (from the TRD, for the firmware `scu35_board.h`)

| Block | Base |
|-------|------|
| Local BRAM (reset vector) | `0x00000000` |
| AXI UARTLite 0 | `0x40600000` |
| AXI UARTLite 1 (console, design `serial1`) | `0x40700000` |
| AXI Timer 0 | `0x41C00000` |
| AXI QuadSPI 0 | `0x44A00000` |
| AXI System Management (SYSMONE4 TRNG, added by `add_sysmon.tcl`) | `0x44A30000` |

Core: MicroBlaze V rv32imc, AXI clock 225 MHz.
