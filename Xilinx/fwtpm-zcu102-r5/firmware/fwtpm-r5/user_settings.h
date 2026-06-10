/* user_settings.h
 *
 * Combined wolfSSL + wolfTPM settings for ZCU102 fwTPM server
 * running bare-metal on the Cortex-R5 RPU in lock-step mode.
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

#ifndef FWTPM_ZCU102_R5_USER_SETTINGS_H
#define FWTPM_ZCU102_R5_USER_SETTINGS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================= */
/* wolfTPM settings                                                          */
/* ========================================================================= */
#define WOLFTPM_FWTPM
/* TPM 2.0 v1.85 dispatch (WOLFTPM_V185) is gated together with PQC in
 * the Post-Quantum block below: fwtpm/fwtpm_crypto.c compiles ML-DSA /
 * ML-KEM unconditionally under WOLFTPM_V185, so v1.85 cannot build
 * without the wolfCrypt PQC algorithms. Both are OFF by default (the
 * transport cannot carry PQC payloads); build -DFWTPM_ENABLE_PQC to
 * enable v1.85 + PQC together. The default build is plain TPM 2.0,
 * which the rpmsg smoke test exercises. */
/* WOLFSSL_USER_SETTINGS / WOLFTPM_USER_SETTINGS are defined on the
 * compiler command line by the Makefile; do not redefine here. */

/* No POSIX sleep on bare-metal R5; provide a busy-wait XSLEEP_MS */
#define XSLEEP_MS(ms) do { \
    volatile uint32_t _i; \
    for (_i = 0; _i < (uint32_t)(ms) * 60000; _i++) { } \
} while (0)

/* ========================================================================= */
/* Platform                                                                  */
/* ========================================================================= */
#define WOLFCRYPT_ONLY
#define NO_FILESYSTEM
#define NO_MAIN_DRIVER
#define SINGLE_THREADED
#define NO_WRITEV
/* NOTE: NO_DEV_RANDOM is intentionally NOT defined. It would select the
 * bare-metal wc_GenerateSeed branch in random.c that has no MemUse
 * support (cryptoCb-or-#error); the MemUse path lives in the generic
 * seed branch. ENTROPY_MEMUSE_FORCE_FAILURE (RNG section) compiles out
 * the /dev/urandom fallback so no OS file APIs are referenced. */
#define WOLFSSL_IGNORE_FILE_WARN
#define WOLFSSL_USER_IO

/* ========================================================================= */
/* Endianness (R5 default, ZCU102 boots LE)                                  */
/* ========================================================================= */
#define LITTLE_ENDIAN_ORDER

/* ========================================================================= */
/* Math backend (32-bit SP -- R5 is 32-bit ARMv7-R, no 64-bit native mul)    */
/* ========================================================================= */
#define WOLFSSL_SP
#define WOLFSSL_SP_MATH_ALL
#define WOLFSSL_SP_SMALL
#define WOLFSSL_HAVE_SP_RSA
#define WOLFSSL_HAVE_SP_ECC
#define SP_WORD_SIZE 32

/* ========================================================================= */
/* RNG / entropy                                                             */
/* ========================================================================= */
/* HASH_DRBG seeded by wolfCrypt's MemUse entropy source. MemUse samples
 * memory-access timing jitter, conditions it through SHA3-256, and
 * enforces SP800-90B RCT/APT health tests fail-closed (wc_Entropy_Get
 * returns ENTROPY_RT_E / ENTROPY_APT_E and seeding fails). It replaces
 * the prior deterministic CCNT-jitter seed callback.
 *
 * The Cortex-R5 has no ARM generic timer (the default __arm__ path in
 * wolfentropy.c reads CNTVCT via "mrrc p15,1,..,c14", which this core
 * does not implement), so we supply the high-resolution time source
 * via CUSTOM_ENTROPY_TIMEHIRES, backed by the PMU cycle counter
 * (fwtpm_entropy_timer in fwtpm_clock_zynqmp.c).
 *
 * By default (FWTPM_DCACHE) the entropy state buffer is cached, so the
 * MemUse noise draws on both cache hit/miss timing and the high-res
 * timer; in the FWTPM_DCACHE=0 fallback the buffer is non-cached and the
 * noise comes from the timer sampling DDR/bus/refresh jitter on this
 * shared-DDR SoC. Either way the SP800-90B health tests gate it
 * fail-closed: if jitter is insufficient, seeding (and FWTPM_Init) fails
 * rather than producing weak keys. The I-cache (on in both modes) is
 * what makes the SHA3-heavy seeding fast enough to be usable.
 *
 * ENTROPY_NUM_WORDS_BITS=13 -> 64 KiB state array (8192 x word64), half
 * the 128 KiB default footprint to fit the 1 MiB DDR carveout (both the
 * default and the FWTPM_ENABLE_PQC build). */
