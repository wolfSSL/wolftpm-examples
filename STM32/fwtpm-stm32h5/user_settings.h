/* user_settings.h
 *
 * Combined wolfSSL + wolfTPM settings for STM32 fwTPM port.
 * Included via WOLFSSL_USER_SETTINGS and WOLFTPM_USER_SETTINGS defines.
 *
 * Copyright (C) 2006-2025 wolfSSL Inc.
 *
 * This file is part of wolfTPM.
 */

#ifndef _USER_SETTINGS_H_
#define _USER_SETTINGS_H_

/* ========================================================================= */
/* wolfTPM settings                                                          */
/* ========================================================================= */
#define WOLFTPM_FWTPM
#define WOLFTPM_SMALL_STACK

/* ========================================================================= */
/* STM32 Platform                                                            */
/* ========================================================================= */
#define NO_FILESYSTEM
#define NO_MAIN_DRIVER
#define WOLFSSL_STM32_CUBEMX
#define USE_HAL_DRIVER
#define WOLFSSL_IGNORE_FILE_WARN

/* Default: No hardware crypto acceleration */
#define NO_STM32_HASH
#define NO_STM32_CRYPTO

#if defined(STM32H563xx)
    #define WOLFSSL_STM32H5
    #define STM32_HAL_V2
    #define HAL_CONSOLE_UART huart3
    /* Note: H563 has HASH peripheral, but requires HAL_HASH_MODULE_ENABLED.
     * Leave NO_STM32_HASH defined for now (use software SHA). */
    /* H563 has no CRYP peripheral */

    /* NV flash layout: last 128KB of flash */
    #if TZEN_ENABLED
        /* TrustZone: last 128KB of secure Bank 1 (before NSC region) */
        #define FWTPM_NV_FLASH_BASE         0x0C0DE000
    #else
        /* Non-TZ: last 128KB of 2MB flash */
        #define FWTPM_NV_FLASH_BASE         0x081E0000
    #endif
    #define FWTPM_NV_FLASH_SIZE         (128 * 1024)
    #define FWTPM_NV_FLASH_SECTOR_SIZE  (8 * 1024)
    #define FWTPM_NV_FLASH_PROGRAM_SIZE 16  /* 128-bit quadword */
#else
    #warning "No STM32 chip defined - please add platform config"
#endif

/* ========================================================================= */
/* wolfCrypt Math                                                            */
/* ========================================================================= */
#define WOLFSSL_SP
#define WOLFSSL_SP_MATH_ALL
#define WOLFSSL_SP_SMALL
#define SP_WORD_SIZE 32
#define WOLFSSL_SP_ARM_CORTEX_M_ASM
#define WOLFSSL_SP_ASM
#define WOLFSSL_HAVE_SP_RSA
#define WOLFSSL_HAVE_SP_ECC

/* ========================================================================= */
/* Algorithms                                                                */
/* ========================================================================= */

/* RSA */
#define WC_RSA_BLINDING
#define WC_RSA_PSS
#define WC_RSA_NO_PADDING
#define WOLFSSL_PUBLIC_MP
#define WOLFSSL_KEY_GEN

/* ECC */
#define HAVE_ECC
#define ECC_USER_CURVES
#undef  NO_ECC256
#define HAVE_ECC384
#define ECC_SHAMIR
#define ECC_TIMING_RESISTANT

/* AES */
#define HAVE_AESGCM
#define HAVE_AES_DECRYPT
#define WOLFSSL_AES_CFB
#define WOLFSSL_AES_DIRECT
#define HAVE_AES_KEYWRAP
#define GCM_SMALL

/* Hashing */
#define WOLFSSL_SHA384
#define WOLFSSL_SHA512
#define HAVE_SHA512

/* RNG: use STM32 hardware RNG directly via custom callback */
#define HAVE_HASHDRBG
#define WC_RESEED_INTERVAL (1000000)
#define NO_OLD_RNGNAME
#define CUSTOM_RAND_GENERATE_BLOCK custom_rand_gen_block
extern int custom_rand_gen_block(unsigned char* output, unsigned int sz);

/* ========================================================================= */
/* wolfSSL / Platform features                                               */
/* ========================================================================= */
#define WOLFCRYPT_ONLY
#define SINGLE_THREADED
#define WOLFSSL_SMALL_STACK
#define WOLFSSL_USER_IO
#define WOLFSSL_NO_SOCK
#define WOLFSSL_ASN_TEMPLATE
#define SIZEOF_LONG_LONG 8
#define WOLFSSL_GENERAL_ALIGNMENT 4

/* ========================================================================= */
/* Disabled features                                                         */
/* ========================================================================= */
#define NO_WRITEV
#define NO_DEV_RANDOM
#define NO_OLD_TLS
#define NO_DSA
#define NO_RC4
#define NO_MD4
#define NO_DES3
#define NO_PSK
#define NO_PWDBASED
#define NO_DH
/* Note: SHA-1 is needed by RSA OAEP (MGF1 default hash) */
#define NO_MD5
#define WOLFSSL_NO_SHAKE128
#define WOLFSSL_NO_SHAKE256
#define NO_ASN_TIME     /* no RTC configured yet */
#define NO_SESSION_CACHE
#define BENCH_EMBEDDED

#endif /* _USER_SETTINGS_H_ */
