/* fwtpm_puf.c
 *
 * wolfCrypt SRAM PUF integration for the Zynq-7000 fwTPM. Uses the power-on
 * state of a carve-out of the Cortex-A9 on-chip memory (OCM) as the PUF source.
 * On first boot it enrolls (generating helper data + a device identity); on
 * later boots it reconstructs the same stable bits from the persisted helper
 * data, correcting the SRAM's power-on noise. From the reconstructed stable
 * bits it HKDF-derives a 32-byte device-unique key which backs the fwTPM NV
 * journal's integrity HMAC (get_integrity_key). No root key is stored: it is
 * regenerated from silicon each boot.
 *
 * The PUF profile (BCH strength WC_PUF_BCH_T, codeword count
 * WC_PUF_NUM_CODEWORDS) is fixed at build time and its WC_PUF_PROFILE_ID is
 * persisted next to the helper data; a mismatch on reconstruct is rejected
 * rather than silently producing a wrong key.
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

#include <wolfssl/wolfcrypt/puf.h>
#include <wolfssl/wolfcrypt/error-crypt.h>

#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "zynq7000.h"
#include "fwtpm_puf.h"

/* OCM PUF source address. A carve-out near the top of the 256 KB OCM (mapped
 * high after the SLCR OCM_CFG remap) that the FSBL does not use, so its
 * power-on SRAM state survives to the first read here. Override for a board
 * whose FSBL clears this region. Must provide at least WC_PUF_RAW_BYTES. */
#ifndef FWTPM_PUF_OCM_ADDR
#define FWTPM_PUF_OCM_ADDR   (ZYNQ_OCM_HIGH_BASE + 0x3F000UL)
#endif

/* HKDF context/info string binding the derived key to this use. */
static const unsigned char PUF_INFO[] = "wolfTPM-fwTPM-NV-integrity";

/* Local secret scrub via volatile writes. ForceZero is inline-compiled in this
 * build (no linkable symbol from misc.c), so use our own to zero key material. */
static void puf_scrub(void* p, unsigned int n)
{
    volatile unsigned char* v = (volatile unsigned char*)p;
    unsigned int i;
    for (i = 0; i < n; i++) {
        v[i] = 0;
    }
}

/* Module state retained after init (the wc_PufCtx, which holds the raw SRAM, is
 * zeroized once the key is derived). */
static unsigned char g_pufKey[WC_PUF_KEY_SZ];
static unsigned char g_pufId[WC_PUF_ID_SZ];
static int           g_pufReady = 0;

/* Default (weak) persistence: no store. load reports "not found" so every boot
 * enrolls; the QSPI NV backend overrides both to persist across power cycles. */
__attribute__((weak))
int fwtpm_puf_helper_load(unsigned char* helper, unsigned int helperSz,
    unsigned int* profileId)
{
    (void)helper;
    (void)helperSz;
    (void)profileId;
    return -1;   /* not found */
}

__attribute__((weak))
int fwtpm_puf_helper_store(const unsigned char* helper, unsigned int helperSz,
    unsigned int profileId)
{
    (void)helper;
    (void)helperSz;
    (void)profileId;
    return 0;    /* no-op (volatile) */
}

int FwTPM_Puf_Init(int* enrolled)
{
    wc_PufCtx ctx;
    unsigned char helper[WC_PUF_HELPER_BYTES];
    unsigned int  profileId = 0;
    int didEnroll = 0;
    int ret;

    g_pufReady = 0;

    ret = wc_PufInit(&ctx);
    if (ret != 0) {
        return ret;
    }

    /* Read the raw OCM power-on state into the PUF context. */
    ret = wc_PufReadSram(&ctx, (const byte*)(uintptr_t)FWTPM_PUF_OCM_ADDR,
        WC_PUF_RAW_BYTES);
    if (ret != 0) {
        wc_PufZeroize(&ctx);
        return ret;
    }

    /* Reconstruct from persisted helper data if present and the profile matches;
     * otherwise enroll this boot and persist the new helper data. */
    if (fwtpm_puf_helper_load(helper, (unsigned int)WC_PUF_HELPER_BYTES,
            &profileId) == 0) {
        ret = wc_PufReconstructEx(&ctx, helper, WC_PUF_HELPER_BYTES, profileId);
    }
    else {
        ret = wc_PufEnroll(&ctx);
        if (ret == 0) {
            ret = wc_PufGetHelperData(&ctx, helper, WC_PUF_HELPER_BYTES);
        }
        if (ret == 0) {
            (void)fwtpm_puf_helper_store(helper, (unsigned int)WC_PUF_HELPER_BYTES,
                (unsigned int)WC_PUF_PROFILE_ID);
        }
        didEnroll = 1;
    }
    if (ret != 0) {
        wc_PufZeroize(&ctx);
        return ret;
    }

    /* Capture the device identity and derive the integrity key. */
    ret = wc_PufGetIdentity(&ctx, g_pufId, sizeof(g_pufId));
    if (ret == 0) {
        ret = wc_PufDeriveKey(&ctx, PUF_INFO, (word32)sizeof(PUF_INFO),
            g_pufKey, sizeof(g_pufKey));
    }

    /* Scrub the context (raw SRAM, stable bits) regardless of outcome. */
    wc_PufZeroize(&ctx);
    puf_scrub(helper, sizeof(helper));

    if (ret != 0) {
        return ret;
    }

    g_pufReady = 1;
    if (enrolled != NULL) {
        *enrolled = didEnroll;
    }
    return 0;
}

int FwTPM_Puf_GetIntegrityKey(void* halCtx, unsigned char* key,
    unsigned int* keySz)
{
    (void)halCtx;
    if (!g_pufReady || key == NULL || keySz == NULL) {
        return -1;
    }
    memcpy(key, g_pufKey, sizeof(g_pufKey));
    *keySz = (unsigned int)sizeof(g_pufKey);
    return 0;
}

void FwTPM_Puf_PrintInfo(int enrolled)
{
    int i;

    printf("SRAM PUF: source OCM 0x%08lX, profile t=%d cw=%d id=0x%08lX (%s)\r\n",
        (unsigned long)FWTPM_PUF_OCM_ADDR, (int)WC_PUF_BCH_T,
        (int)WC_PUF_NUM_CODEWORDS, (unsigned long)WC_PUF_PROFILE_ID,
        enrolled ? "enrolled" : "reconstructed");
    printf("SRAM PUF: device identity ");
    for (i = 0; i < 8 && i < (int)sizeof(g_pufId); i++) {
        printf("%02X", g_pufId[i]);
    }
    printf("...\r\n");
}
