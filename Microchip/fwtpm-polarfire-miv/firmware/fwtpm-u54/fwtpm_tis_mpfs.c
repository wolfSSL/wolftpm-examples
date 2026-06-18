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

/* The boot/trap breadcrumb fields are written from startup.S using
 * absolute literal offsets (the FWTPM_OFF_* .equ values there). Guard the
 * C struct layout against drift so a struct change cannot silently desync
 * the assembler -- this fails the build instead. Keep in sync with
 * startup.S and the linux-client field offsets. */
typedef char fwtpm_mbox_layout_check[
    (offsetof(FWTPM_MPFS_MAILBOX, progress)    == 0x14 &&
     offsetof(FWTPM_MPFS_MAILBOX, echo_nonce)  == 0x1C &&
     offsetof(FWTPM_MPFS_MAILBOX, trap_marker) == 0x20 &&
     offsetof(FWTPM_MPFS_MAILBOX, xport_id)    == 0x24 &&
     offsetof(FWTPM_MPFS_MAILBOX, trap_mcause) == 0x28 &&
     offsetof(FWTPM_MPFS_MAILBOX, trap_mepc)   == 0x30 &&
     offsetof(FWTPM_MPFS_MAILBOX, trap_mtval)  == 0x38 &&
     sizeof(FWTPM_MPFS_MAILBOX)                == 0x40) ? 1 : -1];

/* The mailbox + console ring + TIS register block must fit inside the
 * fixed 16 KB LIM window. sizeof(FWTPM_TIS_REGS) comes from the wolfTPM
 * header and grows with FWTPM_MAX_COMMAND_SIZE (e.g. enabling ML-DSA);
 * fail the build rather than silently overrun LIM at runtime. */
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

/* -------------------------------------------------------------------- */
/* HAL callbacks                                                         */
/* -------------------------------------------------------------------- */

static int TisMpfsInit(void *ctx, FWTPM_TIS_REGS **regs)
{
    FWTPM_TIS_MPFS_CTX *mc = (FWTPM_TIS_MPFS_CTX *)ctx;
    uint8_t *base = (uint8_t *)(uintptr_t)FWTPM_TIS_SHM_BASE;

    /* Direct pointers into the shared region -- no mmap in M-mode. The
     * base is FWTPM_TIS_SHM_BASE (L2 LIM, or the non-cached DDR alias
     * under the DDR_NONCACHED spike build). */
    mc->mbox    = (FWTPM_MPFS_MAILBOX *)(base + FWTPM_MBOX_OFFSET);
    mc->console = (FWTPM_CONSOLE_RING *)(base + FWTPM_CONSOLE_OFFSET);
    mc->regs    = (FWTPM_TIS_REGS *)   (base + FWTPM_TIS_REGS_OFFSET);

    /* Clear only the TIS regs/FIFO area. The mailbox and console ring
     * already contain state written by main() before FWTPM_TIS_Init()
     * (progress markers, banner output) and must be preserved. */
    memset((void *)mc->regs, 0, sizeof(*mc->regs));

    /* Init mailbox signaling fields. Magic/version/progress already
     * set by startup.S and main(), but re-asserting magic/version
     * here is harmless and self-documenting. */
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

static int TisMpfsWaitRequest(void *ctx)
{
    FWTPM_TIS_MPFS_CTX *mc = (FWTPM_TIS_MPFS_CTX *)ctx;
#if defined(FWTPM_XPORT_IHC)
    /* IHC backend not wired yet (gated on the Video Kit Libero bitstream
     * containing the IHC IP). Return -1 so FWTPM_TIS_ServerLoop treats it
     * as EINTR-continue and idles cleanly instead of busy-spinning a dead
     * channel. The real backend will block on an IHC message here. */
    (void)mc;
    return -1;
#else
    uint32_t nonce;

    /* Poll cmd_ready (a client-supplied nonzero nonce). Under HSS AMP
     * (which sets up hart 4's PMP/PMA) the cacheable L2 LIM mailbox is
     * coherent for hart 4 and Linux's /dev/mem writes are observed here
     * -- the full round-trip is verified on the Video Kit. The stale-read
     * issue only appeared in a standalone skip-opensbi bring-up without
     * that setup; DDR_NONCACHED is a fully-uncached alternative. */
    while (mc->mbox->cmd_ready == 0) {
        __asm__ volatile("fence iorw, iorw" ::: "memory");
    }
    nonce = mc->mbox->cmd_ready;

    mpfs_fence_r();

    /* Echo the nonce so the client can confirm hart 4 observed THIS write
     * rather than a stale cached line, then acknowledge by clearing. */
    mc->mbox->echo_nonce = nonce;
    mc->mbox->cmd_ready = 0;
    mpfs_fence_w();
    return 0;
#endif
}

static int TisMpfsSignalResponse(void *ctx)
{
    FWTPM_TIS_MPFS_CTX *mc = (FWTPM_TIS_MPFS_CTX *)ctx;

    mpfs_fence_w();
    mc->mbox->rsp_ready = 1;

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
