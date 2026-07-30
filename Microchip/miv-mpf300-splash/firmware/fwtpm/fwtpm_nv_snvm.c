/* fwtpm_nv_snvm.c
 *
 * Persistent NV storage HAL for the Mi-V fwTPM, backed by the PolarFire on-die
 * secure NVM (sNVM) via the System Controller services (CoreSysServices_PF), so
 * TPM state survives a power cycle. A block of sNVM pages (MIV_SNVM_NV_BASE_PAGE,
 * count MIV_SNVM_NV_PAGES), placed high in the 221-page array clear of pages the
 * FPGA design uses, is shadowed in RAM: reads come from the shadow, writes update
 * it and program only the touched page(s) (write-through). sNVM is limited-
 * endurance flash - fine for a demo; heavy NV churn wants an external flash.
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

#ifdef WOLFTPM_FWTPM

#include <wolftpm/fwtpm/fwtpm.h>
#include <wolftpm/fwtpm/fwtpm_nv.h>
#include <string.h>
#include <stdio.h>

#include "miv_sysserv.h"

/* NV block placement in sNVM. Base is high in the 221-page array to avoid the
 * low pages the design's RAM-init clients use. */
#ifndef MIV_SNVM_NV_BASE_PAGE
#define MIV_SNVM_NV_BASE_PAGE   128U
#endif
#ifndef MIV_SNVM_NV_PAGES
#ifdef MIV_FWTPM_PQC
#define MIV_SNVM_NV_PAGES       33U   /* ~8 KB - trimmed so the PQC image fits */
#else
#define MIV_SNVM_NV_PAGES       65U   /* ~16 KB */
#endif
#endif

/* NV backend mode. Default: plaintext sNVM (validated on this design; needs no
 * device security provisioning). Define MIV_SNVM_NV_AUTH to use authenticated-
 * ciphertext pages (each encrypted and integrity-checked with the on-die
 * factory key plus a user key). Authenticated mode ADDITIONALLY requires the
 * FPGA design's security policy to permit authenticated sNVM writes (set in the
 * Libero Security Policy Manager); without that provisioning the System
 * Controller rejects the write with SNVM_WRITE_NOT_PERMITTED (status 4). */
#ifdef MIV_SNVM_NV_AUTH
#define MIV_SNVM_NV_PAGE_DATA   MIV_SNVM_PAGE_DATA_AUTH   /* 236 usable/page */
/* User secret key for authenticated sNVM. The device factory key already binds
 * ciphertext to this chip; provision this app-layer key uniquely and securely
 * in production (e.g. fused or PUF-derived). Override the build-time default
 * with -DMIV_SNVM_USK='{0x..,...}' so images do not silently share one key. */
#ifndef MIV_SNVM_USK
#define MIV_SNVM_USK { 0x77, 0x6F, 0x6C, 0x66, 0x54, 0x50, 0x4D, 0x4E, \
                       0x56, 0x6B, 0x65, 0x79 }
#endif
static const uint8_t g_nv_usk[MIV_SNVM_USK_LEN] = MIV_SNVM_USK;
#else
#define MIV_SNVM_NV_PAGE_DATA   MIV_SNVM_PAGE_DATA        /* 252 usable/page */
#endif

#define MIV_SNVM_NV_SIZE   (MIV_SNVM_NV_PAGES * MIV_SNVM_NV_PAGE_DATA)

/* A never-written plaintext sNVM page reads back as SNVM_READ_AUTHENTICATION_
 * FAILURE (status 2 -> rc -102). Anything else negative is a transport/controller
 * fault that must NOT be mistaken for a blank page. */
#define MIV_SNVM_RC_BLANK       (-102)
#define MIV_SNVM_IO_RETRIES     3

static uint8_t g_nv[MIV_SNVM_NV_SIZE];
static int g_nv_initialized = 0;

/* Mode-selecting sNVM page write/read for the NV block. */
static int SnvmNvWritePage(uint32_t page, const uint8_t* d)
{
#ifdef MIV_SNVM_NV_AUTH
    return miv_snvm_write_page_auth((uint8_t)page, d, g_nv_usk);
#else
    return miv_snvm_write_page((uint8_t)page, d);
#endif
}

static int SnvmNvReadPage(uint32_t page, uint8_t* d)
{
#ifdef MIV_SNVM_NV_AUTH
    return miv_snvm_read_page_auth((uint8_t)page, d, g_nv_usk);
#else
    return miv_snvm_read_page((uint8_t)page, d);
#endif
}

/* Program the sNVM pages spanning shadow bytes [first, last] from g_nv.
 * Returns 0 on success. */
