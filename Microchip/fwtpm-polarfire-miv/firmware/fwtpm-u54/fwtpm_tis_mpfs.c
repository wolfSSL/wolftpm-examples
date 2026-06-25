/* fwtpm_tis_mpfs.c
 *
 * PolarFire SoC shared-memory TIS HAL for fwTPM server (hart 4, M-mode).
 * Implements FWTPM_TIS_HAL callbacks using L2 LIM shared memory and
 * polling for request/response notification; CLINT IPI doorbell is
 * available but currently disabled (see TisMpfsSignalResponse).
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

#include "user_settings.h"

#ifdef WOLFTPM_FWTPM

#include <wolftpm/fwtpm/fwtpm.h>
#include <wolftpm/fwtpm/fwtpm_tis.h>

#include "fwtpm_tis_mpfs.h"
#include "mpfs_hal.h"

#include <stddef.h>   /* offsetof */
#include <string.h>
#include <stdio.h>

/* startup.S writes the breadcrumb fields at absolute offsets (its
 * FWTPM_OFF_* literals); this static assert fails the build if the
 * struct layout drifts out of sync. */
typedef char fwtpm_mbox_layout_check[
    (offsetof(FWTPM_MPFS_MAILBOX, progress)    == 0x14 &&
     offsetof(FWTPM_MPFS_MAILBOX, echo_nonce)  == 0x1C &&
     offsetof(FWTPM_MPFS_MAILBOX, trap_marker) == 0x20 &&
     offsetof(FWTPM_MPFS_MAILBOX, xport_id)    == 0x24 &&
     offsetof(FWTPM_MPFS_MAILBOX, trap_mcause) == 0x28 &&
     offsetof(FWTPM_MPFS_MAILBOX, trap_mepc)   == 0x30 &&
     offsetof(FWTPM_MPFS_MAILBOX, trap_mtval)  == 0x38 &&
     sizeof(FWTPM_MPFS_MAILBOX)                == 0x40) ? 1 : -1];

/* Fail the build if the mailbox + console ring + TIS regs overrun the
 * 16 KB LIM window (FWTPM_TIS_REGS grows with FWTPM_MAX_COMMAND_SIZE). */
typedef char fwtpm_tis_region_fits[
    (FWTPM_TIS_REGS_OFFSET + sizeof(FWTPM_TIS_REGS) <= FWTPM_TIS_SHM_SIZE)
        ? 1 : -1];

/* Internal context */
typedef struct {
    FWTPM_TIS_REGS      *regs;
    FWTPM_MPFS_MAILBOX   *mbox;
    FWTPM_CONSOLE_RING   *console;
} FWTPM_TIS_MPFS_CTX;

static FWTPM_TIS_MPFS_CTX g_mpfs_ctx;
static uint32_t g_pollcnt = 0;   /* DDR_WCB buffer-cycle + server liveness */

/* -------------------------------------------------------------------- */
/* HAL callbacks                                                         */
/* -------------------------------------------------------------------- */

static int TisMpfsInit(void *ctx, FWTPM_TIS_REGS **regs)
{
    FWTPM_TIS_MPFS_CTX *mc = (FWTPM_TIS_MPFS_CTX *)ctx;
    uint8_t *base = (uint8_t *)(uintptr_t)FWTPM_TIS_SHM_BASE;

    /* Direct pointers into the shared region -- no mmap in M-mode. Base is
     * FWTPM_TIS_SHM_BASE (L2 LIM, or the DDR alias under DDR_NONCACHED). */
    mc->mbox    = (FWTPM_MPFS_MAILBOX *)(base + FWTPM_MBOX_OFFSET);
    mc->console = (FWTPM_CONSOLE_RING *)(base + FWTPM_CONSOLE_OFFSET);
    mc->regs    = (FWTPM_TIS_REGS *)   (base + FWTPM_TIS_REGS_OFFSET);

    /* Clear only the TIS regs/FIFOs; the mailbox and console ring hold
     * state from main() (progress, banner) that must be preserved. */
    memset((void *)mc->regs, 0, sizeof(*mc->regs));

    /* Init mailbox signaling fields (re-asserting magic/version is
     * harmless and self-documenting). */
    mc->mbox->magic = FWTPM_MBOX_MAGIC;
    mc->mbox->version = FWTPM_MBOX_VERSION;
    mc->mbox->cmd_ready = 0;
    mc->mbox->rsp_ready = 0;
    mc->mbox->echo_nonce = 0;
    mc->mbox->xport_id = FWTPM_XPORT_ID;
    mc->mbox->server_alive = 1;

    mpfs_fence_w();

    printf("fwTPM TIS: Shared memory at 0x%lx (%lu bytes)\n",
           (unsigned long)FWTPM_TIS_SHM_BASE,
           (unsigned long)FWTPM_TIS_SHM_SIZE);
    printf("fwTPM TIS: Console ring at offset 0x%lx (%u bytes)\n",
           (unsigned long)FWTPM_CONSOLE_OFFSET,
           (unsigned)FWTPM_CONSOLE_RING_SIZE);
    printf("fwTPM TIS: transport id %u, poll-mode (no MSIP doorbell)\n",
           (unsigned)FWTPM_XPORT_ID);

    *regs = mc->regs;
    return 0;
}

