/* user_settings.h
 *
 * Combined wolfSSL + wolfTPM settings for PolarFire SoC fwTPM server.
 * U54 hart 4, bare-metal M-mode, shared-memory TIS transport.
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

#ifndef FWTPM_U54_USER_SETTINGS_H
#define FWTPM_U54_USER_SETTINGS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================= */
/* wolfTPM settings                                                          */
/* ========================================================================= */
#define WOLFTPM_FWTPM
#define WOLFTPM_FWTPM_TIS
#define WOLFTPM_USER_SETTINGS

/* Use stack buffers -- we have 64KB stack in DDR, no heap needed */
/* #define WOLFTPM_SMALL_STACK */

/* No POSIX sleep available. Busy-wait on the CLINT MTIME counter
 * (memory-mapped at CLINT_BASE 0x02000000 + 0xBFF8, runs at 1 MHz on
 * PolarFire SoC, so 1000 ticks = 1 ms). Accurate regardless of CPU clock
 * or compiler optimization, unlike an empty iteration-count loop. Kept
 * self-contained here (no mpfs_hal.h) since user_settings.h is included
 * very early by the wolfSSL build. */
#define XSLEEP_MS(ms) do { \
    volatile uint64_t *_mt = (volatile uint64_t *)(0x02000000UL + 0xBFF8UL); \
    uint64_t _start = *_mt; \
    while ((*_mt - _start) < (uint64_t)(ms) * 1000UL) { } \
} while (0)

/* ========================================================================= */
/* Platform                                                                  */
/* ========================================================================= */
#define WOLFCRYPT_ONLY
#define NO_FILESYSTEM
#define NO_MAIN_DRIVER
#define SINGLE_THREADED
#define NO_WRITEV
#define NO_DEV_RANDOM
#define WOLFSSL_IGNORE_FILE_WARN
#define WOLFSSL_USER_IO

/* ========================================================================= */
/* Endianness                                                                */
/* ========================================================================= */
#define LITTLE_ENDIAN_ORDER

/* ========================================================================= */
/* Math backend (64-bit SP)                                                  */
/* ========================================================================= */
#define WOLFSSL_SP
#define WOLFSSL_SP_MATH_ALL
#define WOLFSSL_SP_SMALL
#define WOLFSSL_HAVE_SP_RSA
#define WOLFSSL_HAVE_SP_ECC
#define SP_WORD_SIZE 64
#define HAVE___UINT128_T

/* ========================================================================= */
/* RNG / entropy                                                             */
/*                                                                           */
/* The DRBG seed source is selected by FWTPM_RNG (see Makefile). HASHDRBG    */
/* with no seed fallback in both cases, so a seed failure fails rather than  */
/* silently weakening keys.                                                  */
/* ========================================================================= */
#define HAVE_HASHDRBG
#define WC_NO_RNG_SEED_FALLBACK

#if defined(FWTPM_RNG_SCB_NONCE)
    /* Default/production backend: DRBG seeded from the PolarFire SoC System  */
    /* Controller hardware nonce service (fwtpm_trng_scb.c). BENCH-VALIDATED; */
    /* see the SCB mailbox-contention caveat in that file's header.          */
    #define CUSTOM_RAND_GENERATE_SEED mpfs_rng_seed_scb
#else
    /* DEVELOPMENT-ONLY backend (FWTPM_RNG=JITTER): mpfs_rng_seed_cb derives  */
    /* bytes from MTIME jitter, predictable across boots and UNACCEPTABLE for */
    /* the long-term key storage use case in the README. The opt-in is NOT   */
    /* auto-defined here: it must be acknowledged explicitly on the build     */
    /* line (make FWTPM_RNG=JITTER FWTPM_DEV_INSECURE_RNG=1), and mpfs_hal.c   */
    /* #errors without it -- so the weak seed can never be selected silently  */
    /* and a plain `make` uses the hardware TRNG instead.                    */
    #define CUSTOM_RAND_GENERATE_SEED mpfs_rng_seed_cb
#endif

/* ========================================================================= */
/* Hashes                                                                    */
/* ========================================================================= */
#define WOLFSSL_SHA256
#define WOLFSSL_SHA384
#define WOLFSSL_SHA512
#define HAVE_HKDF
#define HAVE_HMAC

/* ========================================================================= */
/* Ciphers                                                                   */
/* ========================================================================= */
#define HAVE_AESGCM
#define HAVE_AESCCM
#define WOLFSSL_AES_DIRECT
#define WOLFSSL_AES_COUNTER
#define HAVE_AES_CBC
#define HAVE_AES_DECRYPT
#define WOLFSSL_AES_CFB
#define WOLFSSL_CMAC

/* ========================================================================= */
/* Disable legacy / unneeded                                                 */
/* ========================================================================= */
#define NO_MD4
#define NO_MD5
#define NO_DSA
#define NO_DES3
#define NO_RC4
#define NO_HC128
#define NO_RABBIT
#define NO_PSK
#define NO_DH

/* ========================================================================= */
/* Asymmetric                                                                */
/* ========================================================================= */
#define HAVE_ECC
#define ECC_USER_CURVES
#define HAVE_ECC256
#define HAVE_ECC384
#define WOLFSSL_KEY_GEN

#define WC_RSA_BLINDING
#define WC_RSA_PSS

/* ========================================================================= */
/* Debug                                                                     */
/* ========================================================================= */
/* Verbose wolfSSL/wolfTPM logging, gated behind FWTPM_DEBUG (set by the
 * Makefile, on by default for this bring-up example). Build FWTPM_DEBUG=0
 * for a quiet/release-style build. */
#if defined(FWTPM_DEBUG)
    #define DEBUG_WOLFSSL
    #define DEBUG_WOLFTPM
#endif

#ifdef __cplusplus
}
#endif

#endif /* FWTPM_U54_USER_SETTINGS_H */