static int SnvmFlushRange(uint32_t first, uint32_t last)
{
    uint32_t p;
    uint32_t pFirst = first / MIV_SNVM_NV_PAGE_DATA;
    uint32_t pLast  = last  / MIV_SNVM_NV_PAGE_DATA;
    int rc = 0;
    int tries;

    for (p = pFirst; p <= pLast; p++) {
        for (tries = 0; tries < MIV_SNVM_IO_RETRIES; tries++) {
            rc = SnvmNvWritePage(MIV_SNVM_NV_BASE_PAGE + p,
                                 g_nv + (p * MIV_SNVM_NV_PAGE_DATA));
            if (rc == 0) {
                break;
            }
        }
        if (rc != 0) {
            /* Page could not be programmed after retries; the RAM shadow is
             * already ahead of sNVM, so report how far the write got - a torn
             * multi-page update is now diagnosable rather than silent. */
            printf("fwTPM NV: sNVM page %u write failed rc=%d (shadow ahead of "
                   "flash)\r\n", (unsigned)(MIV_SNVM_NV_BASE_PAGE + p), rc);
            return rc;
        }
    }
    return 0;
}

/* Signatures use wolfCrypt's word32/byte to match FWTPM_NV_HAL exactly (word32
 * is unsigned int, distinct from this target's uint32_t = unsigned long). */
static int NvSnvmRead(void* ctx, word32 offset, byte* buf, word32 size)
{
    (void)ctx;
    if (offset > MIV_SNVM_NV_SIZE || size > MIV_SNVM_NV_SIZE - offset) {
        return -1;
    }
    memcpy(buf, g_nv + offset, size);
    return 0;
}

static int NvSnvmWrite(void* ctx, word32 offset, const byte* buf, word32 size)
{
    (void)ctx;
    if (offset > MIV_SNVM_NV_SIZE || size > MIV_SNVM_NV_SIZE - offset) {
        return -1;
    }
    if (size == 0U) {
        return 0;
    }
    memcpy(g_nv + offset, buf, size);
    return SnvmFlushRange(offset, offset + size - 1U);
}

static int NvSnvmErase(void* ctx, word32 offset, word32 size)
{
    (void)ctx;
    if (offset > MIV_SNVM_NV_SIZE || size > MIV_SNVM_NV_SIZE - offset) {
        return -1;
    }
    if (size == 0U) {
        return 0;
    }
    memset(g_nv + offset, 0xFF, size);
    return SnvmFlushRange(offset, offset + size - 1U);
}

/* Load the NV block from sNVM into the RAM shadow. A never-written page (blank
 * read) is left erased (0xFF) and the fwTPM formats it on first use; a genuine
 * read fault (after retries) fails init rather than masquerading as blank. */
static int SnvmLoadShadow(void)
{
    uint32_t p;
    int rc = 0;
    int tries;

    memset(g_nv, 0xFF, sizeof(g_nv));
    for (p = 0U; p < MIV_SNVM_NV_PAGES; p++) {
        for (tries = 0; tries < MIV_SNVM_IO_RETRIES; tries++) {
            rc = SnvmNvReadPage(MIV_SNVM_NV_BASE_PAGE + p,
                                g_nv + (p * MIV_SNVM_NV_PAGE_DATA));
            if (rc == 0 || rc == MIV_SNVM_RC_BLANK) {
                break;
            }
        }
        if (rc == MIV_SNVM_RC_BLANK) {
            /* Never written: leave this page of the shadow erased (0xFF). */
            memset(g_nv + (p * MIV_SNVM_NV_PAGE_DATA), 0xFF,
                   MIV_SNVM_NV_PAGE_DATA);
        }
        else if (rc != 0) {
            /* A page that may hold data could not be read after retries. Do NOT
             * treat it as blank: that would let the fwTPM regenerate hierarchy
             * seeds and write 0xFF through, permanently destroying persisted NV.
             * Fail init so main() refuses to run. */
            return rc;
        }
    }
    return 0;
}

int FWTPM_NV_SNVM_Init(FWTPM_NV_HAL* hal)
{
    if (hal == NULL) {
        return -1;
    }

    if (g_nv_initialized == 0) {
        int rc = SnvmLoadShadow();
        if (rc != 0) {
            printf("fwTPM NV: sNVM read fault rc=%d; refusing to run to avoid "
                   "overwriting persisted NV\r\n", rc);
            return rc;
        }
        g_nv_initialized = 1;
    }

    hal->read              = NvSnvmRead;
    hal->write             = NvSnvmWrite;
    hal->erase             = NvSnvmErase;
    hal->ctx               = NULL;
    hal->maxSize           = MIV_SNVM_NV_SIZE;
    hal->get_integrity_key = NULL;
    hal->writeAlign        = 0;
    hal->appendOnly        = 0;

    printf("fwTPM NV: sNVM-backed %s (%u bytes, persistent, pages %u-%u)\r\n",
#ifdef MIV_SNVM_NV_AUTH
           "authenticated-ciphertext",
#else
           "plaintext",
#endif
           (unsigned)MIV_SNVM_NV_SIZE, (unsigned)MIV_SNVM_NV_BASE_PAGE,
           (unsigned)(MIV_SNVM_NV_BASE_PAGE + MIV_SNVM_NV_PAGES - 1U));
    return 0;
}

#endif /* WOLFTPM_FWTPM */
