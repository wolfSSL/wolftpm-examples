# fwTPM STM32 Port

Firmware TPM 2.0 server running on STM32 Cortex-M33.
Supports TrustZone (CMSE) for hardware-isolated TPM secrets.

## Tested Targets

| Board | Chip | Status | Date |
|-------|------|--------|------|
| NUCLEO-H563ZI | STM32H563ZI (Cortex-M33, 250MHz) | Working (ECC OK, RSA keygen investigating) | 2026-03-20 |

## Prerequisites

- `arm-none-eabi-gcc` toolchain (12.x or later)
- STM32Cube_FW_H5 SDK v1.5.1+
- wolfSSL source tree (default: `/tmp/wolfssl-fwtpm`)
- OpenOCD (STMicroelectronics fork for stm32h5x flash driver)

## Build

```bash
cd src/fwtpm/ports/stm32

# Standard build (TZEN=0, no semihosting)
make

# With semihosting debug output (printf via SWD, UART free for TPM protocol)
make SEMIHOSTING=1

# TrustZone enabled (requires TZEN option byte set)
make TZEN=1

# Override wolfSSL path
make WOLFSSL_DIR=/path/to/wolfssl

# Clean
make clean
```

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

## UART Protocol

The fwTPM speaks the **mssim protocol** (Microsoft TPM simulator) over UART at
115200 8N1 on USART3 (PD8=TX, PD9=RX, ST-Link VCP).

On boot (without semihosting), UART shows:
```
=== wolfTPM fwTPM Server (STM32H5 Secure) ===
fwTPM v0.1.0 initialized OK
  FWTPM_CTX size: 159872 bytes
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
the wolfTPM client library:

```bash
python3 test_uart_tpm.py /dev/ttyACM0
```

Tests: TPM2_Startup, TPM2_SelfTest, TPM2_GetRandom, TPM2_GetCapability.

## Memory Map (TZEN=0)

| Region | Address | Size | Contents |
|--------|---------|------|----------|
| Code + rodata | 0x08000000 | ~184KB | fwTPM + wolfCrypt + STM32 HAL |
| NV flash | 0x081E0000 | 128KB | TLV journal (seeds, keys, PCRs) |
| RAM (BSS+heap+stack) | 0x20000000 | ~263KB | FWTPM_CTX (heap) + 64KB stack |
| **Available** | | **377KB RAM, 1608KB flash** | |

## TrustZone Memory Map (TZEN=1)

| Region | Address | Size | Security |
|--------|---------|------|----------|
| Secure code | 0x0C000000 | 888K | Secure |
| NV storage | 0x0C0DE000 | 128K | Secure |
| NSC stubs | 0x0C0FE000 | 8K | Non-Secure Callable |
| NS app | 0x08100000 | 1024K | Non-Secure |
| Secure RAM | 0x30000000 | 320K | Secure |
| NS RAM | 0x20050000 | 320K | Non-Secure |

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
5. Update `Makefile` with new target option
6. Update this README
