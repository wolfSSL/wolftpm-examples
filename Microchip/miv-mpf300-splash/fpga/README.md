# Mi-V soft-core FPGA platform (MPF300 Splash Kit)

The firmware in this example runs on a soft **MIV_RV32** RISC-V core that must be instantiated in the PolarFire FPGA fabric before any code can run. This directory documents how to get that platform onto the board. No Microchip-copyright Libero project or programming job is redistributed here; obtain the reference design from Microchip and build/program it as below.

## Option A - Microchip reference design (fastest, for hello-world and wolfCrypt)

Microchip's "RISC-V for PolarFire" training package (Libero 2025.x) includes a ready-to-run Splash Kit Mi-V tutorial: a `MIV_RV32` core (rv32imc) with CoreUARTapb, CoreGPIO, CoreTimer and LSRAM, plus a pre-generated programming job.

1. Obtain the "RISC-V for PolarFire" package (`Splash_Mi-V_tutorial`) and the Splash Kit lab guide from Microchip.
2. Install FTDI D2XX/VCP drivers so the FT4232H enumerates its JTAG and UART channels.
3. Program the FPGA with the tutorial's programming job using FlashPro Express (`.job` -> Run PROGRAM action) over the on-board FT4232H.
4. Confirm the memory map matches `firmware/common/miv_board.h` (CoreUARTapb `0x70000000`, CoreGPIO `0x70001000`, CoreTimer `0x70002000`, LSRAM `0x80000000`, 80 MHz). The addresses come from the design's `fpga_design_config.h`. If they differ, override the defaults in `miv_board.h` and the linker `ORIGIN`/`LENGTH`.

The stock design has 64 KB of physical LSRAM but the MIV_RV32 maps only a 16 KB AHB window at the reset vector (`AHB_END_ADDR = 0x80003FFF`), which is enough for the hello-world bring-up (it fits in ~5 KB). It has no DDR4 or SPI-flash controller wired, and the 16 KB window is too small for the wolfCrypt test/benchmark and the fwTPM, so those need the extended platform below (which both enlarges the SRAM and widens the window to 512 KB).

## Option B - Extended platform (for the full wolfCrypt test/bench and the fwTPM)

The reference design's on-chip RAM is a `COREAHBLSRAM_PF` instance (wrapped in
`PF_SRAM_AHBL_AXI`) at the reset vector `0x80000000`, configured `MEM_DEPTH =
65536`. The wolfCrypt test/benchmark image measures ~275 KB of code plus
runtime heap (RSA-2048 keygen) and stack - a budget of roughly 0.5-0.75 MB.

**Two changes are required, plus one optional performance tweak (item 3)** - all
scriptable via Libero Tcl `configure_core`, which merges partial params:

1. **Enlarge the on-chip SRAM** `PF_SRAM_AHBL_AXI_C0` to 512 KB
   (`RDEPTH=131072`, `WDEPTH=131072`, `INIT_RAM:F` since we JTAG-load).
2. **Widen the MIV_RV32 AHB target window** to match. The stock core maps only a
   16 KB AHB window at `0x80000000` (`AHB_END_ADDR = 0x80003FFF`), so the CPU can
   physically place 512 KB of LSRAM but only *address* 16 KB of it - firmware
   larger than 16 KB traps on access above the window. Set
   `AHB_END_ADDR_1:0x8007` `AHB_END_ADDR_0:0xffff` on `MIV_RV32_C0` to make the
   window `0x80000000-0x8007FFFF` (512 KB). A firmware image larger than 16 KB
   (the wolfCrypt or fwTPM build) exercising RAM above the old window confirms
   the widen took effect.

3. **(Performance) enable MIV_RV32 pipeline forwarding + instruction cache.**
   The stock core has both off (`FWD_REGS=0`, `ICACHE_EN=0`) to save fabric.
   `configure_core -component_name {MIV_RV32_C0} -params {"FWD_REGS:true"
   "ICACHE_EN:true"}` (booleans - `:true`, not `:1`) raises the core's IPC; with
   the `-O2` toolchain this takes AES to +51% and ECC P-256 to +46% over the
   stock `-Os` build, for ~1,300 extra LUTs and one LSRAM block.

The 512 KB build uses ~256/952 LSRAM blocks (~27%). Detail on enlarging the SRAM
from LSRAM (no DDR4 needed):

1. In Libero, reconfigure the `COREAHBLSRAM_PF` instance (via `PF_SRAM_AHBL_AXI`)
   to a larger depth - target 512 KB, ideally 1 MB, keeping the base at
   `0x80000000` and the MIV_RV32 reset vector unchanged.
