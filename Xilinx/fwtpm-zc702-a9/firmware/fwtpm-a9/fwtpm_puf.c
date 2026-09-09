/* fwtpm_puf.c
 *
 * wolfCrypt SRAM PUF integration for the Zynq-7000 fwTPM. Uses the power-on
 * state of a carve-out of uninitialized DDR as the PUF source. NOT on-chip
 * memory: the Zynq-7000 BootROM clears OCM before any user code runs.
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

/* PUF source address: an untouched DDR carve-out well above the firmware's own
 * link address, read before anything writes it.
 *
 * NOT OCM. The Zynq-7000 BootROM clears the whole on-chip memory before any
 * user code runs, so every OCM address reads back as zeros after a cold power
 * cycle and wc_PufReadSram() rejects it with PUF_READ_E. Measured on a ZC702:
 * all readable OCM regions were 0 percent ones after a genuine power cycle.
 *
 * DDR is left alone: ps7_init brings the controller up but never scrubs the
 * array, so the power-up state of the DRAM cells survives. Measured on a ZC702
 * across two independent cold power cycles: about 30 to 36 percent ones, and
 * only 1.7 to 1.8 percent of bits differed between boots - well inside the
 * BCH(127, k, t=10) budget of 10 flips per 127-bit codeword.
 *
 * Because this is a DRAM rather than an SRAM source, the readout is biased
 * toward zero; see WC_PUF_HW_MIN_PCT in user_settings.h. Must provide at least
 * WC_PUF_RAW_BYTES, and must not overlap the firmware, its heap or its stack. */
#ifndef FWTPM_PUF_SRC_ADDR
#define FWTPM_PUF_SRC_ADDR   0x20000000UL
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

/* Invalidate the D-cache over the PUF source before it is read.
 *
 * The DDR window is mapped Normal write-back cacheable (see common/mmu.c), so
 * unlike the old OCM carve-out a read here could be served from a cache line
 * rather than from the DRAM array. In the current boot flow the first touch is
 * always a miss, so this is a no-op in practice - but the PUF depends on
 * reading the physical power-on state, and that must not be left to depend on
 * nothing else having touched the region first.
 *
 * A plain invalidate (no clean) is correct: nothing writes this region, so
 * there are no dirty lines to discard. The Cortex-A9 cache line is 32 bytes. */
static void FwTPM_Puf_InvalidateSource(const void* addr, unsigned int len)
{
    uintptr_t p   = (uintptr_t)addr & ~(uintptr_t)31;
    uintptr_t end = (uintptr_t)addr + len;

    __asm__ volatile("dsb" ::: "memory");
    for (; p < end; p += 32) {
        /* DCIMVAC: invalidate data cache line by MVA to PoC */
        __asm__ volatile("mcr p15, 0, %0, c7, c6, 1" : : "r"(p) : "memory");
    }
    __asm__ volatile("dsb" ::: "memory");
    __asm__ volatile("isb" ::: "memory");
}

int FwTPM_Puf_InitEx(int forceEnroll, int* enrolled)
{
    wc_PufCtx ctx;
    unsigned char helper[WC_PUF_HELPER_BYTES];
    unsigned int  profileId = 0;
    int haveHelper;
    int didEnroll = 0;
    int ret;

    g_pufReady = 0;

    ret = wc_PufInit(&ctx);
    if (ret != 0) {
        return ret;
    }

    /* Read the raw DDR power-on state into the PUF context. */
    FwTPM_Puf_InvalidateSource((const void*)(uintptr_t)FWTPM_PUF_SRC_ADDR,
        (unsigned int)WC_PUF_RAW_BYTES);
    ret = wc_PufReadSram(&ctx, (const byte*)(uintptr_t)FWTPM_PUF_SRC_ADDR,
        WC_PUF_RAW_BYTES);
    if (ret != 0) {
        wc_PufZeroize(&ctx);
        return ret;
    }

    /* Reconstruct from persisted helper data if present and the profile matches;
     * otherwise enroll this boot and persist the new helper data.
     *
     * A failed reconstruct is deliberately NOT retried as an enrollment: that
     * would paper over a genuine PUF failure (wrong source region, a readout
     * taken too late, a failing part) by minting a fresh device key. Re-enroll
     * is a deliberate provisioning action, so it needs an explicit request:
     * forceEnroll ignores any stored helper data and enrolls this boot
     * (FwTPM_Puf_Init sets it from the FWTPM_PUF_FORCE_ENROLL build flag).
     * Use it when the PUF source or profile changes, then rebuild without it. */
    if (forceEnroll) {
        haveHelper = 0;
    }
    else {
        haveHelper = (fwtpm_puf_helper_load(helper,
            (unsigned int)WC_PUF_HELPER_BYTES, &profileId) == 0);
    }

    if (haveHelper) {
        ret = wc_PufReconstructEx(&ctx, helper, WC_PUF_HELPER_BYTES, profileId);
    }
    else {
        ret = wc_PufEnroll(&ctx);
        if (ret == 0) {
            ret = wc_PufGetHelperData(&ctx, helper, WC_PUF_HELPER_BYTES);
        }
        if (ret == 0) {
            /* The store must succeed before the new key is accepted: a forced
             * enrollment that fails to persist its helper data would leave
             * flash holding old or partial helper data that cannot reproduce
             * this key, silently orphaning the NV journal it protects. */
            ret = fwtpm_puf_helper_store(helper,
                (unsigned int)WC_PUF_HELPER_BYTES,
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

int FwTPM_Puf_Init(int* enrolled)
{
#ifdef FWTPM_PUF_FORCE_ENROLL
    return FwTPM_Puf_InitEx(1, enrolled);
#else
    return FwTPM_Puf_InitEx(0, enrolled);
#endif
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

    printf("SRAM PUF: source DDR 0x%08lX, profile t=%d cw=%d id=0x%08lX (%s)\r\n",
        (unsigned long)FWTPM_PUF_SRC_ADDR, (int)WC_PUF_BCH_T,
        (int)WC_PUF_NUM_CODEWORDS, (unsigned long)WC_PUF_PROFILE_ID,
        enrolled ? "enrolled" : "reconstructed");
    printf("SRAM PUF: device identity ");
    for (i = 0; i < 8 && i < (int)sizeof(g_pufId); i++) {
        printf("%02X", g_pufId[i]);
    }
    printf("...\r\n");
}
