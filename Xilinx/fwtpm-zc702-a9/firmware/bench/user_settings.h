/* user_settings.h
 *
 * wolfCrypt benchmark configuration for a single Cortex-A9 (ARMv7-A) of the AMD
 * Zynq-7000 (ZC702). Standalone wolfCrypt (no wolfTPM): measures raw crypto
 * throughput on the A9 core. The ZC702 has DDR, so this benchmarks the full set
 * the fwTPM uses (RSA-2048, ECC P-256/P-384, AES, SHA-2/3, HMAC).
 *
 * The RNG seed here is a deterministic bench-only source (CUSTOM_RAND_GENERATE_
 * SEED); it is NOT an entropy source and must never be used for real keys.
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

/* ---- Platform (bare-metal, no OS/filesystem) ---- */
#define WOLFCRYPT_ONLY
#define SINGLE_THREADED
#define WOLFSSL_SMALL_STACK
#define WOLFSSL_GENERAL_ALIGNMENT   4
#define SIZEOF_LONG_LONG            8
#define NO_FILESYSTEM
#define NO_WRITEV
#define NO_ASN_TIME
#define WOLFSSL_ASN_TEMPLATE
#define LITTLE_ENDIAN_ORDER

/* ---- Benchmark driver ---- */
#define BENCH_EMBEDDED              /* small buffers, short run per algorithm */
#define WOLFSSL_USER_CURRTIME       /* we supply double current_time(int) */
#define NO_MAIN_DRIVER              /* benchmark_test() is called from main.c */

/* ---- Single-precision math (portable C, 32-bit A9) ---- */
#define WOLFSSL_SP_MATH_ALL
#define WOLFSSL_SP_SMALL
#define SP_WORD_SIZE                32
#define WOLFSSL_SP_384
#define WOLFSSL_HAVE_SP_ECC
#define WOLFSSL_HAVE_SP_RSA
#define WOLFSSL_PUBLIC_MP
#define WOLFSSL_KEY_GEN

/* ---- RSA-2048 ---- */
#define WC_RSA_BLINDING
#define WC_RSA_PSS
#define WC_RSA_NO_PADDING

/* ---- ECC P-256 + P-384 (keygen / ECDSA / ECDHE) ---- */
#define HAVE_ECC
#define ECC_USER_CURVES
#undef  NO_ECC256
#define HAVE_ECC384
#define ECC_SHAMIR
#define ECC_TIMING_RESISTANT
#define HAVE_ECC_KEY_EXPORT

/* ---- AES (GCM / CBC / CTR / CMAC) ---- */
#define HAVE_AESGCM
#define HAVE_AES_DECRYPT
#define WOLFSSL_AES_COUNTER
#define WOLFSSL_AES_CFB
#define WOLFSSL_AES_DIRECT
#define HAVE_AES_KEYWRAP
#define WOLFSSL_CMAC

/* ---- Hashing: SHA-1, SHA-2, SHA-3 ---- */
#define WOLFSSL_SHA384
#define WOLFSSL_SHA512
#define WOLFSSL_SHA3
#define HAVE_HKDF
#define HAVE_HMAC

/* ---- RNG: Hash-DRBG seeded by a deterministic bench-only source ----
 * NOT entropy - bench only. See bench_seed() in main.c. */
#define HAVE_HASHDRBG
#define WC_NO_RNG_SEED_FALLBACK
#define CUSTOM_RAND_GENERATE_SEED   bench_seed
#ifndef __ASSEMBLER__
extern int bench_seed(unsigned char* out, unsigned int sz);
#endif
#define NO_OLD_RNGNAME

/* ---- Disabled (not benchmarked here) ---- */
#define NO_DSA
#define NO_DH
#define NO_OLD_TLS
#define NO_RC4
#define NO_MD4
#define NO_MD5
#define NO_DES3
#define NO_PSK
#define NO_PWDBASED
#define NO_PKCS12
#define NO_SESSION_CACHE
#define WOLFSSL_NO_SHAKE128
#define WOLFSSL_NO_SHAKE256

#ifdef __cplusplus
}
#endif

#endif /* WOLFSSL_USER_SETTINGS_H */