2. Generate the component and the design, then Synthesize -> Place & Route ->
   Generate Bitstream -> Export Programming Job.
3. Update the firmware linker `MEMORY` region `LENGTH` to the new size (and
   `firmware/common/miv_board.h` if the map changes).

DDR4 remains an option if the footprint ever outgrows LSRAM, but is not required
for the wolfCrypt tests or a bare-metal fwTPM.

**Additional peripheral for the fwTPM - as built:**

- **CoreSysServices_PF** on a free CoreAPB3 slave slot (slot 3, base
  `0x70003000`), with only the **Nonce Service** enabled. This one block
  provides *both* fwTPM needs through the System Controller mailbox: the
  hardware TRNG (nonce service, backed by the on-die NRBG) and persistent NV
  (secure-NVM read/write System Services). No `CoreSPI`/external flash is needed
  - the on-board MT25Q is wired to the shared dedicated System-Controller SPI,
  not to fabric pins, so NV lives in on-die sNVM instead.
- Wiring is minimal: connect only `CLK`, `RESETN`, and the `APBSlave` bus; the
  core reaches the System Controller internally (no SC interface to route). Tie
  the `FF_NONTIMED_ENTRY` / `FF_TIMED_ENTRY` inputs to GND (Libero errors on
  floating inputs even with Flash Freeze disabled); status outputs may float.
- Keep CoreTimer0 for timekeeping (this MIV_RV32 has no usable MTIME/mcycle).

After building, export the final memory map and update `firmware/common/miv_board.h`
and the firmware linker scripts to match.

## Verified headless flow (no GUI)

The on-board Embedded FlashPro5 was driven entirely from the command line on a
Linux host (Libero SoC 2025.1 + SoftConsole 2022.2). This is the exact flow
used to bring up the hello-world firmware.

1. Board power: connect the 12 V jack **and** turn on SW1 (the board default
   J4 makes SW1 the manual power switch). The FT4232H enumerates on USB power
   alone, so a clean `dmesg` does not mean the FPGA rail is up.

2. Confirm the JTAG chain (read-only) with FlashPro Express in batch mode. A
   Tcl script does `create_job_project` from the tutorial's `PROC_SUBSYSTEM.job`,
   `refresh_prg_list`, then `scan_chain_prg -name {<programmer>}`. Run headless:
   `xvfb-run FPExpress SCRIPT:scan.tcl LOGFILE:scan.log`. A healthy scan reports
   the device IDCODE (production MPF300T = `5f8131cf`); an all-`F` read means no
   power or the JTAG jumpers (J11 closed 1-2, J5-J9 closed 2-3) are wrong.

3. Program the fabric: same project, `run_selected_actions -name {<programmer>}`
   (the job's default action is PROGRAM). PROGRAM does a full erase+program+verify
   and takes ~3-4 minutes on the MPF300T; run it in the background so it is not
   interrupted.

4. JTAG-load the firmware over the same FlashPro5 with SoftConsole's OpenOCD +
   RISC-V GDB (no bitstream rebuild):
   - `openocd -s <sc>/openocd/share/openocd/scripts -f board/microsemi-riscv.cfg`
     (auto-detects the FlashPro5, brings up the Mi-V hart on gdb port 3333).
   - `riscv64-unknown-elf-gdb miv-hello.elf` with: `set arch riscv:rv32`,
     `target extended-remote localhost:3333`, `monitor reset halt`, `load`,
     `set $pc = 0x80000000`, `monitor resume`. Killing OpenOCD afterward leaves
     the hart running from LSRAM.

The hello-world console banner and per-second heartbeat then appear on the CoreUARTapb
UART (FT4232H channel C).

## Programming and console

- JTAG/FlashPro and the UART console are both on the FT4232H. Identify which `ttyUSB` is the CoreUARTapb console (115200 8N1) on your host.
- For fast firmware iteration, JTAG-load the ELF into LSRAM/DDR (SoftConsole debug, or OpenOCD with a Mi-V target config) rather than rebuilding the bitstream. Only re-run the FPGA programming job when the fabric design itself changes.

## References

- MPF300 Splash Kit User Guide UG0786 (pinout, SPI flash, DDR4, FT4232H).
- Microchip "RISC-V for PolarFire" training package (Libero + SoftConsole Splash Kit Mi-V tutorial).
- Mi-V soft processor ecosystem: MIV_RV32 handbook, SoftConsole, and the CoreUARTapb / CoreGPIO / CoreTimer / CoreSPI / CoreSysServices_PF driver documentation.
