/* fwtpm_nv_qspi.c
 *
 * FWTPM_NV_HAL implementation backed by ZCU102 dual-stacked QSPI flash
 * (MT25QU512) using the Xilinx XQspiPsu driver. The R5 owns a 64 KB
 * region (ZCU102_FWTPM_NV_QSPI_SIZE) at ZCU102_FWTPM_NV_QSPI_OFFSET;
 * the Linux DTS must mark this partition as off-limits.
 *
 * Read/write/erase are passthroughs to XQspiPsu_PolledTransfer with
 * standard 4-byte-address commands (FAST_READ_4B, PP_4B, SE_4B). The
 * fwTPM upper layers handle the TLV journal -- this HAL is a flat
 * sector-aligned read/write/erase store.
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

#include <wolftpm/fwtpm/fwtpm.h>
#include <wolftpm/fwtpm/fwtpm_nv.h>

#include <xqspipsu.h>

#include <stdint.h>
#include <string.h>

/* Logging uses xil_printf() (declared by the BSP headers): it routes
 * through outbyte(), which this port redirects to the remoteproc trace
 * ring -- hang-safe here, unlike newlib printf(). */

/* ------------------------------------------------------------------ */
/* QSPI commands (Micron MT25QU512 4-byte address mode)               */
/* ------------------------------------------------------------------ */
#define QSPI_CMD_FAST_READ_4B      0x0CU
#define QSPI_CMD_PAGE_PROGRAM_4B   0x12U
#define QSPI_CMD_SECTOR_ERASE_4B   0xDCU
#define QSPI_CMD_WRITE_ENABLE      0x06U
#define QSPI_CMD_READ_STATUS       0x05U
#define QSPI_CMD_ENTER_4B          0xB7U

#define QSPI_PAGE_SIZE             256U
#define QSPI_SECTOR_SIZE           65536U
#define QSPI_STATUS_WIP            0x01U  /* Write-in-progress */

/* ------------------------------------------------------------------ */
/* Driver instance                                                    */
/* ------------------------------------------------------------------ */
static XQspiPsu g_qspi;
static int      g_qspi_ready;

static int qspi_wait_done(void)
{
    XQspiPsu_Msg msg[2];
    uint8_t cmd = QSPI_CMD_READ_STATUS;
    uint8_t status = 0xFF;
    int rc;
    int spin = 0;

    msg[0].TxBfrPtr = &cmd;
    msg[0].RxBfrPtr = NULL;
    msg[0].ByteCount = 1;
    msg[0].BusWidth = XQSPIPSU_SELECT_MODE_SPI;
    msg[0].Flags = XQSPIPSU_MSG_FLAG_TX;
    msg[1].TxBfrPtr = NULL;
    msg[1].RxBfrPtr = &status;
    msg[1].ByteCount = 1;
    msg[1].BusWidth = XQSPIPSU_SELECT_MODE_SPI;
    msg[1].Flags = XQSPIPSU_MSG_FLAG_RX;

    /* Bound the WIP spin so a stuck flash returns a real error up to
     * FWTPM_ProcessCommand instead of corrupting the journal. ~100k
     * polls covers worst-case sector erase (~1s @ MT25QU512). */
    do {
        rc = XQspiPsu_PolledTransfer(&g_qspi, msg, 2);
        if (rc != XST_SUCCESS) {
            return rc;
        }
        if (++spin > 100000) {
            xil_printf("qspi_wait_done: WIP timeout, status=0x%02x\n", status);
            return XST_FAILURE;
        }
    } while ((status & QSPI_STATUS_WIP) != 0U);
    return XST_SUCCESS;
}

static int qspi_write_enable(void)
{
    XQspiPsu_Msg msg;
    uint8_t cmd = QSPI_CMD_WRITE_ENABLE;
    msg.TxBfrPtr = &cmd;
    msg.RxBfrPtr = NULL;
    msg.ByteCount = 1;
    msg.BusWidth = XQSPIPSU_SELECT_MODE_SPI;
    msg.Flags = XQSPIPSU_MSG_FLAG_TX;
    return XQspiPsu_PolledTransfer(&g_qspi, &msg, 1);
}

