# Public wolfTPM Examples

## Infineon PSoC6 wolfSSL HTTPS Server Example (using wolfTPM)

See [Infineon/PSoC6_WiFi_HTTPS_Server](/Infineon/PSoC6_WiFi_HTTPS_Server).

Tested on PSoC 62S2 eval kit (CY8CEVAL-062S2) and Sterling LWB5+ Wifi module.

The wolfTPM support for updating Infineon SLB9672/SLB9673 firmware was added here: https://github.com/wolfSSL/wolfTPM/pull/339

Based on the `Wi-Fi_HTTPS_Server` example. TLS ported to wolfSSL. TPM uses wolfTPM

Build steps:
* `make getlibs`
* Update `../mtb_shared/secure-sockets` with https://github.com/Infineon/secure-sockets/pull/1
* Update `../mtb_shared/wpa3-external-supplicant` with https://github.com/Infineon/wpa3-external-supplicant/pull/2
* Update `bsps/TARGET_APP_CY8CEVAL-062S2-LAI-4373M2/config/GeneratedSource/cycfg_system.h` -> `#define CY_CFG_PWR_DEEPSLEEP_LATENCY 125UL`.
* Update `source/secure_http_server.h` WiFi settings (`WIFI_SSID` and `WIFI_PASSWORD`)
* `make build -j8`
* `make program`
* See [wolfTPM Firmware Example](https://github.com/wolfSSL/wolfTPM/tree/master/examples/firmware) for next steps

## STM32H5 Firmware TPM (fwTPM) Port

See [STM32/fwtpm-stm32h5](STM32/fwtpm-stm32h5).

Firmware TPM 2.0 implementation for NUCLEO-H563ZI (Cortex-M33). Supports TrustZone secure and non-TrustZone configurations. Uses UART for mssim protocol transport.

Added in https://github.com/wolfSSL/wolftpm-examples/pull/1

## Xilinx ZCU102 fwTPM on Cortex-R5 Lock-Step

See [Xilinx/fwtpm-zcu102-r5](Xilinx/fwtpm-zcu102-r5).

Firmware TPM 2.0 running bare-metal on the Zynq UltraScale+ MPSoC R5 RPU
in lock-step mode. PetaLinux on the A53 APU acts as TPM client over
OpenAMP RPMsg via Linux remoteproc. Persistent NV in QSPI flash.

## Microchip PolarFire SoC fwTPM on a RISC-V hart (AMP)

See [Microchip/fwtpm-polarfire-miv](Microchip/fwtpm-polarfire-miv).

Firmware TPM 2.0 running bare-metal in M-mode on a dedicated U54 RISC-V
hart of the PolarFire SoC (MPFS250T Video Kit), split from Linux by HSS
AMP partitioning (Linux on harts 1-3, fTPM on hart 4). Linux observes the
fTPM over a shared-memory mailbox + console ring (non-cached DDR) via
`/dev/mem`; the hart runs real TPM 2.0 commands (TPM2_GetCapability,
TPM2_GetRandom from the System Controller TRNG) read live from Linux.

Added in https://github.com/wolfSSL/wolftpm-examples/pull/2

## Microchip PolarFire MPF300 Splash Kit fwTPM on a Mi-V RV32 soft core

See [Microchip/miv-mpf300-splash](Microchip/miv-mpf300-splash).

Firmware TPM 2.0 on a soft MIV_RV32 RISC-V core instantiated in the fabric of a PolarFire MPF300 Splash Kit (a pure FPGA, no hardened CPU). Built and verified on hardware in stages: a CoreUARTapb hello-world with LED heartbeat, wolfCrypt test/benchmark from 512 KB LSRAM, and the fwTPM driven over UART with a hardware TRNG (the PolarFire System Controller NRBG) and persistent NV in the on-die secure NVM (sNVM) - both reached through a single CoreSysServices_PF block. Distinct from the PolarFire SoC (MPFS250T) port above, which runs on a hardened U54 hart.

## Xilinx UltraScale+ MPSoC with FreeRTOS, LWIP with wolfSSL/wolfTPM

See: https://github.com/dgarske/UltraZed-EG-wolf

## Support

For questions email [support@wolfssl.com](mailto:support@wolfssl.com).
