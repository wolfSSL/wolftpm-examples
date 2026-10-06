/* user_settings_tpm.h
 *
 * wolfCrypt and wolfTPM configuration for driving the TPM from the MCU.
 *
 * Copyright (C) 2006-2026 wolfSSL Inc.
 *
 * This file is part of wolfTPM.
 *
 * wolfTPM is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * wolfTPM is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1335, USA
 */
#ifndef USER_SETTINGS_TPM_H
#define USER_SETTINGS_TPM_H

#define SINGLE_THREADED
#define WOLFCRYPT_ONLY
#define NO_FILESYSTEM
#define NO_WRITEV
#define WOLFSSL_NO_SOCK
#define NO_OLD_TLS
#define NO_DH
#define NO_DSA
#define NO_DES3
#define NO_RC4
#define NO_MD5
#define NO_SHA
#define NO_PWDBASED
#define NO_SESSION_CACHE
#define NO_ERROR_STRINGS
#define WOLFSSL_AES_DIRECT
#define HAVE_AES_CBC
#define WOLFSSL_AES_CFB
/* No entropy source on this board, so wolfCrypt's DRBG is seeded from the
 * TPM's own RNG. Only SPDM needs it; every other act has the TPM make the
 * keys. */
extern int app_rand_seed(unsigned char* output, unsigned int sz);
#define CUSTOM_RAND_GENERATE_SEED app_rand_seed

#define HAVE_HKDF
#define HAVE_ECC
#define ECC_USER_CURVES
#define HAVE_ECC256
#define WOLFSSL_SP_MATH_ALL
#define WOLFSSL_HAVE_SP_ECC
#define WOLFSSL_SP_ARM_CORTEX_M_ASM
#define WC_RSA_BLINDING
#define NO_RSA
#define WOLFSSL_SMALL_STACK

/* Without a monotonic clock wolfTPM bounds its wait loops by iteration count
 * instead of time. One million polls of an I2C register at 100 kHz is about
 * seventeen minutes, which reads as a hang rather than a timeout, so give it
 * the board's clock and let TPM_TIMEOUT_MS mean what it says. */
#include <stdint.h>
extern uint32_t demo_uptime_ms(void);
#define XTPM_GET_TIMEMS() ((word32)demo_uptime_ms())

/* wolfTPM: the TCG PTP I2C register map, and the advanced callback form that
 * reports the register and direction separately. */
#define WOLFTPM_I2C
#define WOLFTPM_ADV_IO
#define WOLFTPM_SMALL_STACK

/* TPM 2.0 v1.85, which additively enables the post-quantum algorithm set:
 * Hash-ML-DSA templates, signing and the ML-DSA public key union member. */
#define WOLFTPM_V185

/* Declares TPM2_IoCb without compiling any of wolfTPM's own HAL sources,
 * which is how wolfTPM expects an application to supply its own. The
 * implementation is in tpm_io_i2c_c3.c; WOLFTPM_INCLUDE_IO_FILE is
 * deliberately left undefined. */
#define WOLFTPM_EXAMPLE_HAL

/* Must hold the largest command and response the demo issues. A parameter
 * set 87 signature is 4627 bytes, so the default is far too small for the
 * post-quantum acts even though the identity read would fit. */
#define MAX_COMMAND_SIZE    5120
#define MAX_RESPONSE_SIZE   5120
#define MAX_DIGEST_BUFFER   1024

#endif /* USER_SETTINGS_TPM_H */
