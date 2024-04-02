# Public wolfSSL Examples

## Infineon PSoC6 wolfSSL HTTPS Server Example

See [Infineon/PSoC6_WiFi_HTTPS_Server](/Infineon/PSoC6_WiFi_HTTPS_Server).

Tested on PSoC 62S2 eval kit (CY8CEVAL-062S2) and Sterling LWB5+ Wifi module.

Based on the `Wi-Fi_HTTPS_Server` example. TLS ported to wolfSSL. TPM uses wolfTPM

Build steps:
* `make getlibs`
* Update `../mtb_shared/wolftpm` with https://github.com/wolfSSL/wolfTPM/pull/339
* Update `../mtb_shared/wolfssl` with https://github.com/wolfSSL/wolfssl/pull/7369
* Update `../mtb_shared/secure-sockets` with https://github.com/Infineon/secure-sockets/pull/1
* Update `../mtb_shared/wpa3-external-supplicant` with https://github.com/Infineon/wpa3-external-supplicant/pull/2
* `make -j 12`

Support: For questions email [support@wolfssl.com](mailto:support@wolfssl.com).
