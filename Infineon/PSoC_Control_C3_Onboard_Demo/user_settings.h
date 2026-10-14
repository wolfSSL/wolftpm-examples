/* user_settings.h
 *
 * wolfCrypt configuration for ML-DSA verification on a Cortex-M33.
 *
 * Verify only: the TPM does the signing, so key generation and signing are
 * compiled out. WOLFSSL_MLDSA_VERIFY_SMALLEST_MEM is the setting that makes
 * this fit: without it a parameter set 87 verification expands the matrix A
 * into about 80 KB of heap, and with it the matrix is recomputed on the fly
 * and verification allocates nothing at all.
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
#ifndef USER_SETTINGS_H
#define USER_SETTINGS_H

#define SINGLE_THREADED
#define WOLFCRYPT_ONLY
#define NO_FILESYSTEM
#define NO_WRITEV
#define WOLFSSL_NO_SOCK
/* wolfSSL includes this file from settings.h, so it governs every build
 * here regardless of which user_settings the command line force-includes.
 * Acts that only ask the TPM for keys need no DRBG; SPDM generates an
 * ephemeral key on the MCU and does, so it defines APP_NEED_RNG. */
#ifndef APP_NEED_RNG
#define NO_DEV_RANDOM
#define WC_NO_RNG
#endif
#define NO_RSA
#define NO_DH
#define NO_DSA
#define NO_DES3
#define NO_RC4
#define NO_MD5
#define NO_SHA
#define NO_PWDBASED
#define NO_ASN_TIME
#define NO_CERTS
#define NO_CODING
#define NO_SESSION_CACHE
#define NO_ERROR_STRINGS

#define WOLFSSL_SHA3
#define WOLFSSL_SHAKE128
#define WOLFSSL_SHAKE256

#define WOLFSSL_HAVE_MLDSA
#define WOLFSSL_WC_MLDSA
#define HAVE_DILITHIUM
#define WOLFSSL_MLDSA_NO_SIGN
#define WOLFSSL_MLDSA_NO_MAKE_KEY
#define WOLFSSL_MLDSA_VERIFY_SMALLEST_MEM

#endif /* USER_SETTINGS_H */
