/* user_settings.h
 *
 * Combined wolfSSL + wolfTPM settings for the wolfTPM firmware TPM (fwTPM) on a
 * MicroBlaze V (RISC-V rv32imc) soft core on the AMD Spartan UltraScale+ SCU35.
 * Modeled on the Mi-V and Zynq-7000 standalone-UART ports: raw swtpm/mssim
 * transport over AXI UARTLite, 32-bit portable-C SP math, and a Hash-DRBG
 * seeded either by wolfCrypt's MemUse entropy (default) or, with
 * FWTPM_TINY_HWTRNG, by the on-die SYSMONE4 System Monitor read over AXI.
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

/* No POSIX sleep on bare metal; back XSLEEP_MS with the AXI Timer delay via a
 * shim (fwtpm_sleep_ms -> mbv_delay_ms). */
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
#ifndef FWTPM_TINY_ECC
#define WOLFSSL_SP_384
#endif
#define WOLFSSL_HAVE_SP_ECC

#define WOLFSSL_PUBLIC_MP
#define WOLFSSL_KEY_GEN

/* ---- RSA vs PQC vs tiny-ECC (mutually exclusive) ----
 * FWTPM_TINY_ECC builds a minimal ECC-P256-only fTPM (no RSA, no P-384, SHA-256
 * only, reduced context slots) to explore fitting a small soft core. */
#if defined(FWTPM_ENABLE_PQC) || defined(FWTPM_TINY_ECC)
#define NO_RSA
#else
#define WOLFSSL_HAVE_SP_RSA
#define WC_RSA_BLINDING
#define WC_RSA_PSS
#define WC_RSA_NO_PADDING
#endif

/* ---- ECC P-256 (+ P-384 unless tiny) ---- */
#define HAVE_ECC
#define ECC_USER_CURVES
#undef  NO_ECC256
#ifndef FWTPM_TINY_ECC
#define HAVE_ECC384
#endif
#ifndef FWTPM_TINY_ECC
#define ECC_SHAMIR          /* faster verify, larger code - drop for tiny */
#endif
#define ECC_TIMING_RESISTANT
#define HAVE_ECC_KEY_EXPORT

#ifdef FWTPM_TINY_ECC
/* Shrink the fwTPM context: fewer object/NV slots, smaller NV data and command
 * buffers. ECC keys are small (FWTPM_MAX_PRIVKEY_DER=256 under NO_RSA). */
#define FWTPM_MAX_COMMAND_SIZE  1024   /* ECC commands fit; halves cmd/rsp bufs */
#define TPM_MAX_DIGEST_SIZE     32     /* SHA-256 max (no SHA-384/512) */
#define FWTPM_MAX_OBJECTS       2
#define FWTPM_MAX_PERSISTENT    2
#define FWTPM_MAX_PRIMARY_CACHE 2
#define FWTPM_MAX_SESSIONS      2
#define FWTPM_MAX_HASH_SEQ      2
#define FWTPM_MAX_SIGN_SEQ      2
#define FWTPM_MAX_NV_INDICES    2
#define FWTPM_MAX_NV_DATA       256
/* IMPLEMENTATION_PCR defaults to 24 (TCG). A minimal fTPM can lower it to
 * shrink the PCR arrays (~200 B/PCR/bank); override here if fewer PCRs are OK. */
#ifdef FWTPM_TINY_PCR8
#define IMPLEMENTATION_PCR      8
#endif
/* Gate out fwTPM command groups the minimal build does not need (policy,
 * attestation, credentials, dictionary-attack lockout, parameter encryption). */
#define FWTPM_NO_POLICY
#define FWTPM_NO_ATTESTATION
#define FWTPM_NO_CREDENTIAL
#define FWTPM_NO_DA
#define FWTPM_NO_PARAM_ENC
/* wolfCrypt trims: no SHA-1 (SHA-256 PCR bank only), table-free AES core.
 * AES itself cannot be removed - the fwTPM context-protection key and AES-GCM
 * require it. */