#define HAVE_HASHDRBG
#define WC_NO_RNG_SEED_FALLBACK
#define HAVE_ENTROPY_MEMUSE
/* Fail-closed: on MemUse failure (health-test trip or insufficient
 * entropy), return the error from wc_GenerateSeed instead of falling
 * back to /dev/urandom. Also compiles out the OS file-based fallback,
 * which has no implementation on this bare-metal R5. */
#define ENTROPY_MEMUSE_FORCE_FAILURE
#define ENTROPY_NUM_WORDS_BITS 13
/* MemUse indexes state as (d[i] << ENTROPY_BLOCK_SZ) + (j << shift), where
 * ENTROPY_BLOCK_SZ = ENTROPY_NUM_WORDS_BITS - 8 = 5 and j runs to
 * ENTROPY_NUM_UPDATES-1. With the default 18 updates the max index is
 * (255<<5) + (17<<1) = 8194, 3 words past the 8192-word (64 KiB) state
 * -- an out-of-bounds write. (The wolfSSL default BITS=14 has slack; 13
 * does not.) 16 updates keeps it in bounds: (255<<5) + (15<<1) = 8190. */
#define ENTROPY_NUM_UPDATES 16
#define CUSTOM_ENTROPY_TIMEHIRES() fwtpm_entropy_timer()
extern uint64_t fwtpm_entropy_timer(void);

/* ========================================================================= */
/* Hashes                                                                    */
/* ========================================================================= */
#define WOLFSSL_SHA256
#define WOLFSSL_SHA384
#define WOLFSSL_SHA512
#define WOLFSSL_SHA3        /* required by ML-KEM and ML-DSA below */
#define WOLFSSL_SHAKE128    /* ML-KEM uses SHAKE128 for matrix gen */
#define WOLFSSL_SHAKE256    /* ML-DSA uses SHAKE256 for sign/verify */
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
/* Post-Quantum (TCG TPM 2.0 v1.85)                                          */
/* ========================================================================= */
/* PQC is OFF by default. The OpenAMP rpmsg transport carries only one
 * ~496-byte frame per message (256 x 512-byte buffers, see rsc_table.c
 * and ZCU102_R5_BUF_SIZE), while every ML-KEM / ML-DSA payload exceeds
 * a single frame (e.g. ML-KEM-768 ciphertext 1088 B, ML-DSA-65 sig
 * 3309 B). Without a fragmentation/reassembly layer -- which this
 * transport does not yet implement -- advertising PQC would offer
 * algorithms the client cannot round-trip. Build with -DFWTPM_ENABLE_PQC
 * once that layer lands to compile the algorithms back in. */
#ifdef FWTPM_ENABLE_PQC

/* v1.85 dispatch paths (ML-DSA / ML-KEM TPM2_* commands and the
 * PT_ML_PARAMETER_SETS advertisement) live behind this; see the note
 * in the wolfTPM settings section above. */
#define WOLFTPM_V185

/* Some wolfCrypt PQC headers gate visibility behind this. */
#define WOLFSSL_EXPERIMENTAL_SETTINGS

/* ML-KEM: enable all NIST parameter sets so PT_ML_PARAMETER_SETS
 * advertises 512/768/1024 to the APU client. The R5 has 256 KiB
 * merged TCM in lock-step + ~1 MiB DDR carveout; the small-mem
 * variants trade CPU for stack to keep keygen / encap stack
 * under ~4 KiB on this core. */
#define WOLFSSL_HAVE_MLKEM
#define WOLFSSL_WC_MLKEM
#define WOLFSSL_MLKEM_MAKEKEY_SMALL_MEM
#define WOLFSSL_MLKEM_ENCAPSULATE_SMALL_MEM

/* ML-DSA: enable all NIST parameter sets (44/65/87). FIXED_ARRAY
 * keeps the key/sig context out of malloc -- this firmware does
 * not run a heap allocator suitable for PQC-sized buffers. */
#define HAVE_DILITHIUM
#define WOLFSSL_WC_DILITHIUM
#define WC_DILITHIUM_FIXED_ARRAY

#endif /* FWTPM_ENABLE_PQC */

/* ========================================================================= */
/* Debug                                                                     */
/* ========================================================================= */
/* DEBUG_WOLFSSL / DEBUG_WOLFTPM are intentionally OFF in V1: both pull in
 * newlib printf, which on this bare-metal R5 has no working file streams
 * and hangs at the first call. The remoteproc trace ring (outbyte ->
 * trace_buffer in bsp_stubs.c) is the only viable stdout. To temporarily
 * re-enable wolfSSL logging, also provide an outbyte-backed printf. */
#if 0
    #define DEBUG_WOLFSSL
    #define DEBUG_WOLFTPM
#endif

#ifdef __cplusplus
}
#endif

#endif /* FWTPM_ZCU102_R5_USER_SETTINGS_H */