/* Select the stacked die for an absolute flash address and return the
 * die-relative address to place in the command bytes. The pair is
 * dual-STACKED (two MT25QU512, 64 MiB each, sharing one bus): the lower
 * die covers 0..SINGLE-1 and the upper die covers SINGLE..2*SINGLE-1.
 * In stacked mode both dies sit on the lower bus and are picked apart
 * by chip-select, so BUS stays LOWER and only CS toggles. The fwTPM NV
 * region (ZCU102_FWTPM_NV_QSPI_OFFSET) lies wholly in the upper die.
 * Every command must call this first so write-enable, program, erase
 * and status polls all target the same die. */
static uint32_t qspi_select_addr(uint32_t addr)
{
    if (addr >= ZCU102_QSPI_SINGLE_FLASH_SIZE) {
        XQspiPsu_SelectFlash(&g_qspi, XQSPIPSU_SELECT_FLASH_CS_UPPER,
                              XQSPIPSU_SELECT_FLASH_BUS_LOWER);
        return addr - ZCU102_QSPI_SINGLE_FLASH_SIZE;
    }
    XQspiPsu_SelectFlash(&g_qspi, XQSPIPSU_SELECT_FLASH_CS_LOWER,
                          XQSPIPSU_SELECT_FLASH_BUS_LOWER);
    return addr;
}

/* ------------------------------------------------------------------ */
/* FWTPM_NV_HAL callbacks                                             */
/* ------------------------------------------------------------------ */
static int nv_qspi_read(void* halCtx, word32 offset, byte* buf, word32 size)
{
    uint32_t addr;
    uint8_t cmdBuf[6];
    XQspiPsu_Msg msg[2];

    (void)halCtx;
    if (!g_qspi_ready) {
        return -1;
    }
    /* Overflow-safe bounds check: reject oversized size first, then the
     * offset, without forming offset+size which can wrap (see STM32 HAL). */
    if (size > ZCU102_FWTPM_NV_QSPI_SIZE ||
        offset > ZCU102_FWTPM_NV_QSPI_SIZE - size) {
        return -1;
    }

    addr = qspi_select_addr(ZCU102_FWTPM_NV_QSPI_OFFSET + offset);
    cmdBuf[0] = QSPI_CMD_FAST_READ_4B;
    cmdBuf[1] = (uint8_t)(addr >> 24);
    cmdBuf[2] = (uint8_t)(addr >> 16);
    cmdBuf[3] = (uint8_t)(addr >> 8);
    cmdBuf[4] = (uint8_t)(addr);
    /* FAST_READ (0x0C) clocks 8 dummy cycles = 1 dummy byte after the
     * 4-byte address before data. XQspiPsu_PolledTransfer sends the
     * command buffer verbatim, so the dummy byte must be in cmdBuf. */
    cmdBuf[5] = 0x00U;

    msg[0].TxBfrPtr = cmdBuf;
    msg[0].RxBfrPtr = NULL;
    msg[0].ByteCount = 6;
    msg[0].BusWidth = XQSPIPSU_SELECT_MODE_SPI;
    msg[0].Flags = XQSPIPSU_MSG_FLAG_TX;
    msg[1].TxBfrPtr = NULL;
    msg[1].RxBfrPtr = buf;
    msg[1].ByteCount = size;
    msg[1].BusWidth = XQSPIPSU_SELECT_MODE_SPI;
    msg[1].Flags = XQSPIPSU_MSG_FLAG_RX;
    return XQspiPsu_PolledTransfer(&g_qspi, msg, 2);
}

