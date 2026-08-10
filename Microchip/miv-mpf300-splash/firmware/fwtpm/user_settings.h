/* user_settings.h
 *
 * Combined wolfSSL + wolfTPM settings for the wolfTPM firmware TPM (fwTPM) on
 * the Mi-V RV32 soft core (PolarFire MPF300 Splash Kit). Modeled on the STM32H5
 * fwTPM port, with rv32imc portable-C math, persistent NV in the on-die sNVM
 * and entropy from the System Controller NRBG (see fwtpm_nv_snvm.c and
 * fwtpm_rng_sysserv.c).
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA
 */

#ifndef WOLFSSL_USER_SETTINGS_H
#define WOLFSSL_USER_SETTINGS_H

#ifdef __cplusplus
extern "C" {
#endif

/* ---- wolfTPM fwTPM ---- */
#define WOLFTPM_FWTPM
#define WOLFTPM_SMALL_STACK
/* NV is persistent, backed by the PolarFire on-die sNVM through the System
 * Controller services (fwtpm_nv_snvm.c). The region size and its sNVM page
 * placement are defined there. */

/* ---- Platform (bare-metal, no OS/filesystem) ---- */
#define WOLFCRYPT_ONLY
#define SINGLE_THREADED
#define WOLFSSL_SMALL_STACK
#define WOLFSSL_GENERAL_ALIGNMENT   4
#define SIZEOF_LONG_LONG            8
#define WOLFSSL_USER_IO
#define WOLFSSL_NO_SOCK
#define NO_FILESYSTEM
#define NO_MAIN_DRIVER
#define NO_WRITEV
#define NO_DEV_RANDOM
#define NO_ASN_TIME                 /* no RTC */
#define WOLFSSL_ASN_TEMPLATE

/* ---- Single-precision math (portable C, 32-bit, arbitrary sizes for TPM) ---- */
#define WOLFSSL_SP_MATH_ALL
#define WOLFSSL_SP_SMALL
#define SP_WORD_SIZE                32
#define WOLFSSL_SP_384
#define WOLFSSL_HAVE_SP_ECC

#define WOLFSSL_PUBLIC_MP
#define WOLFSSL_KEY_GEN

/* ---- RSA vs PQC (mutually exclusive to fit 512 KB LSRAM) ----
 * Default: RSA + ECC. Build with -DMIV_FWTPM_PQC for an ECC + post-quantum
 * (ML-DSA / ML-KEM) TPM instead; that drops RSA to make room and adds SHA-3 /
 * SHAKE. See the Makefile EXTRA_CFLAGS notes. */
#ifdef MIV_FWTPM_PQC
#define NO_RSA
#else
#define WOLFSSL_HAVE_SP_RSA
#define WC_RSA_BLINDING
#define WC_RSA_PSS
#define WC_RSA_NO_PADDING
#endif

/* ---- ECC P-256 / P-384 ---- */
#define HAVE_ECC
#define ECC_USER_CURVES
#undef  NO_ECC256
#define HAVE_ECC384
#define ECC_SHAMIR
#define ECC_TIMING_RESISTANT
#define HAVE_ECC_KEY_EXPORT

/* ---- AES (CFB + keywrap + GCM for the TPM) ---- */
#define HAVE_AESGCM
#define GCM_SMALL
#define HAVE_AES_DECRYPT
#define WOLFSSL_AES_CFB
#define WOLFSSL_AES_DIRECT
#define HAVE_AES_KEYWRAP
#define WOLFSSL_CMAC

/* ---- Hashing (SHA-1 kept: RSA OAEP MGF1 default) ---- */
#define WOLFSSL_SHA384
#define WOLFSSL_SHA512
#define HAVE_HKDF

/* ---- RNG: Hash-DRBG seeded from the System Controller Nonce Service ----
 * Seed material comes from the PolarFire System Controller NRBG (nonce
 * service, opcode 0x21) via the CoreSysServices_PF mailbox (see
 * fwtpm_rng_sysserv.c). wolfCrypt's Hash-DRBG expands it and reseeds from the
 * nonce service every WC_RESEED_INTERVAL. On the non-S MPF300T this NRBG is an
 * SRAM-PUF-seeded iRNG (not SP800-90A/B certified); it is used only as seed for
 * the SP800-90A Hash-DRBG, which produces the TPM's random stream. */
#define HAVE_HASHDRBG
#define WC_RESEED_INTERVAL          (1000000)
#define NO_OLD_RNGNAME
#define CUSTOM_RAND_GENERATE_SEED   miv_get_seed
#ifndef __ASSEMBLER__
extern int miv_get_seed(unsigned char* output, unsigned int sz);
#endif

/* ---- Disabled legacy/unused ---- */
#define NO_OLD_TLS
#define NO_DSA
#define NO_DH
#define NO_RC4
#define NO_MD4
#define NO_MD5
#define NO_DES3
#define NO_PSK
#define NO_PWDBASED
#define NO_PKCS12
#define NO_SESSION_CACHE
#ifdef MIV_FWTPM_PQC
/* Post-quantum: ML-DSA (sign) + ML-KEM (encap). Both need SHA-3 / SHAKE, and
 * the wolfTPM v1.85 spec support (ML-DSA/ML-KEM key types, sign/verify
 * sequences). Trimmed to ML-KEM-768 and the small-memory paths to fit 512 KB. */
#define WOLFTPM_V185
#define WOLFSSL_SHA3
#define WOLFSSL_SHAKE128
#define WOLFSSL_SHAKE256
#define WOLFSSL_HAVE_MLDSA
#define WOLFSSL_WC_DILITHIUM
#define WOLFSSL_DILITHIUM_NO_LARGE_CODE
/* Sign/verify small-memory paths so the larger ML-DSA levels fit 512 KB. */
#define WOLFSSL_MLDSA_SIGN_SMALL_MEM
#define WOLFSSL_MLDSA_SIGN_SMALL_MEM_PRECALC
#define WOLFSSL_MLDSA_VERIFY_SMALL_MEM
#define WOLFSSL_MLDSA_MAKE_KEY_SMALL_MEM
#define WOLFSSL_HAVE_MLKEM
#define WOLFSSL_WC_MLKEM
#define WOLFSSL_MLKEM_SMALL
#define WOLFSSL_NO_ML_KEM_512
#define WOLFSSL_NO_ML_KEM_1024
#else
#define WOLFSSL_NO_SHAKE128
#define WOLFSSL_NO_SHAKE256
#endif

#ifdef __cplusplus
}
#endif

#endif /* WOLFSSL_USER_SETTINGS_H */
