/* user_settings.h
 *
 * wolfCrypt configuration for the Mi-V RV32 soft core on the PolarFire MPF300
 * Splash Kit - bare-metal, no OS, no filesystem, single-precision (SP) math.
 * Runs wolfcrypt_test and benchmark_test. Modeled on the wolfSSL SiFive
 * HiFive1 rv32 example (IDE/RISCV/SIFIVE-HIFIVE1).
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

/* Platform / build model */
#define WOLFCRYPT_ONLY
#define SINGLE_THREADED
#define WOLFSSL_SMALL_STACK
#define WOLFSSL_GENERAL_ALIGNMENT   4
#define SIZEOF_LONG_LONG            8
#define WOLFSSL_USER_IO
#define NO_FILESYSTEM
#define NO_WRITEV
#define NO_MAIN_DRIVER
#define NO_DEV_RANDOM

/* Single-precision math only (no fast-math, 32-bit words) */
#define WOLFSSL_SP_SMALL
#define SP_WORD_SIZE                32
#define WOLFSSL_SP_MATH
#define WOLFSSL_HAVE_SP_ECC
#define WOLFSSL_HAVE_SP_RSA
#define WOLFSSL_SP_384              /* SP ECC P-384 support (fixes ecc test) */

/* Performance note - RISC-V assembly is NOT enabled here, on purpose:
 *  - WOLFSSL_SP_RISCV32 (generic sp_int.c asm) was measured to be a no-op for
 *    this config: RSA/ECC run through the curve/size-specific sp_c32.c, not the
 *    generic sp_int.c path, so it produced a byte-identical binary.
 *  - The wolfSSL --enable-riscv-asm crypto/bit-manip/vector paths
 *    (WOLFSSL_RISCV_SCALAR_CRYPTO_ASM / _BIT_MANIPULATION / _VECTOR, i.e.
 *    zkned/zbb/zba/zbkb/zv*) require ISA extensions this MIV_RV32 does not have
 *    - the core is rv32imc (I+M+C only, misa=0x42001104) - and that code is
 *    RV64-only. Enabling any of it emits instructions that trap on this core. */

/* RSA (SP-backed) */
#define WC_RSA_BLINDING
#define WC_RSA_PSS

/* ECC: NIST P-256 and P-384 */
#define HAVE_ECC
#define ECC_USER_CURVES
#undef  NO_ECC256
#define HAVE_ECC384
#define ECC_TIMING_RESISTANT
#define HAVE_ECC_KEY_EXPORT

/* Symmetric. Trimmed toward the fwTPM's algorithm families (no ChaCha/Poly1305)
 * and to free room for ML-DSA/ML-KEM at -O2. This is a performance/coverage
 * benchmark, not a bit-exact mirror of the fwTPM build: it uses WOLFSSL_SP_MATH
 * (fixed-size) + GCM_TABLE_4BIT and omits AES-CFB / key-wrap / key-gen, which the
 * fwTPM config (SP_MATH_ALL, WOLFSSL_AES_CFB, HAVE_AES_KEYWRAP, WOLFSSL_KEY_GEN)
 * enables. */
#define HAVE_AES_CBC
#define HAVE_AESGCM
#define GCM_TABLE_4BIT              /* 4-bit GHASH tables: faster GCM/GMAC than
                                     * GCM_SMALL, ~256 bytes/key of extra data */
#define WOLFSSL_AES_COUNTER
#define WOLFSSL_AES_DIRECT
#define WOLFSSL_CMAC

/* Hashes */
#define WOLFSSL_SHA512
#define WOLFSSL_SHA384
#define WOLFSSL_SHA3
#define HAVE_HKDF

/* Disabled legacy/unused to keep footprint down */
#define NO_DH
#define NO_DSA
#define NO_DES3
#define NO_RC4
#define NO_MD4
#define NO_MD5
#define NO_PWDBASED
#define NO_PKCS12

/* No real-time clock on this board; skip time-based ASN.1 cert-validity checks.
 * retarget.c's _gettimeofday reports only monotonic time since boot. */
#define NO_ASN_TIME

/* RNG: DRBG seeded by a custom source. NOTE: my_rng_seed_gen() is a placeholder
 * counter for test/bench bring-up only; the fwTPM example uses the PolarFire
 * System Controller nonce service instead. Not secure as-is. */
#define HAVE_HASHDRBG
#define CUSTOM_RAND_TYPE            unsigned int
#define CUSTOM_RAND_GENERATE       my_rng_seed_gen
#ifndef __ASSEMBLER__
extern unsigned int my_rng_seed_gen(void); /* provided in main.c */
#endif

/* ---- Post-quantum: ML-DSA (Dilithium) + ML-KEM (Kyber), enabled by default ----
 * This example targets a PQC-capable TPM, so ML-DSA and ML-KEM are always in the
 * test + benchmark. The small-memory sign/verify/make-key paths let every enabled
 * ML-DSA level run on this core; trimmed to ML-DSA-44/-65 and ML-KEM-768 to fit
 * alongside the RSA/ECC/AES/SHA suite in 512 KB LSRAM. */
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
#define WOLFSSL_NO_ML_DSA_87   /* drop largest level to fit; 44+65 benched */

/* Benchmark/test tuning */
#define BENCH_EMBEDDED
#define USE_CERT_BUFFERS_2048
#define USE_CERT_BUFFERS_256
#define WOLFSSL_USER_CURRTIME       /* benchmark uses current_time() */
#define USER_TICKS

#ifdef __cplusplus
}
#endif

#endif /* WOLFSSL_USER_SETTINGS_H */
