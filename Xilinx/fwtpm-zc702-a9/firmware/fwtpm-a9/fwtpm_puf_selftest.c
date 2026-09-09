/* fwtpm_puf_selftest.c
 *
 * Synthetic wolfCrypt SRAM PUF regression for the Zynq-7000 A9, run on target.
 * Mirrors the wolfCrypt puf_test: enroll -> clean reconstruct (identity and
 * derived key must match) -> reconstruct at the BCH correction limit (t flips,
 * must still match) -> over-limit (t+1 flips, must fail or differ, never
 * silently reproduce the key) -> bad-argument checks -> zeroize. It injects
 * deterministic synthetic SRAM (WOLFSSL_PUF_TEST), so it proves the fuzzy-
 * extractor math on this ARMv7-A silicon with no dependence on the physical
 * OCM. Built only with -DFWTPM_PUF_SELFTEST (which also enables
 * WOLFSSL_PUF_TEST); the BCH profile is set by PUF_T / PUF_CW.
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

#include "user_settings.h"

#ifdef FWTPM_PUF_SELFTEST

#include <wolfssl/wolfcrypt/puf.h>
#include <wolfssl/wolfcrypt/error-crypt.h>

#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "fwtpm_puf.h"

/* Deterministic synthetic SRAM fill (a simple LCG so the pattern is stable
 * across the enroll/reconstruct passes but not all-zero). */
static void puf_fill_sram(unsigned char* buf, unsigned int sz)
{
    unsigned int i;
    unsigned int x = 0x1234567u;

    for (i = 0; i < sz; i++) {
        x = x * 1103515245u + 12345u;
        buf[i] = (unsigned char)(x >> 16);
    }
}

/* Flip the first nbits bits of the 128-bit (16-byte) codeword block. */
static void puf_flip_bits(unsigned char* buf, int block, int nbits)
{
    int i;
    int base = block * 16;

    for (i = 0; i < nbits; i++) {
        buf[base + (i >> 3)] ^= (unsigned char)(1u << (i & 7));
    }
}

static void step(const char* name, int ok)
{
    printf("  %-40s %s\r\n", name, ok ? "PASS" : "FAIL");
}

/* Local secret scrub via volatile writes (ForceZero is inline-only here). */
static void puf_scrub(void* p, unsigned int n)
{
    volatile unsigned char* v = (volatile unsigned char*)p;
    unsigned int i;
    for (i = 0; i < n; i++) {
        v[i] = 0;
    }
}

#ifndef FWTPM_NV_QSPI
/* Controllable RAM helper-data backend for the FwTPM_Puf_Init integration
 * steps below. These strong definitions override the weak no-op defaults in
 * fwtpm_puf.c (the QSPI NV backend provides its own, so this section is
 * excluded from -DFWTPM_NV_QSPI builds). */
static unsigned char st_helper[WC_PUF_HELPER_BYTES];
static unsigned int  st_profileId = 0;
static int st_haveHelper = 0;
static int st_failStore  = 0;
static int st_storeCount = 0;

int fwtpm_puf_helper_load(unsigned char* helper, unsigned int helperSz,
    unsigned int* profileId)
{
    if (helper == NULL || profileId == NULL || !st_haveHelper ||
            helperSz != sizeof(st_helper)) {
        return -1;
    }
    memcpy(helper, st_helper, helperSz);
    *profileId = st_profileId;
    return 0;
}

int fwtpm_puf_helper_store(const unsigned char* helper, unsigned int helperSz,
    unsigned int profileId)
{
    st_storeCount++;
    if (st_failStore) {
        return -1;
    }
    if (helper == NULL || helperSz != sizeof(st_helper)) {
        return -1;
    }
    memcpy(st_helper, helper, helperSz);
    st_profileId = profileId;
    st_haveHelper = 1;
    return 0;
}

/* Integration regression over FwTPM_Puf_Init/InitEx and helper persistence:
 * first boot enrolls and stores, a later boot reconstructs the same key,
 * forced enrollment overrides an existing helper and still reproduces the
 * key, and a failing helper store must fail the (forced) enrollment rather
 * than accept a key that cannot be reconstructed. Returns 0 on PASS. */
