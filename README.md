# Public wolfSSL Examples

## Infineon PSoC6 wolfSSL HTTPS Server Example

See `PSoC6_WiFi_HTTPS_Server`.

Stock Wi-Fi_HTTPS_Server example ported to wolfSSL.
Tested on PSoC 62S2 eval kit (CY8CEVAL-062S2) and Sterling LWB5+ Wifi module.

This requires updating the mtb_shared secure-sockets to use: https://github.com/Infineon/secure-sockets/pull/1
For wolfSSL use: https://github.com/wolfSSL/wolfssl/pull/7369
For wolfTPM use: https://github.com/wolfSSL/wolfTPM/pull/339

Build steps:
* `make getlibs`
* Update `../mtb_shared/secure-sockets` with https://github.com/Infineon/secure-sockets/pull/1
* Update `../mtb_shared/wolfssl` with https://github.com/wolfSSL/wolfssl/pull/7369 (specifically wolfcrypt/src/random.c)
* Update `../mtb_shared/wpa3-external-supplicant` with https://github.com/Infineon/wpa3-external-supplicant/pull/2
* `make -j 12`

Support: For questions email support@wolfssl.com
