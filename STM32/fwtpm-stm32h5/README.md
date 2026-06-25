# fwTPM STM32 Port

Firmware TPM 2.0 server running on STM32 Cortex-M33.
Supports TrustZone (CMSE) for hardware-isolated TPM secrets.

## Tested Targets

| Board | Chip | Status | Date |
|-------|------|--------|------|
| NUCLEO-H563ZI | STM32H563ZI (Cortex-M33, 250MHz) | TZEN=0 and TZEN=1 verified: boot, Startup/SelfTest/GetRandom/GetCapability, wolfTPM `caps` over UART, append-only NV persistence across reboots | 2026-06-24 |

## Prerequisites

- `arm-none-eabi-gcc` toolchain (12.x or later)
- STM32Cube_FW_H5 SDK v1.5.0+ (v1.6.0 verified)
- wolfTPM source tree, v4.1.0 or later (default: `../../../wolftpm`). The fwTPM
  core is compiled from here; NV uses the core's append-only journal mode for
  write-once flash, enabled by `WOLFTPM_FWTPM_NV_APPEND_ONLY` (defined in
  `user_settings.h`). This needs the wolfTPM "append" NV support from
  https://github.com/wolfSSL/wolfTPM/pull/540.
- wolfSSL source tree (default: `../../../wolfssl`)
- OpenOCD (STMicroelectronics fork for stm32h5x flash driver).
  Building the fork from source requires `libusb-1.0-0-dev`.

## Build

```bash
cd STM32/fwtpm-stm32h5

# Standard build (TZEN=0, no semihosting). The Makefile defaults to
# STM32Cube_FW_H5_V1.5.1; pass STM32CUBE=<path> if a different SDK is installed.
make

# With semihosting debug output (printf via SWD, UART free for TPM protocol)
make SEMIHOSTING=1

# TrustZone enabled (requires TZEN option byte set).
# Flip the option byte once with STM32CubeProgrammer:
#   STM32_Programmer_CLI -c port=swd mode=hotplug -ob TZEN=0xB4
# Or with STM-OpenOCD (stm32h5x trustzone/option_load are TODO stubs,
# but option_write works on FLASH_OPTR at offset 0x074):
#   stm32h5x option_write 0 0x074 0xB4000000 0xFF000000   # enable TZEN
#   stm32h5x option_write 0 0x074 0xC3000000 0xFF000000   # revert
# Follow either command with a board power-cycle (or OpenOCD reset).
make TZEN=1

# Override wolfTPM / wolfSSL source paths
make WOLFTPM_DIR=/path/to/wolftpm WOLFSSL_DIR=/path/to/wolfssl

# Clean
make clean
```