static int puf_init_integration(void)
{
    unsigned char keyA[WC_PUF_KEY_SZ], keyB[WC_PUF_KEY_SZ];
    unsigned int keySz;
    int enrolled, ret;

    st_haveHelper = 0;
    st_failStore  = 0;
    st_storeCount = 0;

    /* First boot: no stored helper -> enroll + store. */
    ret = FwTPM_Puf_InitEx(0, &enrolled);
    step("init: first boot enrolls and stores helper",
        ret == 0 && enrolled == 1 && st_haveHelper == 1);
    if (ret != 0 || enrolled != 1 || st_haveHelper != 1) return -1;
    keySz = sizeof(keyA);
    if (FwTPM_Puf_GetIntegrityKey(NULL, keyA, &keySz) != 0) return -1;

    /* Later boot: reconstruct from the stored helper, same key. */
    ret = FwTPM_Puf_InitEx(0, &enrolled);
    keySz = sizeof(keyB);
    if (ret == 0) ret = FwTPM_Puf_GetIntegrityKey(NULL, keyB, &keySz);
    step("init: reconstruct from stored helper, key matches",
        ret == 0 && enrolled == 0 &&
        memcmp(keyA, keyB, sizeof(keyA)) == 0);
    if (ret != 0 || enrolled != 0 ||
            memcmp(keyA, keyB, sizeof(keyA)) != 0) return -1;

    /* Forced enrollment: overrides the existing helper (store runs again)
     * and, from the same readout, reproduces the same key. */
    ret = FwTPM_Puf_InitEx(1, &enrolled);
    keySz = sizeof(keyB);
    if (ret == 0) ret = FwTPM_Puf_GetIntegrityKey(NULL, keyB, &keySz);
    step("init: forced enrollment overrides stored helper",
        ret == 0 && enrolled == 1 && st_storeCount == 2 &&
        memcmp(keyA, keyB, sizeof(keyA)) == 0);
    if (ret != 0 || enrolled != 1 || st_storeCount != 2 ||
            memcmp(keyA, keyB, sizeof(keyA)) != 0) return -1;

    /* Failing helper store: the enrollment must fail, not hand out a key
     * whose helper data was never durably persisted. */
    st_failStore = 1;
    ret = FwTPM_Puf_InitEx(1, &enrolled);
    st_failStore = 0;
    step("init: failed helper store fails the enrollment", ret != 0);
    if (ret == 0) return -1;

    puf_scrub(keyA, sizeof(keyA));
    puf_scrub(keyB, sizeof(keyB));
    st_haveHelper = 0;
    return 0;
}
#endif /* !FWTPM_NV_QSPI */

