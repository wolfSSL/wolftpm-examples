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

## AMD Zynq-7000 fwTPM on Cortex-A9 with SRAM PUF

See [Xilinx/fwtpm-zc702-a9](Xilinx/fwtpm-zc702-a9).

Firmware TPM 2.0 running bare-metal on a single Cortex-A9 of an AMD/Xilinx
Zynq-7000 (ZC702), served to a host over UART with the raw swtpm/mssim framing.
The TPM's NV-journal integrity key is a device-unique key derived from the
Cortex-A9 on-chip-memory (OCM) SRAM power-on state via wolfCrypt's SRAM PUF
(BCH fuzzy extractor + HKDF) - no root key is stored in flash. Entropy is
wolfCrypt MemUse (the Zynq-7000 PS has no hardware TRNG).

## AMD Spartan UltraScale+ SCU35 fwTPM on a MicroBlaze V soft core

See [Xilinx/fwtpm-scu35-microblazev](Xilinx/fwtpm-scu35-microblazev).

Firmware TPM 2.0 on a MicroBlaze V (RISC-V rv32imc) soft core in the fabric of an
AMD Spartan UltraScale+ SCU35 Evaluation Kit (`xcsu35p`, a pure FPGA), served over
UART with the raw swtpm/mssim framing - the AMD analog of the PolarFire Mi-V
example. The full RSA+ECC fwTPM is ~652 KB and needs a larger device, but a
minimal ECC-only build (`FWTPM_TINY_ECC`) fits the stock 192 KB of block RAM
(no DDR on this part): ~190 KB via wolfTPM's per-command-group gates (the
individual `FWTPM_NO_*` macros, selected explicitly in `user_settings.h`) and an
on-die SYSMONE4 fabric TRNG (added by `fpga/add_sysmon.tcl`) in place of MemUse
entropy. Hardware-validated on the SCU35 - TPM2_Startup and TPM2_GetRandom pass and
GetRandom differs across cold boots, confirming real System-Monitor entropy.

## Microchip PolarFire SoC fwTPM on a RISC-V hart (AMP)

See [Microchip/fwtpm-polarfire-miv](Microchip/fwtpm-polarfire-miv).

Firmware TPM 2.0 running bare-metal in M-mode on a dedicated U54 RISC-V
hart of the PolarFire SoC (MPFS250T Video Kit), split from Linux by HSS
AMP partitioning (Linux on harts 1-3, fTPM on hart 4). Linux observes the
fTPM over a shared-memory mailbox + console ring (non-cached DDR) via
`/dev/mem`; the hart runs real TPM 2.0 commands (TPM2_GetCapability,
TPM2_GetRandom seeded from the System Controller nonce service) read live from Linux.

Added in https://github.com/wolfSSL/wolftpm-examples/pull/2

## Microchip PolarFire MPF300 Splash Kit fwTPM on a Mi-V RV32 soft core

See [Microchip/miv-mpf300-splash](Microchip/miv-mpf300-splash).

Firmware TPM 2.0 on a soft MIV_RV32 RISC-V core instantiated in the fabric of a PolarFire MPF300 Splash Kit (a pure FPGA, no hardened CPU). Built and verified on hardware in stages: a CoreUARTapb hello-world with LED heartbeat, wolfCrypt test/benchmark from 512 KB LSRAM, and the fwTPM driven over UART, seeding its Hash-DRBG from the PolarFire System Controller nonce service (NRBG output) and with persistent NV in the on-die secure NVM (sNVM) - both reached through a single CoreSysServices_PF block. Distinct from the PolarFire SoC (MPFS250T) port above, which runs on a hardened U54 hart.

## Xilinx UltraScale+ MPSoC with FreeRTOS, LWIP with wolfSSL/wolfTPM

See: https://github.com/dgarske/UltraZed-EG-wolf

## Support

For questions email [support@wolfssl.com](mailto:support@wolfssl.com).
