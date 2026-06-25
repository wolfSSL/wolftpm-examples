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
#include <wolftpm/fwtpm/fwtpm_command.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Forward declarations for HAL init functions */
extern int  FWTPM_NV_RAM_Init(FWTPM_NV_HAL *hal);
extern int  FWTPM_Clock_MPFS_Init(FWTPM_CTX *ctx);
extern void FWTPM_TIS_MPFS_SetHAL(FWTPM_CTX *ctx);

/* Static fwTPM context (avoids heap) */
static FWTPM_CTX g_ctx;

/* -------------------------------------------------------------------- */
/* One-shot TPM 2.0 capability + random self-test, executed on hart 4   */
/* via FWTPM_ProcessCommand and printed to the console ring (read from   */
/* Linux over /dev/mem). Exercises a real TPM command path without the   */
/* interactive mailbox round-trip.                                       */
/* -------------------------------------------------------------------- */
static uint32_t fwtpm_be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static void fwtpm_caps_selftest(FWTPM_CTX *ctx)
{
    uint8_t cmd[32];
    uint8_t rsp[512];
    int rspSize;
    int rc;
    unsigned int i;
    uint32_t prop, val, count;
    static const uint32_t props[3] = {
        0x00000105UL,   /* TPM_PT_MANUFACTURER */
        0x00000106UL,   /* TPM_PT_VENDOR_STRING_1 */
        0x0000010BUL    /* TPM_PT_FIRMWARE_VERSION_1 */
    };
    static const char *names[3] = { "Manufacturer ", "VendorString ",
                                    "FirmwareVer  " };

    /* TPM2_Startup(SU_CLEAR): required before GetRandom and most commands
     * (GetCapability is allowed beforehand). Ignore the RC -- a second
     * startup just returns TPM_RC_INITIALIZE. */
    cmd[0] = 0x80; cmd[1] = 0x01;
    cmd[2] = 0; cmd[3] = 0; cmd[4] = 0; cmd[5] = 12;
    cmd[6] = 0; cmd[7] = 0; cmd[8] = 0x01; cmd[9] = 0x44;  /* TPM2_Startup */
    cmd[10] = 0; cmd[11] = 0;                              /* TPM_SU_CLEAR */
    rspSize = (int)sizeof(rsp);
    (void)FWTPM_ProcessCommand(ctx, cmd, 12, rsp, &rspSize, 0);

    printf("\n=== fTPM TPM2_GetCapability (executed on hart 4) ===\n");
    for (i = 0; i < 3; i++) {
        prop = props[i];
        cmd[0] = 0x80; cmd[1] = 0x01;                  /* TPM_ST_NO_SESSIONS */
        cmd[2] = 0; cmd[3] = 0; cmd[4] = 0; cmd[5] = 22;
        cmd[6] = 0; cmd[7] = 0; cmd[8] = 0x01; cmd[9] = 0x7A; /* GetCapability */
        cmd[10] = 0; cmd[11] = 0; cmd[12] = 0; cmd[13] = 6;   /* TPM_PROPERTIES */
        cmd[14] = (uint8_t)(prop >> 24); cmd[15] = (uint8_t)(prop >> 16);
        cmd[16] = (uint8_t)(prop >> 8);  cmd[17] = (uint8_t)prop;
        cmd[18] = 0; cmd[19] = 0; cmd[20] = 0; cmd[21] = 1;   /* count = 1 */
        rspSize = (int)sizeof(rsp);
        rc = FWTPM_ProcessCommand(ctx, cmd, 22, rsp, &rspSize, 0);
        if (rc != 0 || rspSize < 19 || fwtpm_be32(rsp + 6) != 0) {
            printf("  %s: rc=%d tpm_rc=0x%08lx\n", names[i], rc,
                   (unsigned long)(rspSize >= 10 ? fwtpm_be32(rsp + 6) : 0));
            continue;
        }
        count = fwtpm_be32(rsp + 15);
        val = (count >= 1 && rspSize >= 27) ? fwtpm_be32(rsp + 23) : 0;
        printf("  %s = 0x%08lx", names[i], (unsigned long)val);
        if (prop == 0x105UL || prop == 0x106UL) {
            char s[5];
            s[0] = (char)(val >> 24); s[1] = (char)(val >> 16);
            s[2] = (char)(val >> 8);  s[3] = (char)val; s[4] = 0;
            printf("  \"%s\"", s);
        }
        printf("\n");
    }

    /* Live entropy: TPM2_GetRandom(16) seeded by the System Controller TRNG */
    cmd[0] = 0x80; cmd[1] = 0x01;
    cmd[2] = 0; cmd[3] = 0; cmd[4] = 0; cmd[5] = 12;
    cmd[6] = 0; cmd[7] = 0; cmd[8] = 0x01; cmd[9] = 0x7B;  /* GetRandom */
    cmd[10] = 0; cmd[11] = 16;
    rspSize = (int)sizeof(rsp);
    rc = FWTPM_ProcessCommand(ctx, cmd, 12, rsp, &rspSize, 0);
    if (rc == 0 && rspSize >= 12 && fwtpm_be32(rsp + 6) == 0) {
        unsigned int rn = (unsigned int)(((unsigned int)rsp[10] << 8) | rsp[11]);
        /* Clamp the wire-supplied count to the bytes the response actually
         * carries so a malformed reply cannot over-read rsp[]. */
        if (rn > (unsigned int)rspSize - 12)
            rn = (unsigned int)rspSize - 12;
        printf("  TPM2_GetRandom(%u) =", rn);
        for (i = 0; i < rn; i++)
            printf(" %02x", rsp[12 + i]);
        printf("\n");
    }
    else {
        /* Guard the TPM RC decode: on an early ProcessCommand failure rspSize
         * can be < 10 and rsp[] is uninitialized (see GetCapability loop). */
        printf("  TPM2_GetRandom: rc=%d tpm_rc=0x%08lx\n", rc,
               (unsigned long)(rspSize >= 10 ? fwtpm_be32(rsp + 6) : 0));
    }
    printf("=== end capabilities ===\n\n");
}

int main(void)
{
    int rc;
    FWTPM_NV_HAL nvHal;
    /* Live mailbox (moves to the non-cached alias under DDR_NONCACHED). */
    volatile FWTPM_MPFS_MAILBOX *mbox =
        (volatile FWTPM_MPFS_MAILBOX *)(uintptr_t)FWTPM_TIS_SHM_BASE;
    /* Boot breadcrumbs go to the always-LIM debug mailbox so they stay
     * visible to Linux even if the live mailbox relocates (DDR_NONCACHED);
     * otherwise the two bases alias. */
    volatile FWTPM_MPFS_MAILBOX *dbg =
        (volatile FWTPM_MPFS_MAILBOX *)(uintptr_t)FWTPM_DBG_BASE;

    /* Progress markers in shared memory let Linux see how far boot got
     * even without UART4. startup.S already wrote FWTPM_PROG_ENTRY. */
    dbg->progress = FWTPM_PROG_MAIN;
    mpfs_fence_w();

    /* Init UART4 for debug console.
     * HSS has configured PLL: APB clock = 150 MHz. */
    mpfs_uart_init(FWTPM_UART_BASE, MSS_APB_AHB_CLK, 115200);

    /* Console ring captures printf output when UART4 is unrouted
     * (Linux reads it from /dev/mem). */
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

    /* Run a real TPM 2.0 caps/random command on hart 4; output goes to the
     * console ring, which Linux reads reliably over /dev/mem. */
    fwtpm_caps_selftest(&g_ctx);

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