int FwTPM_Puf_SelfTest(void)
{
    wc_PufCtx ctx;
    unsigned char key1[WC_PUF_KEY_SZ];
    unsigned char key2[WC_PUF_KEY_SZ];
    unsigned char id1[WC_PUF_ID_SZ];
    unsigned char id2[WC_PUF_ID_SZ];
    static unsigned char testSram[WC_PUF_RAW_BYTES];
    static unsigned char noisySram[WC_PUF_RAW_BYTES];
    static unsigned char helperBuf[WC_PUF_HELPER_BYTES];
    const unsigned char info[] = "puf-test-context";
    int block, nblocks;
    int ret;

    printf("SRAM PUF synthetic self-test (t=%d cw=%d):\r\n",
        (int)WC_PUF_BCH_T, (int)WC_PUF_NUM_CODEWORDS);

    puf_fill_sram(testSram, (unsigned int)sizeof(testSram));
    nblocks = (WC_PUF_NUM_CODEWORDS < 3) ? WC_PUF_NUM_CODEWORDS : 3;

    /* Enroll. */
    if ((ret = wc_PufInit(&ctx)) != 0) goto fail;
    if ((ret = wc_PufSetTestData(&ctx, testSram, sizeof(testSram))) != 0)
        goto fail;
    if ((ret = wc_PufEnroll(&ctx)) != 0) goto fail;
    memcpy(helperBuf, ctx.helperData, WC_PUF_HELPER_BYTES);
    if ((ret = wc_PufGetIdentity(&ctx, id1, sizeof(id1))) != 0) goto fail;
    if ((ret = wc_PufDeriveKey(&ctx, info, sizeof(info), key1, sizeof(key1)))
            != 0) goto fail;
    step("enroll", 1);

    /* Clean reconstruct: identity and key must match. */
    if ((ret = wc_PufInit(&ctx)) != 0) goto fail;
    if ((ret = wc_PufSetTestData(&ctx, testSram, sizeof(testSram))) != 0)
        goto fail;
    if ((ret = wc_PufReadSram(&ctx, testSram, sizeof(testSram))) != 0) goto fail;
    if ((ret = wc_PufReconstruct(&ctx, helperBuf, WC_PUF_HELPER_BYTES)) != 0)
        goto fail;
    if ((ret = wc_PufGetIdentity(&ctx, id2, sizeof(id2))) != 0) goto fail;
    if (memcmp(id1, id2, WC_PUF_ID_SZ) != 0) { ret = -1; goto fail; }
    if ((ret = wc_PufDeriveKey(&ctx, info, sizeof(info), key2, sizeof(key2)))
            != 0) goto fail;
    if (memcmp(key1, key2, WC_PUF_KEY_SZ) != 0) { ret = -1; goto fail; }
    step("clean reconstruct (identity + key match)", 1);

    /* Reconstruct at the correction limit: t flips per block, still matches. */
    memcpy(noisySram, testSram, sizeof(testSram));
    for (block = 0; block < nblocks; block++) {
        puf_flip_bits(noisySram, block, WC_PUF_BCH_T);
    }
    if ((ret = wc_PufInit(&ctx)) != 0) goto fail;
    if ((ret = wc_PufSetTestData(&ctx, noisySram, sizeof(noisySram))) != 0)
        goto fail;
    if ((ret = wc_PufReadSram(&ctx, noisySram, sizeof(noisySram))) != 0)
        goto fail;
    if ((ret = wc_PufReconstruct(&ctx, helperBuf, WC_PUF_HELPER_BYTES)) != 0)
        goto fail;
    if ((ret = wc_PufGetIdentity(&ctx, id2, sizeof(id2))) != 0) goto fail;
    if (memcmp(id1, id2, WC_PUF_ID_SZ) != 0) { ret = -1; goto fail; }
    if ((ret = wc_PufDeriveKey(&ctx, info, sizeof(info), key2, sizeof(key2)))
            != 0) goto fail;
    if (memcmp(key1, key2, WC_PUF_KEY_SZ) != 0) { ret = -1; goto fail; }
    step("reconstruct at correction limit (t flips)", 1);

    /* Over the limit: t+1 flips in block 0 must fail or yield a DIFFERENT
     * identity - never silently reproduce the enrolled key. */
    memcpy(noisySram, testSram, sizeof(testSram));
    puf_flip_bits(noisySram, 0, WC_PUF_BCH_T + 1);
    if ((ret = wc_PufInit(&ctx)) != 0) goto fail;
    if ((ret = wc_PufSetTestData(&ctx, noisySram, sizeof(noisySram))) != 0)
        goto fail;
    if ((ret = wc_PufReadSram(&ctx, noisySram, sizeof(noisySram))) != 0)
        goto fail;
    ret = wc_PufReconstruct(&ctx, helperBuf, WC_PUF_HELPER_BYTES);
    if (ret == 0) {
        if (wc_PufGetIdentity(&ctx, id2, sizeof(id2)) == 0 &&
                memcmp(id1, id2, WC_PUF_ID_SZ) == 0) {
            ret = -1;   /* reproduced the enrolled key past the limit: fail */
            goto fail;
        }
    }
    else if (ret != WC_NO_ERR_TRACE(PUF_RECONSTRUCT_E)) {
        goto fail;      /* unexpected error */
    }
    step("over-limit reconstruct rejected (t+1 flips)", 1);

    /* Bad-argument checks. */
    if (wc_PufInit(NULL) != WC_NO_ERR_TRACE(BAD_FUNC_ARG)) { ret = -1; goto fail; }
    if (wc_PufEnroll(NULL) != WC_NO_ERR_TRACE(BAD_FUNC_ARG)) { ret = -1; goto fail; }
    step("bad-argument checks", 1);

    /* Zeroize; derive must then fail (not ready). */
    if ((ret = wc_PufZeroize(&ctx)) != 0) goto fail;
    if (wc_PufDeriveKey(&ctx, info, sizeof(info), key1, sizeof(key1))
            != WC_NO_ERR_TRACE(PUF_DERIVE_KEY_E)) { ret = -1; goto fail; }
    step("zeroize", 1);

    puf_scrub(key1, sizeof(key1));
    puf_scrub(key2, sizeof(key2));

#ifndef FWTPM_NV_QSPI
    if ((ret = puf_init_integration()) != 0) goto fail;
#endif

    printf("Result: 0 (PASS)\r\n");
    return 0;

fail:
    printf("Result: %d (FAIL)\r\n", ret);
    return ret;
}

#endif /* FWTPM_PUF_SELFTEST */
