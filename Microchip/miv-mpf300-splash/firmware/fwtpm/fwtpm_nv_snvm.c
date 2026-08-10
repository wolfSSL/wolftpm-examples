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
#include <wolfssl/wolfcrypt/misc.h>   /* wc_ForceZero */
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
 * ciphertext to this chip; this app-layer key must be provisioned uniquely and
 * securely (e.g. fused or PUF-derived). There is deliberately no built-in
 * default: supply -DMIV_SNVM_USK='{0x..,...}' so images can never silently
 * share one compiled-in key. */
#ifndef MIV_SNVM_USK
#error "MIV_SNVM_NV_AUTH requires a unique -DMIV_SNVM_USK={...} (no shared default key)"
#endif
static const uint8_t g_nv_usk[MIV_SNVM_USK_LEN] = MIV_SNVM_USK;
#else
#define MIV_SNVM_NV_PAGE_DATA   MIV_SNVM_PAGE_DATA        /* 252 usable/page */
#endif

#define MIV_SNVM_NV_SIZE   (MIV_SNVM_NV_PAGES * MIV_SNVM_NV_PAGE_DATA)

/* Controller status 2 (rc -102) on a page read. In PLAINTEXT mode this is just a
 * never-written (blank) page and is benign. In AUTHENTICATED mode it is an
 * authentication failure (tamper, wrong key, or not-yet-provisioned) and must
 * NOT be mistaken for blank. Any other negative rc is a transport/controller
 * fault. MIV_SNVM_RC_IS_BLANK() decides whether a -102 may be treated as blank:
 * only in plaintext mode - AUTH mode fails closed instead. */
#define MIV_SNVM_RC_BLANK       (-102)
#define MIV_SNVM_IO_RETRIES     3
#ifdef MIV_SNVM_NV_AUTH
#define MIV_SNVM_RC_IS_BLANK(rc)   0
#else
#define MIV_SNVM_RC_IS_BLANK(rc)   ((rc) == MIV_SNVM_RC_BLANK)
#endif

/* NV-journal integrity key: the fwTPM core HMACs its journal with this key. What
 * that buys depends on where the key lives. In the DEFAULT plaintext build the
 * key is device-unique but stored in plaintext sNVM next to the data, so it is
 * NOT secret from an attacker with sNVM read access: it detects accidental
 * corruption and rejects a journal transplanted from another device, but does
 * NOT provide rollback/tamper resistance against such an attacker (who can read
 * the key and recompute the MAC). Genuine tamper/rollback protection needs
 * MIV_SNVM_NV_AUTH (key page stored as authenticated-ciphertext under the on-die
 * factory key) or a provisioned -DMIV_FWTPM_NV_KEY={...} from fuses/PUF. The key
 * is acquired fail-closed at init (see NvLoadIntegrityKey): if it cannot be
 * obtained the device refuses to run rather than silently dropping the MAC. */
#define FWTPM_NV_KEY_LEN    32U
#ifndef MIV_SNVM_KEY_PAGE
#define MIV_SNVM_KEY_PAGE   (MIV_SNVM_NV_BASE_PAGE + MIV_SNVM_NV_PAGES)
#endif
#ifdef MIV_FWTPM_NV_KEY
static const uint8_t g_nv_ikey[FWTPM_NV_KEY_LEN] = MIV_FWTPM_NV_KEY;
#endif

#if !defined(MIV_SNVM_NV_AUTH) && !defined(MIV_FWTPM_NV_KEY)
#warning "fwTPM NV: default plaintext sNVM with a device-derived (non-secret) integrity key - eval only; production should enable MIV_SNVM_NV_AUTH and/or provision -DMIV_FWTPM_NV_KEY"
#endif

static uint8_t g_nv[MIV_SNVM_NV_SIZE];
static int g_nv_initialized = 0;
/* Latched when a persistent write fails: further NV writes are refused so the
 * store is never left with the RAM shadow ahead of sNVM. */
static int g_nv_fault = 0;
/* Integrity key cached at init (fail-closed): NvGetIntegrityKey serves from here
 * so a transient sNVM fault can never silently disable journal authentication. */
static uint8_t g_nv_key[FWTPM_NV_KEY_LEN];
static int g_nv_key_ready = 0;

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

/* Scratch page image for SnvmCommit (single-threaded; kept off the stack). */
static uint8_t g_page_img[MIV_SNVM_NV_PAGE_DATA];