#define NO_SHA
#define WOLFSSL_AES_SMALL_TABLES
/* wolfTPM finer command gating: compile out the command groups a minimal ECC
 * attestation + NV fTPM does not need. wolfTPM has no "minimal" umbrella macro
 * on purpose - each gate removes real TPM functionality, so the set is selected
 * explicitly here. Together with the five gates above, this is what brings the
 * image under the 192 KB device, while retaining Startup / GetCapability /
 * GetRandom / PCR / Create / Load / Sign / VerifySignature / NV / sessions.
 * Comment out any line below to keep that command group (at a size cost). */
#define FWTPM_NO_KEY_MIGRATION   /* Import / Duplicate / Rewrap */
#define FWTPM_NO_ECDH            /* ECDH / EC_Ephemeral / ZGen / ECC_Parameters */
#define FWTPM_NO_HASH_CMDS       /* Hash / HMAC + hash sequence commands */
#define FWTPM_NO_CONTEXT         /* ContextSave / ContextLoad */
#define FWTPM_NO_SYM_ENCRYPT     /* EncryptDecrypt / EncryptDecrypt2 */
#define FWTPM_NO_CLOCK           /* ReadClock / ClockSet / ClockRateAdjust */
#endif

/* ---- AES ---- */
#define HAVE_AESGCM
#define GCM_SMALL
#define HAVE_AES_DECRYPT
#define WOLFSSL_AES_CFB
#define WOLFSSL_AES_DIRECT
#define HAVE_AES_KEYWRAP
#define WOLFSSL_CMAC

/* ---- Hashing ---- */
#ifndef FWTPM_TINY_ECC
#define WOLFSSL_SHA384      /* needed by ECC P-384 */
#define WOLFSSL_SHA512
#endif
#ifndef FWTPM_TINY_HWTRNG
#define WOLFSSL_SHA3        /* required by the MemUse entropy conditioner (SHA3-256) */
#endif
#define HAVE_HKDF
#define HAVE_HMAC

/* ---- RNG: Hash-DRBG seeded by wolfCrypt MemUse entropy ----
 * The SCU35 fabric provides no hardware TRNG, so MemUse (memory-timing jitter
 * conditioned through SHA3-256, gated fail-closed by SP800-90B health tests)
 * seeds the DRBG. The high-resolution time source is the AXI Timer counter
 * (fwtpm_entropy_timer in fwtpm_clock_mbv.c) via CUSTOM_ENTROPY_TIMEHIRES. */
#define HAVE_HASHDRBG
#define WC_NO_RNG_SEED_FALLBACK
#ifdef FWTPM_TINY_HWTRNG
/* Seed the Hash-DRBG from an FPGA hardware entropy source (the SU35P SYSMONE4
 * System Monitor, read as an AXI peripheral) instead of MemUse. This removes the
 * 16 KB MemUse state, the SHA3-256 conditioner and the health-test buffers
 * (~22 KB RAM + ~6 KB code), and provides real electrical noise as entropy.
 * mbv_trng_seed() is in fwtpm_trng_sysmon.c. */
#define CUSTOM_RAND_GENERATE_SEED   mbv_trng_seed
#ifndef __ASSEMBLER__
extern int mbv_trng_seed(unsigned char* out, unsigned int sz);
#endif
#else
#define HAVE_ENTROPY_MEMUSE
#define ENTROPY_MEMUSE_FORCE_FAILURE
#ifdef FWTPM_TINY_ECC
/* Smallest in-bounds MemUse state: BITS=11 -> 2048 word64 = 16 KB (vs 64 KB).
 * The index bound (255<<(BITS-8)) + (UPDATES-1)<<1 < 2^BITS caps UPDATES at 4. */
#define ENTROPY_NUM_WORDS_BITS      11
#define ENTROPY_NUM_UPDATES         4
#else
#define ENTROPY_NUM_WORDS_BITS      13
#define ENTROPY_NUM_UPDATES         16
#endif
#endif
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
