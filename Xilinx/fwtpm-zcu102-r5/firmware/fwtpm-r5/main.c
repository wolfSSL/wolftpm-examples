/* main.c
 *
 * fwTPM server entry point for ZCU102 Cortex-R5 RPU in lock-step mode.
 * Loaded by Linux remoteproc as ELF firmware; communicates with the
 * APU (PetaLinux) via OpenAMP RPMsg, with persistent NV in QSPI.
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
#include "zcu102_r5.h"

#include <wolfssl/wolfcrypt/logging.h>
#include <wolfssl/wolfcrypt/random.h>

#include <wolftpm/fwtpm/fwtpm.h>
#include <wolftpm/fwtpm/fwtpm_nv.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Static fwTPM context (avoid heap on bare-metal). */
static FWTPM_CTX g_ctx;

extern void outbyte(char c);
extern int  print(const char *str);
extern int  xil_printf(const char *ctrl1, ...);
extern void mpu_init(void);

static void emit(const char *s)
{
    while (*s) outbyte(*s++);
}

int main(void)
{
    int rc;
    FWTPM_NV_HAL nvHal;

    /* MPU first: libmetal atomics (LDREX/STREX) require Normal-typed
     * memory. Without an MPU map, R5 background regions default to
     * Strongly-Ordered where exclusives trap. Caches stay OFF so the
     * .trace_buffer mirror remains visible to APU without dcache flush. */
    mpu_init();

    /* GIC + IPI ISR before anything calls printf-on-error, so an
     * early IPI from APU during ELF load does not get latched and
     * lost. gic_init() also enables IRQs in CPSR. emit() breadcrumbs
     * around it so a fault inside the XScuGic driver shows up in
     * the trace ring instead of silent _data_abort spin. */
    emit("step 0: gic init\r\n");
    rc = gic_init();
    emit("step 0: gic init done\r\n");
    if (rc != 0) {
        xil_printf("ERROR: gic_init: %d\r\n", rc);
        goto error;
    }

    /* Use libxilstandalone print() / our outbyte() directly rather
     * than newlib printf(). newlib stdio pulls in __sinit / _sbrk /
     * malloc and hangs on bare-metal R5 without a complete c-runtime. */
    emit("\r\n=== wolfTPM fwTPM on ZCU102 R5 (lock-step) ===\r\n");
    emit("step 1: zero context\r\n");

    /* Zero context. */
    memset(&g_ctx, 0, sizeof(g_ctx));
    emit("step 2: zero nvHal\r\n");

    /* Register NV storage HAL.
     *
     * Default is the volatile 64 KiB DDR mirror (FWTPM_NV_RAM). The
     * persistent QSPI HAL is opt-in via -DFWTPM_NV_QSPI. With the Linux
     * spi-zynqmp-qspi driver disabled in DTS, PMUFW power-gates QSPI, so
     * zynqmp_r5_nv_qspi_init() issues an EEMI PM_REQUEST_NODE for the
     * QSPI node (pmu_eemi.c) before XQspiPsu_LookupConfig to bring the
     * controller's power/clock domain up; without that the polled
     * transfers would hang waiting for the controller. */
    memset(&nvHal, 0, sizeof(nvHal));
#ifdef FWTPM_NV_QSPI
    emit("step 3: nv qspi init (persistent)\r\n");
    rc = zynqmp_r5_nv_qspi_init(&nvHal);
#else
    emit("step 3: nv ram init (volatile)\r\n");
    rc = zynqmp_r5_nv_ram_init(&nvHal);
#endif
    emit("step 4: nv init done\r\n");
    if (rc != 0) {
        xil_printf("ERROR: NV init failed: %d\n", rc);
        goto error;
    }
    emit("step 5: NV SetHAL\r\n");
    rc = FWTPM_NV_SetHAL(&g_ctx, &nvHal);
    if (rc != 0) {
        xil_printf("ERROR: NV SetHAL failed: %d\n", rc);
        goto error;
    }
    emit("step 6: NV SetHAL done\r\n");

    /* Register clock HAL (R5 PMU cycle counter -> ms). */
    emit("step 7: clock init\r\n");
    rc = zynqmp_r5_clock_init(&g_ctx);
    emit("step 8: clock init done\r\n");
    if (rc != 0) {
        xil_printf("ERROR: Clock init failed: %d\n", rc);
        goto error;
    }

    /* Skip wolfSSL_Debugging_ON for first-light bring-up -- it pulls in
     * file/stdio logging that depends on full c-runtime stdio. */

    emit("step 10: FWTPM_Init\r\n");
    rc = FWTPM_Init(&g_ctx);
    emit("step 10: FWTPM_Init done\r\n");
    if (rc != 0) {
        xil_printf("ERROR: FWTPM_Init: %d\r\n", rc);
        goto error;
    }
    emit("fwTPM initialized successfully\r\n");

#if 1
    /* rpmsg server loop. Pre-conditions:
     *   - MPU is enabled (libmetal atomics need Normal-typed memory).
     *   - DT has an xlnx,zynqmp-ipi-mailbox node bound to r5f@0 via
     *     mboxes/mbox-names, so kick() actually fires the IPI.
     * Both are landed; if rpmsg fails to come up, drop to the #else
     * heartbeat branch for breadcrumb-only diagnostics. */
    emit("step 11: rpmsg init\r\n");
    rc = zynqmp_r5_rpmsg_init(&g_ctx);
    emit("step 12: rpmsg init done\r\n");
    if (rc != 0) {
        xil_printf("ERROR: rpmsg init failed: %d\n", rc);
        goto error;
    }
    emit("rpmsg ready, waiting for commands\r\n");

    /* IRQ-driven server loop: WFI until an IPI ISR fires, then drain
     * both vrings. With IRQs enabled, the core idles at near-zero
     * power between commands. zynqmp_r5_rpmsg_serve() does not
     * return unless a vring drain fails. */
    rc = zynqmp_r5_rpmsg_serve();
    xil_printf("rpmsg serve exit: %d\n", rc);
#else
    /* V1 lifecycle proof: heartbeat loop. Demonstrates R5 stays alive
     * after init. APU sees the dots in trace0; rpmsg round-trip is
     * left as a follow-on. */
    emit("step 11: heartbeat loop (rpmsg deferred)\r\n");
    {
        volatile uint32_t i;
        uint32_t tick = 0;
        for (;;) {
            for (i = 0; i < 30000000U; i++) { }    /* ~few seconds at 533 MHz */
            outbyte('.');
            outbyte('0' + (tick++ % 10));
            outbyte('\r');
            outbyte('\n');
            if (tick >= 100) tick = 0;
        }
    }
#endif

error:
    xil_printf("fwTPM halted\n");
    for (;;) {
        __asm__ volatile("wfi");
    }
}
