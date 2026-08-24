/* user_settings.h
 *
 * Combined wolfSSL + wolfTPM settings for the wolfTPM firmware TPM (fwTPM) on
 * the Zynq-7000 Cortex-A9 (ZC702), served over UART. Modeled on the STM32H5 and
 * Mi-V standalone-UART ports for the transport, and on the ZCU102 R5 port for
 * the entropy source: the Zynq-7000 PS has no hardware TRNG, so the Hash-DRBG
 * is seeded by wolfCrypt's MemUse entropy (memory-timing jitter conditioned
 * through SHA3-256, gated by SP800-90B health tests). SP math is 32-bit
 * portable C for the ARMv7-A core.
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

/* No POSIX sleep on bare-metal A9; back XSLEEP_MS with a shim over the Global
 * Timer delay (fwtpm_sleep_ms -> zynq_delay_ms) so TPM retry waits are real
 * time. The shim keeps a stable prototype independent of the HAL's uint32_t. */
#ifndef __ASSEMBLER__
extern void fwtpm_sleep_ms(unsigned int ms);
#endif
#define XSLEEP_MS(ms) fwtpm_sleep_ms((unsigned int)(ms))

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
#define NO_ASN_TIME                 /* no RTC */
#define WOLFSSL_ASN_TEMPLATE
#define LITTLE_ENDIAN_ORDER

/* ---- Single-precision math (portable C, 32-bit, arbitrary sizes for TPM) ---- */
#define WOLFSSL_SP_MATH_ALL
#define WOLFSSL_SP_SMALL
#define SP_WORD_SIZE                32
#define WOLFSSL_SP_384
#define WOLFSSL_HAVE_SP_ECC

#define WOLFSSL_PUBLIC_MP
#define WOLFSSL_KEY_GEN

/* ---- RSA vs PQC (mutually exclusive) ----
 * Default: RSA + ECC. Build with -DFWTPM_ENABLE_PQC for an ECC + post-quantum
 * (ML-DSA / ML-KEM) TPM instead; that drops RSA and adds SHA-3 / SHAKE. */
#ifdef FWTPM_ENABLE_PQC
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
#define WOLFSSL_SHA3        /* required by the MemUse entropy conditioner (SHA3-256) */
#define HAVE_HKDF
#define HAVE_HMAC

/* ---- SRAM PUF: device-unique NV integrity key ---- */
/* Derives the fwTPM NV-journal integrity key from the OCM power-on SRAM state
 * (BCH fuzzy extractor + HKDF; see fwtpm_puf.c). The BCH profile comes from the
 * Makefile PUF_T / PUF_CW (-DWC_PUF_BCH_T / -DWC_PUF_NUM_CODEWORDS); puf.h
 * defaults to t=10, cw=16 when unset. Building -DFWTPM_PUF_SELFTEST additionally
 * enables the synthetic on-target regression (WOLFSSL_PUF_TEST). */
#define WOLFSSL_PUF
#ifdef FWTPM_PUF_SELFTEST
#define WOLFSSL_PUF_TEST
#endif

/* ---- RNG: Hash-DRBG seeded by wolfCrypt MemUse entropy ----
 * The Zynq-7000 PS has no hardware TRNG. MemUse samples memory-access timing
 * jitter, conditions it through SHA3-256, and enforces SP800-90B RCT/APT health
 * tests fail-closed (seeding, and FWTPM_Init, fail rather than emit weak keys).
 * NO_DEV_RANDOM is intentionally NOT defined (it would select a bare-metal seed
 * branch without MemUse support); ENTROPY_MEMUSE_FORCE_FAILURE compiles out the
 * /dev/urandom fallback so no OS file APIs are referenced.
 *
 * The A9 has no ARMv7 generic timer, so the high-resolution time source for the
 * jitter sampler is supplied via CUSTOM_ENTROPY_TIMEHIRES, backed by the A9 PMU
 * cycle counter (fwtpm_entropy_timer in fwtpm_clock_zynq.c). The I-cache (on)
 * keeps the SHA3-heavy seeding fast enough to be usable.
 *
 * ENTROPY_NUM_WORDS_BITS=13 -> 64 KiB state array (8192 x word64). With
 * ENTROPY_NUM_UPDATES=16 the max state index (255<<5)+(15<<1)=8190 stays in
 * bounds (the default 18 updates would write 4 words past the end at BITS=13). */
#define HAVE_HASHDRBG
#define WC_NO_RNG_SEED_FALLBACK
#define HAVE_ENTROPY_MEMUSE
#define ENTROPY_MEMUSE_FORCE_FAILURE
#define ENTROPY_NUM_WORDS_BITS      13
#define ENTROPY_NUM_UPDATES         16
#define CUSTOM_ENTROPY_TIMEHIRES()  fwtpm_entropy_timer()
#ifndef __ASSEMBLER__
extern unsigned long long fwtpm_entropy_timer(void);
#endif
#define NO_OLD_RNGNAME

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

#ifdef FWTPM_ENABLE_PQC
/* Post-quantum: ML-DSA (sign) + ML-KEM (encap). Both need SHA-3 / SHAKE and the
 * wolfTPM v1.85 spec support. Trimmed to the small-memory paths. */
#define WOLFTPM_V185
#define WOLFSSL_EXPERIMENTAL_SETTINGS
#define WOLFSSL_SHAKE128
#define WOLFSSL_SHAKE256
#define WOLFSSL_HAVE_MLDSA
#define WOLFSSL_WC_DILITHIUM
#define WOLFSSL_DILITHIUM_NO_LARGE_CODE
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