/* Program the sNVM page(s) covering shadow bytes [offset, offset+size-1] with
 * new content, committing each page into the RAM shadow ONLY after its sNVM
 * write succeeds, so the shadow never gets ahead of persistent storage. New
 * content is the bytes in buf (write) or 0xFF (erase, buf == NULL). On an
 * unrecoverable page-write failure the shadow is left equal to flash for the
 * failed page, a fault is latched, and the error is returned. */
static int SnvmCommit(uint32_t offset, const uint8_t* buf, uint32_t size)
{
    uint32_t pFirst = offset / MIV_SNVM_NV_PAGE_DATA;
    uint32_t pLast  = (offset + size - 1U) / MIV_SNVM_NV_PAGE_DATA;
    uint32_t p;
    uint32_t pageBase;
    uint32_t lo, hi;
    int rc = 0;
    int tries;

    for (p = pFirst; p <= pLast; p++) {
        pageBase = p * MIV_SNVM_NV_PAGE_DATA;
        /* Build the new page image: current shadow page with the changed span
         * overlaid, so partial-page writes preserve neighbouring bytes. */
        memcpy(g_page_img, g_nv + pageBase, MIV_SNVM_NV_PAGE_DATA);
        lo = (offset > pageBase) ? offset : pageBase;
        hi = (offset + size < pageBase + MIV_SNVM_NV_PAGE_DATA)
             ? (offset + size) : (pageBase + MIV_SNVM_NV_PAGE_DATA);
        if (buf != NULL) {
            memcpy(g_page_img + (lo - pageBase), buf + (lo - offset), hi - lo);
        }
        else {
            memset(g_page_img + (lo - pageBase), 0xFF, hi - lo);
        }

        for (tries = 0; tries < MIV_SNVM_IO_RETRIES; tries++) {
            rc = SnvmNvWritePage(MIV_SNVM_NV_BASE_PAGE + p, g_page_img);
            if (rc == 0) {
                break;
            }
        }
        if (rc != 0) {
            /* Could not persist this page after retries: leave the shadow equal
             * to flash (this page not committed) and latch a fault so no further
             * NV writes run on a half-persisted store. */
            g_nv_fault = 1;
            printf("fwTPM NV: sNVM page %u write failed rc=%d; NV writes "
                   "disabled\r\n", (unsigned)(MIV_SNVM_NV_BASE_PAGE + p), rc);
            return rc;
        }
        /* Commit: the shadow now matches flash for this page. */
        memcpy(g_nv + pageBase, g_page_img, MIV_SNVM_NV_PAGE_DATA);
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
    /* Reads come from the shadow, which is always consistent with flash, so
     * they remain valid even after a write fault is latched. */
    memcpy(buf, g_nv + offset, size);
    return 0;
}

static int NvSnvmWrite(void* ctx, word32 offset, const byte* buf, word32 size)
{
    (void)ctx;
    if (g_nv_fault) {
        return -1;
    }
    if (offset > MIV_SNVM_NV_SIZE || size > MIV_SNVM_NV_SIZE - offset) {
        return -1;
    }
    if (size == 0U) {
        return 0;
    }
    return SnvmCommit(offset, buf, size);
}

static int NvSnvmErase(void* ctx, word32 offset, word32 size)
{
    (void)ctx;
    if (g_nv_fault) {
        return -1;
    }
    if (offset > MIV_SNVM_NV_SIZE || size > MIV_SNVM_NV_SIZE - offset) {
        return -1;
    }
    if (size == 0U) {
        return 0;
    }
    return SnvmCommit(offset, NULL, size);
}

/* Acquire the NV-journal integrity key once, fail-closed, into g_nv_key. Returns
 * 0 on success (key cached), non-zero on an unrecoverable fault so the caller can
 * refuse to run. It never falls back to "no key" - that would silently disable
 * journal authentication. */
static int NvLoadIntegrityKey(void)
{
#ifdef MIV_FWTPM_NV_KEY
    memcpy(g_nv_key, g_nv_ikey, FWTPM_NV_KEY_LEN);
    g_nv_key_ready = 1;
    return 0;
#else
    uint8_t page[MIV_SNVM_NV_PAGE_DATA];
    unsigned char seed[FWTPM_NV_KEY_LEN];
    int rc = 0;
    int tries;
    int i;
    int blank = 1;

    /* Read the key page with retries; a transient sNVM fault must not be allowed
     * to disable authentication. */
    for (tries = 0; tries < MIV_SNVM_IO_RETRIES; tries++) {
        rc = SnvmNvReadPage(MIV_SNVM_KEY_PAGE, page);
        if (rc == 0 || MIV_SNVM_RC_IS_BLANK(rc)) {
            break;
        }
    }
    if (rc == 0) {
        /* An all-0xFF slot is an erased/never-written key. */
        for (i = 0; i < (int)FWTPM_NV_KEY_LEN; i++) {
            if (page[i] != 0xFFU) {
                blank = 0;
                break;
            }
        }
    }
    else if (MIV_SNVM_RC_IS_BLANK(rc)) {
        blank = 1;   /* plaintext: never-written key page */
    }
    else {
        /* Persistent fault, or (AUTH mode) an authentication failure on the key
         * page. Fail closed: do NOT run unauthenticated, and do NOT auto-format a
         * possibly-tampered AUTH key page. */
        wc_ForceZero(page, sizeof(page));
        return rc;
    }

    if (blank) {
        /* First boot (plaintext): derive a per-device key from the hardware NRBG
         * and persist it so the journal MAC is stable across power cycles. In
         * AUTH mode this is unreachable (a blank key page reads as -102, handled
         * above as a fault), so AUTH requires a provisioned key. */
        rc = miv_sysserv_nonce(seed);
        if (rc != 0) {
            wc_ForceZero(seed, sizeof(seed));
            wc_ForceZero(page, sizeof(page));
            return rc;
        }
        memset(page, 0xFF, sizeof(page));
        memcpy(page, seed, FWTPM_NV_KEY_LEN);
        wc_ForceZero(seed, sizeof(seed));
        for (tries = 0; tries < MIV_SNVM_IO_RETRIES; tries++) {
            rc = SnvmNvWritePage(MIV_SNVM_KEY_PAGE, page);
            if (rc == 0) {
                break;
            }
        }
        if (rc != 0) {
            wc_ForceZero(page, sizeof(page));
            return rc;
        }
    }

    memcpy(g_nv_key, page, FWTPM_NV_KEY_LEN);
    g_nv_key_ready = 1;
    wc_ForceZero(page, sizeof(page));
    return 0;
#endif
}

/* Supply the fwTPM NV-journal HMAC integrity key from the cache populated fail-
 * closed by NvLoadIntegrityKey() at init. Returns 0 with *keySz>0 to enable
 * journal authentication. */
static int NvGetIntegrityKey(void* ctx, byte* key, word32* keySz)
{
    (void)ctx;
    if (key == NULL || keySz == NULL || !g_nv_key_ready) {
        return -1;
    }
    memcpy(key, g_nv_key, FWTPM_NV_KEY_LEN);
    *keySz = FWTPM_NV_KEY_LEN;
    return 0;
}

/* Load the NV block from sNVM into the RAM shadow. In plaintext mode a never-
 * written page (blank read, -102) is left erased (0xFF) and the fwTPM formats it
 * on first use; a genuine read fault (after retries) fails init rather than
 * masquerading as blank. In AUTH mode -102 is an authentication failure, not a
 * blank page (MIV_SNVM_RC_IS_BLANK is 0), so it takes the fault path and init
 * fails closed instead of silently erasing/regenerating a tampered page. */
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
            if (rc == 0 || MIV_SNVM_RC_IS_BLANK(rc)) {
                break;
            }
        }
        if (MIV_SNVM_RC_IS_BLANK(rc)) {
            /* Never written: leave this page of the shadow erased (0xFF). */
            memset(g_nv + (p * MIV_SNVM_NV_PAGE_DATA), 0xFF,
                   MIV_SNVM_NV_PAGE_DATA);
        }
        else if (rc != 0) {
            /* A page that may hold data could not be read after retries (or, in
             * AUTH mode, failed authentication). Do NOT treat it as blank: that
             * would let the fwTPM regenerate hierarchy seeds and write 0xFF
             * through, destroying persisted NV. Fail init so main() refuses to
             * run. */
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
        /* Acquire the journal integrity key fail-closed: refuse to run rather
         * than serve the NV journal unauthenticated. */
        rc = NvLoadIntegrityKey();
        if (rc != 0) {
            printf("fwTPM NV: integrity key unavailable rc=%d; refusing to run "
                   "unauthenticated\r\n", rc);
            return rc;
        }
        g_nv_initialized = 1;
    }

    hal->read              = NvSnvmRead;
    hal->write             = NvSnvmWrite;
    hal->erase             = NvSnvmErase;
    hal->ctx               = NULL;
    hal->maxSize           = MIV_SNVM_NV_SIZE;
    /* Authenticate the NV journal with the key acquired above (see the
     * NV-journal integrity-key note near the top for exactly what that protects
     * in plaintext vs authenticated builds). */
    hal->get_integrity_key = NvGetIntegrityKey;
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
