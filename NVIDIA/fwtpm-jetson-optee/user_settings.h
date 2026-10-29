#ifndef FWTPM_TA_USER_SETTINGS_H
#define FWTPM_TA_USER_SETTINGS_H

/* Reached through both WOLFSSL_USER_SETTINGS and WOLFTPM_USER_SETTINGS, which
 * both include "user_settings.h", so this one file configures wolfCrypt and
 * wolfTPM together. That is also what keeps an installed
 * /usr/local/include/wolftpm/options.h out of the build: wolfTPM only falls
 * back to <wolftpm/options.h> when WOLFTPM_USER_SETTINGS is undefined. */

/* --- wolfTPM --- */

/* Build the fwTPM itself, not a client of one. */
#define WOLFTPM_FWTPM

/* TPM 2.0 Library Specification v1.85: the ML-DSA and ML-KEM commands. */
#define WOLFTPM_V185

/* TA_STACK_SIZE is small while the TA_DATA_SIZE heap is generous. */
#define WOLFTPM_SMALL_STACK

/* --- wolfCrypt --- */

/* wolfCrypt configuration for the wolfTPM fwTPM running as an OP-TEE trusted
 * application. A TA links libutee rather than a C library: no filesystem,
 * sockets, threads or wall clock.
 *
 * The same header is used for the native x86 dry run so the source list and
 * the algorithm set are validated off-board. FWTPM_TA_NATIVE selects the
 * host platform shims instead of the TEE ones. */

#define WOLFCRYPT_ONLY
#define SINGLE_THREADED
#define NO_FILESYSTEM
#define NO_STDIO_FILESYSTEM
#define NO_WRITEV
#define WOLFSSL_NO_SOCK
#define NO_WOLFSSL_DIR
#define NO_MAIN_DRIVER
#define NO_ASN_TIME
#define USE_WOLF_STRTOK

/* libutee has no strcasecmp, strncasecmp or strncat, so use wolfCrypt's own
 * implementations. ECC curve lookup by name is the only caller that matters
 * here. */
#define USE_WOLF_STRCASECMP
#define USE_WOLF_STRNCASECMP

/* Big-number math: single implementation covering RSA, ECC and keygen. */
#define WOLFSSL_SP_MATH_ALL

/* Optimized single-precision RSA and ECC. sp_arm64.c carries the aarch64
 * assembly for RSA 2048/3072/4096 and ECC P-256/P-384, which is most of the
 * private-key cost in a TPM; WOLFSSL_SP_MATH_ALL above stays as the fallback
 * for sizes it does not cover. Guarded on the architecture because the native
 * dry run builds these same sources for x86. */
#if defined(__aarch64__) && !defined(FWTPM_TA_NO_SP_ASM)
    #define WOLFSSL_HAVE_SP_RSA
    #define WOLFSSL_HAVE_SP_ECC
    #define WOLFSSL_SP_ARM64
    #define WOLFSSL_SP_ARM64_ASM
    #define WOLFSSL_SP_4096
    #define WOLFSSL_SP_384
#endif

/* ARMv8 crypto extensions for AES and SHA-2, the ML-KEM number-theoretic
 * transform and the Keccak permutation, in the inline-assembly form so the
 * OP-TEE build never sees a .S file. Build with FWTPM_TA_NO_ARMASM to leave
 * them out.
 *
 * The Cortex-A78AE reports aes, pmull, sha1 and sha2, so the compiled-in
 * feature flags this selects are correct for it. A core without those
 * extensions needs WOLFSSL_ARMASM_NO_HW_CRYPTO as well, because a trusted
 * application cannot detect them at run time.
 *
 * Neither FEAT_SHA3 nor FEAT_SHA512 is implemented on this core, so the
 * instruction paths needing them stay off. Keccak still gets assembly:
 * BlockSha3_base in armv8-sha3-asm_c.c sits outside the
 * WOLFSSL_ARMASM_CRYPTO_SHA3 guard and requires no extension. */
#if defined(__aarch64__) && !defined(FWTPM_TA_NO_ARMASM)
    #define WOLFSSL_ARMASM
    #define WOLFSSL_ARMASM_INLINE
#endif
/* Deliberately not WOLFSSL_AARCH64_BUILD: it defines HAVE_CPUID_AARCH64
 * (cpuid.h), whose Linux arm gets runtime feature detection from
 * <sys/auxv.h>. The TA toolchain predefines __linux__ but libutee has no
 * such header. sp_arm64.c needs only WOLFSSL_SP_ARM64_ASM, and a trusted
 * application has no use for CPU feature dispatch. */