/* LIM_CACHED only: the U54 L1 data cache is write-back with NO cache-
 * maintenance instruction (the core predates Zicbom), so on the cacheable LIM
 * mailbox hart-4 writes do not reach the shared L2 and hart-4 reads do not see
 * Linux's writes. Reading a block several times the L1d size cycles it (random
 * replacement), evicting the mailbox lines. NOTE: this is unreliable on a
 * write-back cache and is superseded by a genuinely non-cached transport
 * (FWTPM_XPORT=DDR_WCB / DDR_NONCACHED) when the platform provides one. */
#if defined(FWTPM_XPORT_LIM_CACHED)
#define FWTPM_L1D_EVICT_BYTES  (256u * 1024u)
static void TisMpfsEvictL1D(void)
{
    volatile const uint8_t *p =
        (volatile const uint8_t *)(uintptr_t)FWTPM_TIS_SHM_BASE;
    volatile uint8_t sink = 0;
    uint32_t i;

    for (i = 0; i < FWTPM_L1D_EVICT_BYTES; i += 64)
        sink += p[i];
    (void)sink;
}
#endif

static int TisMpfsWaitRequest(void *ctx)
{
    FWTPM_TIS_MPFS_CTX *mc = (FWTPM_TIS_MPFS_CTX *)ctx;
#if defined(FWTPM_XPORT_IHC)
    /* IHC backend not wired yet (gated on a Libero bitstream with the IHC
     * IP). Returns -1 (EINTR-continue); ServerLoop has no backoff, so this
     * unbuilt stub would busy-spin -- harmless since hart 4 is dedicated.
     * The real backend will block on an IHC message here. */
    (void)mc;
    return -1;
#else
    uint32_t nonce;

    /* Poll cmd_ready (a client-supplied nonzero nonce). On LIM_CACHED, evict
     * the L1d each iteration so Linux's write is observed; on a non-cached
     * transport (DDR_WCB/DDR_NONCACHED) accesses bypass the cache and no evict
     * is needed. */
    for (;;) {
#if defined(FWTPM_XPORT_LIM_CACHED)
        TisMpfsEvictL1D();
#endif
#if defined(FWTPM_XPORT_DDR_WCB)
        /* DDR_WCB: a write to the non-cached window cycles the write-combine
         * buffer which, with the following fence, forces the next cmd_ready
         * read to be served from DDR rather than a stale/buffered value (a
         * bare fence alone did not). g_pollcnt doubles as a liveness counter. */
        mc->mbox->rc = ++g_pollcnt;
        mpfs_fence_rw();
#else
        /* Other transports: no write-combine buffer to cycle, so avoid the
         * per-iteration mailbox write and full fence (needless bus traffic);
         * a read fence is enough to order the cmd_ready poll. Keep the
         * liveness counter ticking. */
        ++g_pollcnt;
        mpfs_fence_r();
#endif
        if (mc->mbox->cmd_ready != 0)
            break;
    }
    nonce = mc->mbox->cmd_ready;

    /* Echo the nonce so the client can confirm hart 4 observed THIS write,
     * then acknowledge by clearing cmd_ready (write-through reaches L2). */
    mc->mbox->echo_nonce = nonce;
    mc->mbox->cmd_ready = 0;
    mpfs_fence_rw();
#if defined(FWTPM_XPORT_LIM_CACHED)
    /* Evict again so the ServerLoop's reads of the client-written TIS reg
     * fields (reg_addr/reg_is_write/reg_len/reg_data) re-fetch from L2
     * instead of a stale L1d line left over from a prior access. */
    TisMpfsEvictL1D();
#endif
    return 0;
#endif
}

static int TisMpfsSignalResponse(void *ctx)
{
    FWTPM_TIS_MPFS_CTX *mc = (FWTPM_TIS_MPFS_CTX *)ctx;

    /* Flush the response payload (reg_data/sts/rsp_buf written by the core's
     * TisHandleRegAccess) out of the write-combine buffer BEFORE raising
     * rsp_ready, then flush rsp_ready itself, so the client never observes
     * the ready flag ahead of the data it gates (DDR_WCB ordering). */
    mpfs_fence_rw();
    mc->mbox->rsp_ready = 1;
    mpfs_fence_rw();

    /* Optional: IPI to wake Linux hart for low-latency notification.
     * Uncomment when a Linux kernel driver handles MSIP[1]. */
    /* CLINT_MSIP(1) = 1; */

    return 0;
}

static void TisMpfsCleanup(void *ctx)
{
    FWTPM_TIS_MPFS_CTX *mc = (FWTPM_TIS_MPFS_CTX *)ctx;

    if (mc->mbox != NULL) {
        mc->mbox->server_alive = 0;
        mpfs_fence_w();
    }
}

/* -------------------------------------------------------------------- */
/* Public API                                                            */
/* -------------------------------------------------------------------- */

void FWTPM_TIS_MPFS_SetHAL(FWTPM_CTX *ctx)
{
    if (ctx == NULL)
        return;

    memset(&g_mpfs_ctx, 0, sizeof(g_mpfs_ctx));

    ctx->tisHal.init            = TisMpfsInit;
    ctx->tisHal.wait_request    = TisMpfsWaitRequest;
    ctx->tisHal.signal_response = TisMpfsSignalResponse;
    ctx->tisHal.cleanup         = TisMpfsCleanup;
    ctx->tisHal.ctx             = &g_mpfs_ctx;
}

#endif /* WOLFTPM_FWTPM */