NV storage uses the fwTPM core's **append-only** journal mode for write-once
flash (`WOLFTPM_FWTPM_NV_APPEND_ONLY` in `user_settings.h`, wolfTPM v4.1.0+
incl. PR #540). The core
buffers each program granule and only issues `writeAlign`-aligned, forward,
into-erased writes, so this example's `fwtpm_nv_flash.c` is a plain flash
driver: `read` raw bytes, `write` (program) 16-byte quadwords, `erase` whole
sectors -- no read-modify-write in the port. `FWTPM_NV_FlashHAL_Init()` sets
`hal.appendOnly = 1` and `hal.writeAlign = 16` (the H5 quadword) before
registering the HAL. The header sector is not rewritten per append and a power
loss mid append leaves the previously committed state intact (compaction, on
Clear / shutdown / journal-full, still erases the region). Note: the on-flash
layout differs from earlier example builds -- erase the NV region when
upgrading firmware; the fwTPM regenerates fresh seeds/state on the first boot
after. The NV region's flash sectors have limited erase endurance
(the STM32H5 datasheet rates ~10k cycles); each journal-full compaction
erases the region, so NV-write-heavy workloads (frequent PCR extends / NV
index writes) will wear it over time.

## Flash

Requires the STMicroelectronics OpenOCD fork (has `stm32h5x` flash driver):

```bash
OPENOCD=/path/to/STM-OpenOCD/src/openocd
OPENOCD_SCRIPTS=/path/to/STM-OpenOCD/tcl

# Flash and reset
$OPENOCD -s $OPENOCD_SCRIPTS \
    -f interface/stlink-dap.cfg -f target/stm32h5x.cfg \
    -c "program fwtpm_stm32h5.elf verify reset exit"

# Flash with semihosting (keeps OpenOCD connected for debug output)
$OPENOCD -s $OPENOCD_SCRIPTS \
    -f interface/stlink-dap.cfg -f target/stm32h5x.cfg \
    -c "program fwtpm_stm32h5.elf verify" \
    -c "arm semihosting enable" \
    -c "reset run"

# Reset board only
$OPENOCD -s $OPENOCD_SCRIPTS \
    -f interface/stlink-dap.cfg -f target/stm32h5x.cfg \
    -c "init; reset run; shutdown"
```

The Makefile `flash` target writes the raw `.bin` to `0x08000000`, so it is
TZEN=0 only; for TZEN=1 flash the `.elf` via OpenOCD (above) so the secure
`0x0C000000` addresses are used. When the NV layout changes (e.g. upgrading
from an older build), mass-erase first so stale NV is not misread:
`STM32_Programmer_CLI -c port=swd -e all`.

## UART Protocol

The fwTPM speaks the **mssim protocol** (Microsoft TPM simulator) over UART at
115200 8N1 on USART3 (PD8=TX, PD9=RX, ST-Link VCP).

On boot (without semihosting), UART shows:
```
=== wolfTPM fwTPM Server (STM32H5 Secure) ===
fwTPM v0.1.0 initialized OK
  FWTPM_CTX size: 95952 bytes
  NV flash: 0x081E0000 (128 KB)
Waiting for UART commands...
```

After the boot banner, UART switches to binary mssim protocol. The server
auto-detects both mssim and swtpm (raw TPM) packet formats.

## Running wolfTPM Examples Over UART

Build wolfTPM with UART transport on the host:
```bash
cd /path/to/wolftpm
./configure --enable-swtpm=uart
make
```

Run examples against the STM32 fwTPM:
```bash
# Set the serial device (ST-Link VCP)
export TPM2_SWTPM_HOST=/dev/ttyACM0

# Capabilities
./examples/wrap/caps

# Key generation
./examples/keygen/keygen

# Seal/unseal
./examples/seal/seal

# Full example suite
WOLFSSL_PATH=../wolfssl ./examples/run_examples.sh
```

`TPM2_SWTPM_HOST` may be a symlinked serial path (e.g. `/dev/serial/by-id/...`
or a udev alias) with wolfTPM v4.1.0 or later. The server's UART loop is
byte-oriented, so if a client is interrupted mid-command the stream desyncs;
reset the board to resync.

## Semihosting Debug

When built with `SEMIHOSTING=1`, printf output (including `DEBUG_WOLFTPM` trace)
is routed through the SWD debug probe instead of UART. This keeps UART clean for
the mssim TPM protocol while providing full debug visibility.

**Build and flash with semihosting:**
```bash
make clean && make SEMIHOSTING=1

OPENOCD=/path/to/STM-OpenOCD/src/openocd
OPENOCD_SCRIPTS=/path/to/STM-OpenOCD/tcl
$OPENOCD -s $OPENOCD_SCRIPTS \
    -f interface/stlink-dap.cfg -f target/stm32h5x.cfg \
    -c "program fwtpm_stm32h5.elf verify" \
    -c "arm semihosting enable" \
    -c "reset run" > /tmp/openocd_semihost.log 2>&1 &
```

**Monitor debug output (in another terminal):**
```bash
tail -f /tmp/openocd_semihost.log
```

**Example semihosting output during TPM operations:**
```
fwTPM: Dispatch CC=0x00000144 tag=0x8001 size=12 locality=0
fwTPM: Startup(CLEAR)
fwTPM: Dispatch CC=0x00000131 tag=0x8002 size=355 locality=0
fwTPM: CreatePrimary(hierarchy=0x40000001, type=1, handle=0x80000000)
fwTPM: Dispatch CC=0x00000153 tag=0x8002 size=370 locality=0
fwTPM: Create(parent=0x81000200, type=1)
```

**Note:** OpenOCD must stay running for semihosting to work. Each `printf` halts
the CPU briefly while OpenOCD reads the output via SWD.

## Python Test Script

A standalone test script is included for quick verification without building
the wolfTPM client library. It speaks the raw swtpm framing implemented by
`FwTPM_UartCommandLoop()` (TPM packets sent verbatim over the UART):

```bash
python3 test_uart_tpm.py /dev/ttyACM0
```

Tests: TPM2_Startup, TPM2_SelfTest, TPM2_GetRandom, TPM2_GetCapability.

## RAM Budget (both TZEN=0 and TZEN=1)

Both linker scripts reserve the same heap and stack so behavior matches
across configurations:

| Region | Size | Notes |
|--------|------|-------|
| BSS (incl. FWTPM_CTX) | ~94KB | static buffers |
| Heap reservation | 96KB | wolfCrypt RSA keygen / TPM ops (`_Min_Heap_Size`) |
| Stack reservation | 64KB | top of RAM (`_Min_Stack_Size`) |
| **Total RAM used** | ~254KB | |

TZEN=0 has 640KB SRAM available (386KB headroom). TZEN=1 has 320KB
Secure SRAM available (66KB headroom).

## Memory Map (TZEN=0)

| Region | Address | Size | Contents |
|--------|---------|------|----------|
| Code + rodata | 0x08000000 | ~196KB | fwTPM + wolfCrypt + STM32 HAL |
| NV flash | 0x081E0000 | 128KB | TLV journal (seeds, keys, PCRs) |
| RAM | 0x20000000 | 640KB | BSS + heap + stack (see budget above) |

## TrustZone Memory Map (TZEN=1)

| Region | Address | Size | Security |
|--------|---------|------|----------|
| Secure code | 0x0C000000 | 888K | Secure |
| NV storage | 0x0C0DE000 | 128K | Secure |
| NSC stubs | 0x0C0FE000 | 8K | Non-Secure Callable |
| NS app | 0x08100000 | 1024K | Non-Secure |
| Secure RAM | 0x30000000 | 320K | Secure (BSS + heap + stack) |
| NS RAM | 0x20050000 | 320K | Non-Secure |

### What runs where (TZEN=1)

There are two ways to use the secure fwTPM, and they differ in what runs on
the non-secure side:

- **Over UART (this demo, SWTPM-style):** the entire fwTPM -- USART3 transport,
  command loop, wolfCrypt, and NV -- runs in the **secure** world. `main()`
  never releases the non-secure world, so **nothing runs on the non-secure
  side**; the TPM "client" is the external host over the wire. The "NS app" /
  "NS RAM" rows above show where a non-secure application could live, but this
  demo loads none (and as flashed the secure watermark marks all flash secure).
  TrustZone here isolates the TPM secrets, NV, and crypto from anything later
  added to the normal world.

- **On-chip via the NSC gateway:** a non-secure application on the same H5
  calls into the secure fwTPM through the Non-Secure-Callable veneer (see
  below). That non-secure app is what runs on the non-secure side. It requires
  carving out a non-secure flash/RAM region (secure watermark + SAU) and
  building/flashing a separate non-secure image -- a different setup than the
  UART demo.

## NSC API (TrustZone, for non-secure applications)

Include `fwtpm_nsc.h` and link against `fwtpm_nsc_lib.o`:

```c
#include "fwtpm_nsc.h"

uint8_t cmd[256], rsp[4096];
uint32_t rspSz = sizeof(rsp);

/* Build TPM2_GetRandom command in cmd[] ... */
int rc = FWTPM_NSC_ExecuteCommand(cmd, cmdLen, rsp, &rspSz);
```

## Known Issues

- **RSA Create (child key)**: `TPM2_Create` with RSA type fails with
  `TPM_RC_FAILURE`. RSA CreatePrimary works. Under investigation — likely
  stack overflow or wolfCrypt configuration issue during `wc_MakeRsaKey`.
  ECC key operations work correctly.

## Adding New STM32 Targets

1. Add chip-specific `#elif` block in `user_settings.h`
2. Create linker scripts (`CHIP.ld` for non-TZ, `CHIP_S.ld`/`CHIP_NS.ld` for TZ)
3. Copy startup assembly from SDK
4. Add HAL MSP callbacks for your board's pin assignments
5. For internal-flash NV, implement a plain `FWTPM_NV_HAL` (read / write /
   erase), set `hal.appendOnly = 1` and `hal.writeAlign = <program size>`, and
   define `WOLFTPM_FWTPM_NV_APPEND_ONLY` in `user_settings.h` -- see
   `fwtpm_nv_flash.c`
6. Update `Makefile` with new target option
7. Update this README