static int nv_qspi_write(void* halCtx, word32 offset, const byte* buf,
                          word32 size)
{
    word32 remaining = size;
    word32 cur_off = offset;
    const byte* cur_buf = buf;
    int rc;

    (void)halCtx;
    if (!g_qspi_ready) {
        return -1;
    }
    /* Overflow-safe bounds check (see nv_qspi_read). */
    if (size > ZCU102_FWTPM_NV_QSPI_SIZE ||
        offset > ZCU102_FWTPM_NV_QSPI_SIZE - size) {
        return -1;
    }

    while (remaining > 0U) {
        uint32_t abs_addr = ZCU102_FWTPM_NV_QSPI_OFFSET + cur_off;
        uint32_t page_off = abs_addr & (QSPI_PAGE_SIZE - 1U);
        uint32_t chunk = QSPI_PAGE_SIZE - page_off;
        uint32_t addr;
        uint8_t cmdBuf[5];
        XQspiPsu_Msg msg[2];
        if (chunk > remaining) {
            chunk = remaining;
        }

        /* Select the stacked die first so write-enable, program and
         * the WIP status poll all target the same die. */
        addr = qspi_select_addr(abs_addr);
        rc = qspi_write_enable();
        if (rc != XST_SUCCESS) return rc;

        cmdBuf[0] = QSPI_CMD_PAGE_PROGRAM_4B;
        cmdBuf[1] = (uint8_t)(addr >> 24);
        cmdBuf[2] = (uint8_t)(addr >> 16);
        cmdBuf[3] = (uint8_t)(addr >> 8);
        cmdBuf[4] = (uint8_t)(addr);

        msg[0].TxBfrPtr = cmdBuf;
        msg[0].RxBfrPtr = NULL;
        msg[0].ByteCount = 5;
        msg[0].BusWidth = XQSPIPSU_SELECT_MODE_SPI;
        msg[0].Flags = XQSPIPSU_MSG_FLAG_TX;
        msg[1].TxBfrPtr = (uint8_t*)cur_buf;
        msg[1].RxBfrPtr = NULL;
        msg[1].ByteCount = chunk;
        msg[1].BusWidth = XQSPIPSU_SELECT_MODE_SPI;
        msg[1].Flags = XQSPIPSU_MSG_FLAG_TX;
        rc = XQspiPsu_PolledTransfer(&g_qspi, msg, 2);
        if (rc != XST_SUCCESS) return rc;

        rc = qspi_wait_done();
        if (rc != XST_SUCCESS) return rc;

        cur_off += chunk;
        cur_buf += chunk;
        remaining -= chunk;
    }
    return 0;
}

