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
* `make build -j8`


## Xilinx UltraScale+ MPSoC with FreeRTOS, LWIP with wolfSSL/wolfTPM

See: https://github.com/dgarske/UltraZed-EG-wolf

## Support

For questions email [support@wolfssl.com](mailto:support@wolfssl.com).