/* The TPM generates its own primary keys. */
#define WOLFSSL_KEY_GEN

/* WOLFSSL_KEY_GEN otherwise pulls in wc_DerToPemEx, whose only use of
 * XSTRNCAT reaches a strncat that libutee does not provide. A TPM has no
 * reason to emit PEM. */
#define WOLFSSL_NO_DER_TO_PEM

/* TPM 2.0 RSA signing uses PKCS#1 v1.5 and PSS. The fwTPM also performs raw
 * RSA, which without WC_RSA_NO_PADDING returns TPM_RC_SCHEME: wolfTPM's
 * configure warns about this, and a trusted application build has no
 * configure step to warn it. */
#define WC_RSA_PSS
#define WC_RSA_NO_PADDING

/* Side-channel hardening. A TPM holds long-lived keys, so this is not
 * optional here even though the TA is the only thing in its address space. */
#define WC_RSA_BLINDING
#define TFM_TIMING_RESISTANT
#define ECC_TIMING_RESISTANT

/* Classical algorithms the fwTPM command set requires. */
#define HAVE_ECC
#define ECC_USER_CURVES
#define HAVE_ECC256
#define HAVE_ECC384
#define ECC_SHAMIR
#define HAVE_ECC_SIGN
#define HAVE_ECC_VERIFY
#define HAVE_ECC_DHE
#define HAVE_ECC_KEY_EXPORT
#define HAVE_ECC_KEY_IMPORT
#define WOLFSSL_CUSTOM_CURVES
#define WOLFSSL_PUBLIC_MP

#define WOLFSSL_AES_128
#define WOLFSSL_AES_256
#define WOLFSSL_AES_DIRECT
#define WOLFSSL_AES_CFB
#define HAVE_AES_DECRYPT
#define HAVE_AES_CBC

#define WOLFSSL_SHA224
#define WOLFSSL_SHA384
#define WOLFSSL_SHA512
#define WOLFSSL_SHA3
#define WOLFSSL_SHAKE128
#define WOLFSSL_SHAKE256
#define HAVE_HKDF
#define WOLFSSL_NO_SHAKE128_ASM

/* Post-quantum: ML-DSA (FIPS 204) and ML-KEM (FIPS 203), TPM 2.0 v1.85. */
#define WOLFSSL_HAVE_MLDSA
#define WOLFSSL_HAVE_MLKEM
#define WOLFSSL_MLDSA_NO_ASN1
#define WOLFSSL_EXPERIMENTAL_SETTINGS

/* OP-TEE's compiler.h defines a `fallthrough` macro. wolfCrypt's types.h
 * only notices it if compiler.h was included first, and otherwise settles on
 * __attribute__((fallthrough)), which then expands through OP-TEE's macro and
 * nests the attribute inside itself. Settle it here instead. */
#define FALL_THROUGH ; __attribute__((__fallthrough__))

/* Keep large temporaries off the small TA stack. */
#define WOLFSSL_SMALL_STACK

/* Not reachable from the TPM command set. */
#define NO_DH
#define NO_DSA
#define NO_DES3
#define NO_RC4
#define NO_MD4
#define NO_MD5
#define NO_PSK
#define NO_OLD_TLS
#define NO_PKCS12
#define NO_PWDBASED
#define WOLFSSL_NO_PKCS8

/* No allocator or RNG shims here. wolfSSL maps XMALLOC, XFREE and XREALLOC
 * onto TEE_Malloc, TEE_Free and TEE_Realloc, and seeds its DRBG from
 * TEE_GenerateRandom, whenever WOLFSSL_OPTEE is defined.
 *
 * The native dry run is a host build, so it gets neither. NO_FILESYSTEM above
 * is right for the trusted application but also closes the host seed path, so
 * the harness stands in for the TEE RNG the same way it stands in for NV. */
#ifdef FWTPM_TA_NATIVE
    #ifndef WOLFSSL_NO_PORT_DECLS
        int fwtpm_native_rand(unsigned char* out, unsigned int sz);
    #endif
    #define CUSTOM_RAND_GENERATE_BLOCK fwtpm_native_rand
    #define WC_NO_HASHDRBG
#endif

#endif /* FWTPM_TA_USER_SETTINGS_H */