static int nv_qspi_erase(void* halCtx, word32 offset, word32 size)
{
    word32 cur_off = offset & ~(QSPI_SECTOR_SIZE - 1U);
    word32 end = (offset + size + QSPI_SECTOR_SIZE - 1U) &
                  ~(QSPI_SECTOR_SIZE - 1U);

    (void)halCtx;
    if (!g_qspi_ready) {
        return -1;
    }
    /* Overflow-safe bounds check (see nv_qspi_read). The loop below
     * rounds [offset, offset+size) out to whole sectors; guard the
     * original request against the partition so the rounded-up 'end'
     * (which itself could wrap) can never erase past the NV region. */
    if (size > ZCU102_FWTPM_NV_QSPI_SIZE ||
        offset > ZCU102_FWTPM_NV_QSPI_SIZE - size) {
        return -1;
    }

    while (cur_off < end) {
        uint32_t addr = qspi_select_addr(ZCU102_FWTPM_NV_QSPI_OFFSET + cur_off);
        uint8_t cmdBuf[5];
        XQspiPsu_Msg msg;
        int rc;

        rc = qspi_write_enable();
        if (rc != XST_SUCCESS) return rc;

        cmdBuf[0] = QSPI_CMD_SECTOR_ERASE_4B;
        cmdBuf[1] = (uint8_t)(addr >> 24);
        cmdBuf[2] = (uint8_t)(addr >> 16);
        cmdBuf[3] = (uint8_t)(addr >> 8);
        cmdBuf[4] = (uint8_t)(addr);
        msg.TxBfrPtr = cmdBuf;
        msg.RxBfrPtr = NULL;
        msg.ByteCount = 5;
        msg.BusWidth = XQSPIPSU_SELECT_MODE_SPI;
        msg.Flags = XQSPIPSU_MSG_FLAG_TX;
        rc = XQspiPsu_PolledTransfer(&g_qspi, &msg, 1);
        if (rc != XST_SUCCESS) return rc;

        rc = qspi_wait_done();
        if (rc != XST_SUCCESS) return rc;

        cur_off += QSPI_SECTOR_SIZE;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Init: configure XQspiPsu and enter 4-byte address mode             */
/* ------------------------------------------------------------------ */
/* Outbyte-backed trace breadcrumbs. newlib printf()/stdio is unsafe on
 * this build (not wired up; it hangs at the first call), so logging here
 * goes through outbyte() -- qspi_emit() for breadcrumbs and xil_printf()
 * for the rare value-carrying error. Each step is bracketed so a hang in
 * XQspiPsu_* is observable in the trace ring instead of a silent
 * _data_abort spin. The order A..F documents the empirical hang point
 * seen during bring-up: with &qspi { status = "disabled" } in DTS,
 * PMUFW power-gates the QSPI controller and XQspiPsu_PolledTransfer at
 * step E never completes -- which is why qspi init issues an EEMI
 * PM_REQUEST_NODE for the QSPI node before XQspiPsu_LookupConfig. */
extern void outbyte(char c);
static void qspi_emit(const char *s) { while (*s) outbyte(*s++); }

int zynqmp_r5_nv_qspi_init(FWTPM_NV_HAL* hal)
{
    XQspiPsu_Config* cfg;
    XQspiPsu_Msg msg;
    uint8_t cmd;
    int rc;

    /* Bring up the QSPI clock + power domain via PMU EEMI before
     * touching any controller register. With Linux's spi-zynqmp-qspi
     * driver disabled in DTS (status="disabled") PMUFW would otherwise
     * keep QSPI gated -- XQspiPsu_PolledTransfer then hangs at the
     * first WIP poll because the controller never responds. The
     * PM_REQUEST_NODE call is idempotent: PMU returns 0 success
     * regardless of prior state. */
    qspi_emit("qspi: 0 pmu_request_node(QSPI)\r\n");
    rc = pmu_eemi_init();
    if (rc == 0) {
        rc = pmu_request_node(PMU_NODE_QSPI);
    }
    if (rc != 0) {
        qspi_emit("qspi: ERR pmu_request_node\r\n");
        /* Continue anyway -- on a board where PMUFW is permissive or
         * QSPI is already on, LookupConfig + CfgInitialize may still
         * succeed. The hang point (step E) is what tells us power
         * actually reached the controller. */
    }

    qspi_emit("qspi: A LookupConfig\r\n");
    cfg = XQspiPsu_LookupConfig(XPAR_XQSPIPSU_0_BASEADDR);
    if (cfg == NULL) {
        qspi_emit("qspi: ERR LookupConfig returned NULL\r\n");
        return -1;
    }
    qspi_emit("qspi: B CfgInitialize\r\n");
    rc = XQspiPsu_CfgInitialize(&g_qspi, cfg, cfg->BaseAddress);
    if (rc != XST_SUCCESS) {
        qspi_emit("qspi: ERR CfgInitialize\r\n");
        return rc;
    }
    qspi_emit("qspi: C SetClkPrescaler\r\n");
    XQspiPsu_SetClkPrescaler(&g_qspi, XQSPIPSU_CLK_PRESCALE_8);
    qspi_emit("qspi: D SelectFlash\r\n");
    /* Select the stacked die that owns the NV region (upper die for the
     * default 0x07FF0000 offset). Subsequent read/write/erase calls
     * re-select per access, but the WREN + ENTER_4B below must target
     * the NV die. */
    (void)qspi_select_addr(ZCU102_FWTPM_NV_QSPI_OFFSET);

    qspi_emit("qspi: E write_enable\r\n");
    /* Enter 4-byte address mode (no-op if already in 4B). */
    rc = qspi_write_enable();
    if (rc != XST_SUCCESS) {
        qspi_emit("qspi: ERR write_enable\r\n");
        return rc;
    }
    qspi_emit("qspi: F enter4b\r\n");
    cmd = QSPI_CMD_ENTER_4B;
    msg.TxBfrPtr = &cmd;
    msg.RxBfrPtr = NULL;
    msg.ByteCount = 1;
    msg.BusWidth = XQSPIPSU_SELECT_MODE_SPI;
    msg.Flags = XQSPIPSU_MSG_FLAG_TX;
    rc = XQspiPsu_PolledTransfer(&g_qspi, &msg, 1);
    if (rc != XST_SUCCESS) return rc;

    g_qspi_ready = 1;

    hal->read    = nv_qspi_read;
    hal->write   = nv_qspi_write;
    hal->erase   = nv_qspi_erase;
    hal->ctx     = NULL;
    hal->maxSize = ZCU102_FWTPM_NV_QSPI_SIZE;
    return 0;
}
