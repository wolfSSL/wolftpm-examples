/* pmu_eemi.c
 *
 * Minimal EEMI client for the Cortex-R5 lock-step fwTPM. Sends
 * PM_REQUEST_NODE / PM_RELEASE_NODE messages to PMUFW over the
 * RPU0 IPI channel so the R5 can ask PMU to power up devices the
 * Linux DTS has marked status="disabled" -- specifically QSPI for
 * persistent NV (fwtpm_nv_qspi.c).
 *
 * Wire format (per ZynqMP TRM ug1085 Sec.13 / pm_api_sys.c):
 *   payload[0] = PM_REQUEST_NODE          (= 0x0D)
 *   payload[1] = node id                  (e.g. NODE_QSPI = 0x2D)
 *   payload[2] = capabilities             (PM_CAP_ACCESS = 0x01)
 *   payload[3] = qos                      (MAX_QOS = 100)
 *   payload[4] = ack mode                 (REQUEST_ACK_BLOCKING = 1)
 *   payload[5..6] = 0
 *
 * RPU0 IPI block at 0xFF310000 targets PMU channel 0 (mask bit 0
 * = 0x00000001) via XIpiPsu_TriggerIpi. PMU writes the response
 * back into RPU0's response buffer; XIpiPsu_PollForAck blocks
 * (bounded) until PMU clears its OBS bit; XIpiPsu_ReadMessage
 * fetches the 4-word response. resp[0] is the EEMI status: 0 =
 * success.
 *
 * No XIpiPsu interrupt path is used here -- ZynqMP IPI completion
 * is observable purely via the OBS register, and our other ISR
 * (ipi_isr in platform_info.c) is dedicated to APU<->RPU rpmsg.
 *
 * Copyright (C) 2006-2026 wolfSSL Inc.
 *
 * This file is part of wolfTPM.
 *
 * wolfTPM is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 */

#include "user_settings.h"
#include "zcu102_r5.h"
#include "include/xparameters.h"

#include <xipipsu.h>

#include <stdint.h>
#include <stdio.h>

/* EEMI API IDs (subset). Values verified against
 * embeddedsw/lib/bsp/standalone_v9_4/src/common/pm_api_version.h. */
#define EEMI_PM_REQUEST_NODE    0x0DU
#define EEMI_PM_RELEASE_NODE    0x0EU

/* Capabilities + ack mode + QoS (zynqmp pm_defs.h). */
#define EEMI_CAP_ACCESS         0x01U
#define EEMI_QOS_MAX            100U
#define EEMI_ACK_BLOCKING       1U

/* Target mask for PMU0 from RPU0's IPI block. PMU is IPI channel 0,
 * so the bit position in TRIG/OBS/ISR is bit 0. */
#define IPI_PMU0_MASK           0x00000001U

extern void outbyte(char c);
static void eemi_emit(const char *s) { while (*s) outbyte(*s++); }

static XIpiPsu g_pmu_ipi;
static int     g_pmu_ipi_ready;

int pmu_eemi_init(void)
{
    XIpiPsu_Config* cfg;
    int rc;

    if (g_pmu_ipi_ready) {
        return 0;
    }
    /* Use RPU0's own IPI block (XPAR_PSU_IPI_1_BASE_ADDRESS in our
     * xparameters.h). PMU is the *target* selected by the per-call
     * DestCpuMask. */
    cfg = XIpiPsu_LookupConfig(XPAR_PSU_IPI_1_BASE_ADDRESS);
    if (cfg == NULL) {
        eemi_emit("eemi: ERR XIpiPsu_LookupConfig\r\n");
        return -1;
    }
    rc = XIpiPsu_CfgInitialize(&g_pmu_ipi, cfg, cfg->BaseAddress);
    if (rc != XST_SUCCESS) {
        eemi_emit("eemi: ERR XIpiPsu_CfgInitialize\r\n");
        return rc;
    }
    g_pmu_ipi_ready = 1;
    return 0;
}

/* Send a 5-word EEMI command to PMU and read the response. Returns
 * EEMI status (0 = success) or a negative XStatus on transport
 * failure. timeout_us bounds the PMU ACK poll. */
static int pmu_eemi_send(uint32_t api_id, uint32_t arg0, uint32_t arg1,
                         uint32_t arg2, uint32_t arg3, uint32_t timeout_us)
{
    uint32_t payload[8];
    uint32_t resp[4];
    int rc;

    if (!g_pmu_ipi_ready) {
        return -1;
    }
    payload[0] = api_id;
    payload[1] = arg0;
    payload[2] = arg1;
    payload[3] = arg2;
    payload[4] = arg3;
    payload[5] = 0;
    payload[6] = 0;
    payload[7] = 0;

    rc = XIpiPsu_WriteMessage(&g_pmu_ipi, IPI_PMU0_MASK, payload, 7,
                              XIPIPSU_BUF_TYPE_MSG);
    if (rc != XST_SUCCESS) {
        eemi_emit("eemi: ERR WriteMessage\r\n");
        return rc;
    }
    rc = XIpiPsu_TriggerIpi(&g_pmu_ipi, IPI_PMU0_MASK);
    if (rc != XST_SUCCESS) {
        eemi_emit("eemi: ERR TriggerIpi\r\n");
        return rc;
    }
    rc = XIpiPsu_PollForAck(&g_pmu_ipi, IPI_PMU0_MASK, timeout_us);
    if (rc != XST_SUCCESS) {
        eemi_emit("eemi: ERR PollForAck\r\n");
        return rc;
    }
    rc = XIpiPsu_ReadMessage(&g_pmu_ipi, IPI_PMU0_MASK, resp, 4,
                             XIPIPSU_BUF_TYPE_RESP);
    if (rc != XST_SUCCESS) {
        eemi_emit("eemi: ERR ReadMessage\r\n");
        return rc;
    }
    /* resp[0] is the EEMI status. PMU returns 0 on success. */
    return (int)resp[0];
}

int pmu_request_node(unsigned int node_id)
{
    int rc;
    if (!g_pmu_ipi_ready) {
        rc = pmu_eemi_init();
        if (rc != 0) return rc;
    }
    rc = pmu_eemi_send(EEMI_PM_REQUEST_NODE, (uint32_t)node_id,
                       EEMI_CAP_ACCESS, EEMI_QOS_MAX, EEMI_ACK_BLOCKING,
                       1000000U);
    if (rc != 0) {
        eemi_emit("eemi: PM_REQUEST_NODE non-zero status\r\n");
    }
    return rc;
}

int pmu_release_node(unsigned int node_id)
{
    int rc;
    if (!g_pmu_ipi_ready) {
        return -1;
    }
    /* PM_RELEASE_NODE takes only the node id as arg0. */
    rc = pmu_eemi_send(EEMI_PM_RELEASE_NODE, (uint32_t)node_id, 0, 0, 0,
                       1000000U);
    return rc;
}
