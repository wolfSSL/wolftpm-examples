/* main.c
 *
 * fwTPM server entry point for bare-metal U54 hart on PolarFire SoC.
 * Runs on hart 4 in M-mode, loaded by HSS AMP payload at 0x91C00000.
 *
 * Initializes shared-memory TIS HAL, RAM-backed NV, MTIME clock,
 * then enters the TIS server loop waiting for commands from Linux.
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
#include "mpfs_hal.h"
#include "fwtpm_tis_mpfs.h"

#include <wolfssl/wolfcrypt/logging.h>

#include <wolftpm/fwtpm/fwtpm.h>
#include <wolftpm/fwtpm/fwtpm_nv.h>
#include <wolftpm/fwtpm/fwtpm_tis.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Forward declarations for HAL init functions */
extern int  FWTPM_NV_RAM_Init(FWTPM_NV_HAL *hal);
extern int  FWTPM_Clock_MPFS_Init(FWTPM_CTX *ctx);
extern void FWTPM_TIS_MPFS_SetHAL(FWTPM_CTX *ctx);

/* Static fwTPM context (avoids heap) */
static FWTPM_CTX g_ctx;

int main(void)
{
    int rc;
    FWTPM_NV_HAL nvHal;
    /* Live mailbox (moves to the non-cached alias under DDR_NONCACHED). */
    volatile FWTPM_MPFS_MAILBOX *mbox =
        (volatile FWTPM_MPFS_MAILBOX *)(uintptr_t)FWTPM_TIS_SHM_BASE;
    /* Boot breadcrumbs go to the always-LIM debug mailbox so they stay
     * visible to Linux even when the live mailbox relocates. When the two
     * bases coincide (LIM_CACHED / L1D_OFF / IHC) these alias each other. */
    volatile FWTPM_MPFS_MAILBOX *dbg =
        (volatile FWTPM_MPFS_MAILBOX *)(uintptr_t)FWTPM_DBG_BASE;

    /* Breadcrumb: write progress markers to shared memory so we can
     * diagnose how far boot gets even without UART4 or console ring.
     * startup.S already wrote FWTPM_PROG_ENTRY before reaching here. */
    dbg->progress = FWTPM_PROG_MAIN;
    mpfs_fence_w();

    /* Init UART4 for debug console.
     * HSS has configured PLL: APB clock = 150 MHz. */
    mpfs_uart_init(FWTPM_UART_BASE, MSS_APB_AHB_CLK, 115200);

    /* Wire up the shared-memory console ring so printf output is captured
     * even when UART4 is not physically routed (the Linux side reads it
     * from /dev/mem). */
    mpfs_console_init(
        (FWTPM_CONSOLE_RING *)((uintptr_t)FWTPM_TIS_SHM_BASE +
                               FWTPM_CONSOLE_OFFSET));

    dbg->progress = FWTPM_PROG_UART;

    printf("\n");
    printf("=== wolfTPM fwTPM on PolarFire SoC U54 (hart 4, M-mode) ===\n");
    printf("wolfTPM fwTPM version %s\n", FWTPM_VERSION_STRING);
    printf("APB clock = %lu Hz\n", (unsigned long)MSS_APB_AHB_CLK);

    dbg->progress = FWTPM_PROG_PRINTF;

    /* Zero context */
    memset(&g_ctx, 0, sizeof(g_ctx));

    /* Register NV storage HAL (RAM-backed, volatile) */
    memset(&nvHal, 0, sizeof(nvHal));
    rc = FWTPM_NV_RAM_Init(&nvHal);
    if (rc != 0) {
        printf("ERROR: NV RAM init failed: %d\n", rc);
        goto halt;
    }
    rc = FWTPM_NV_SetHAL(&g_ctx, &nvHal);
    if (rc != 0) {
        printf("ERROR: NV SetHAL failed: %d\n", rc);
        goto halt;
    }

    /* Register clock HAL (MTIME-based) */
    rc = FWTPM_Clock_MPFS_Init(&g_ctx);
    if (rc != 0) {
        printf("ERROR: Clock init failed: %d\n", rc);
        goto halt;
    }

    dbg->progress = FWTPM_PROG_NV_CLOCK;

    /* Register TIS transport HAL (shared memory + IPI) */
    FWTPM_TIS_MPFS_SetHAL(&g_ctx);

    dbg->progress = FWTPM_PROG_TIS_HAL;

    /* Enable wolfSSL debug logging -- output captured by the console ring. */
    wolfSSL_Debugging_ON();

    /* Initialize fwTPM (generates seeds, initializes PCRs, etc.) */
    printf("Initializing fwTPM...\n");
    rc = FWTPM_Init(&g_ctx);
    dbg->progress = FWTPM_PROG_INIT_DONE;
    mbox->rc = (uint32_t)rc;
    if (rc != 0) {
        printf("ERROR: FWTPM_Init failed: %d\n", rc);
        goto halt;
    }
    printf("fwTPM initialized successfully\n");

    /* Initialize the TIS transport (calls hal->init, allocates regs).
     * FWTPM_Init() does not do this -- it must be called separately. */
    rc = FWTPM_TIS_Init(&g_ctx);
    if (rc != 0) {
        printf("ERROR: FWTPM_TIS_Init failed: %d\n", rc);
        goto halt;
    }

    dbg->progress = FWTPM_PROG_SERVERLOOP;

    /* Enter TIS server loop (blocks forever, processing commands) */
    printf("Entering TIS server loop (waiting for commands)\n");
    rc = FWTPM_TIS_ServerLoop(&g_ctx);
    dbg->progress = FWTPM_PROG_LOOP_EXIT;

    printf("TIS server loop exited: %d\n", rc);

halt:
    /* Reached on a fatal init error (via goto) or, unexpectedly, if the
     * server loop ever returns. Both cases simply halt in WFI. */
    printf("fwTPM halted\n");
    for (;;) {
        __asm__ volatile("wfi");
    }
}
