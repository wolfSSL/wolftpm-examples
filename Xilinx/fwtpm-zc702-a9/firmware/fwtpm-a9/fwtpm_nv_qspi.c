/* fwtpm_nv_qspi.c
 *
 * Persistent FWTPM_NV_HAL for the Zynq-7000 A9, backed by the QSPI NOR flash
 * (the same device wolfBoot boots from). Opt-in: build with -DFWTPM_NV_QSPI
 * (the default build uses the volatile fwtpm_nv_ram.c). Also provides the
 * persistent SRAM-PUF helper-data store (fwtpm_puf_helper_load/store), so the
 * PUF-derived NV integrity key is stable across real power cycles.
 *
 * The wolfTPM core owns a log-structured NV journal on top of this flat store.
 * Here NV is a RAM shadow loaded from flash at init; reads are served from the
 * shadow, and a write updates the shadow then rewrites only the touched 64 KB
 * sector(s) (erase + page program). The PUF helper data lives in its own top
 * sector.
 *
 * SAFETY: the wolfBoot partitions on this board occupy up to ~0x00E10000 of the
 * 16 MB flash. NV and the PUF helper live in the top two sectors (0x00FE0000,
 * 0x00FF0000) and a hard runtime guard refuses any erase/program below
 * FWTPM_QSPI_MIN_SAFE (0x00F00000), so a miscomputed address can never touch
 * the boot image.
 *
 * The QSPI controller access is ported from the wolfBoot Zynq-7000 HAL
 * (hal/zynq7000.c, also wolfSSL Inc.): I/O mode for commands/erase/program and
 * Linear/XIP mode (0xFC000000+) for bulk reads.
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

#ifdef FWTPM_NV_QSPI

#include <wolftpm/fwtpm/fwtpm.h>
#include <wolftpm/fwtpm/fwtpm_nv.h>

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "zynq7000.h"
#include "zynq_time.h"

/* ---- QSPI controller registers (Zynq-7000 "Linear/Static" QSPI, UG585 ch.12;
 * facts from the wolfBoot HAL). ---- */
#define QSPI_REG(off)   (*(volatile uint32_t*)(uintptr_t)(ZYNQ_QSPI_BASE + (off)))
#define QSPI_CR         QSPI_REG(0x00)
#define QSPI_ISR        QSPI_REG(0x04)
#define QSPI_IDR        QSPI_REG(0x0C)
#define QSPI_EN         QSPI_REG(0x14)
#define QSPI_TXD0       QSPI_REG(0x1C)
#define QSPI_RXD        QSPI_REG(0x20)
#define QSPI_TXTHR      QSPI_REG(0x28)
#define QSPI_RXTHR      QSPI_REG(0x2C)
#define QSPI_TXD1       QSPI_REG(0x80)
#define QSPI_TXD2       QSPI_REG(0x84)
#define QSPI_TXD3       QSPI_REG(0x88)
#define QSPI_LQSPI_CR   QSPI_REG(0xA0)

#define QSPI_LINEAR_BASE    0xFC000000UL   /* XIP window for linear-mode reads */

/* CR bits. */
#define CR_IFMODE       0x80000000U
#define CR_HOLD_B       0x00080000U
#define CR_SSFORCE      0x00004000U
#define CR_PCS_NONE     0x00003C00U
#define CR_PCS_CS0      0x00003800U
#define CR_FIFO_WIDTH   0x000000C0U
#define CR_BAUD_DIV_4   0x00000008U
#define CR_BAUD_DIV_8   0x00000010U
#define CR_MSTREN       0x00000001U

#define ISR_RXNEMPTY    0x00000010U
#define ISR_MASK        0x0000007DU
#define EN_VAL          0x00000001U

/* SPI NOR commands / status. */
#define SPI_CMD_RDID            0x9F
#define SPI_CMD_RDSR            0x05
#define SPI_CMD_WREN            0x06
#define SPI_CMD_PAGE_PROGRAM    0x02
#define SPI_CMD_SECTOR_ERASE    0xD8   /* 64 KB */
#define SPI_STATUS_WIP          0x01
#define SPI_STATUS_WEL          0x02
#define SPI_NOR_PAGE_SIZE       256U
#define SPI_NOR_SECTOR_SIZE     0x10000U

/* ---- NV / PUF-helper flash layout (top of the 16 MB flash) ---- */
#define FWTPM_QSPI_NV_BASE      0x00FE0000UL   /* NV journal sector (64 KB) */
#define FWTPM_QSPI_PUF_BASE     0x00FF0000UL   /* PUF helper-data sector (64 KB) */
#define FWTPM_QSPI_MIN_SAFE     0x00F00000UL   /* refuse writes below this */
#define FWTPM_QSPI_MAX_SAFE     0x01000000UL   /* ...and at/above this (16 MB top) */
#define FWTPM_NV_SIZE           SPI_NOR_SECTOR_SIZE   /* 64 KB NV region */

/* Bound the WIP (write-in-progress) poll so a stuck flash/controller cannot hang
 * the fwTPM forever. A 64 KB sector erase is well under this. */
#ifndef FWTPM_QSPI_WIP_TIMEOUT_MS
#define FWTPM_QSPI_WIP_TIMEOUT_MS   5000U
#endif

/* ---- low-level QSPI I/O (ported from wolfBoot hal/zynq7000.c) ---- */
static void qspi_drain_rxfifo(void)
{
    while (QSPI_ISR & ISR_RXNEMPTY) {
        (void)QSPI_RXD;
    }
}

static void qspi_cs_assert(void)
{
    QSPI_CR = (QSPI_CR & ~CR_PCS_NONE) | CR_PCS_CS0;
}

static void qspi_cs_release(void)
{
    QSPI_CR |= CR_PCS_NONE;
}

/* Clock up to 4 bytes: the byte count is encoded by which TXDn register is
 * written (TXD0 for 4 or when reading, TXD1/2/3 for 1/2/3 tx-only bytes). */
static void qspi_xfer4(const uint8_t* tx, uint8_t* rx, unsigned int nbytes)
{
    uint32_t txw = 0xFFFFFFFFU;
    uint32_t rxw;
    unsigned int i;

    if (nbytes > 4) {
        nbytes = 4;
    }
    if (nbytes == 0) {
        return;
    }
    if (tx != NULL) {
        for (i = 0; i < nbytes; i++) {
            txw &= ~((uint32_t)0xFFU << (i * 8));
            txw |= ((uint32_t)tx[i]) << (i * 8);
        }
    }
    qspi_drain_rxfifo();
    if (rx != NULL || nbytes == 4) {
        QSPI_TXD0 = txw;
    }
    else {
        switch (nbytes) {
            case 1: QSPI_TXD1 = txw; break;
            case 2: QSPI_TXD2 = txw; break;
            case 3: QSPI_TXD3 = txw; break;
            default: QSPI_TXD0 = txw; break;
        }
    }
    while (!(QSPI_ISR & ISR_RXNEMPTY)) {
        /* wait for the RX word */
    }
    rxw = QSPI_RXD;
    if (rx != NULL) {
        for (i = 0; i < nbytes; i++) {
            rx[i] = (uint8_t)(rxw >> (i * 8));
        }
    }
}

static int qspi_xfer(const uint8_t* tx, uint8_t* rx, unsigned int len)
{
    unsigned int off = 0;
    unsigned int chunk;

    qspi_cs_assert();
    while (off < len) {
        chunk = len - off;
        if (chunk > 4) {
            chunk = 4;
        }
        qspi_xfer4((tx != NULL) ? &tx[off] : NULL,
                   (rx != NULL) ? &rx[off] : NULL, chunk);
        off += chunk;
    }
    qspi_cs_release();
    return 0;
}

static void qspi_io_mode_setup(void)
{
    QSPI_EN  = 0;
    QSPI_IDR = ISR_MASK;
    qspi_drain_rxfifo();
    QSPI_ISR = ISR_MASK;
    QSPI_LQSPI_CR = 0;             /* leave linear mode */
    QSPI_TXTHR = 1;
    QSPI_RXTHR = 1;
    QSPI_CR  = CR_IFMODE | CR_HOLD_B | CR_SSFORCE | CR_PCS_NONE
             | CR_FIFO_WIDTH | CR_BAUD_DIV_8 | CR_MSTREN;
    QSPI_EN  = EN_VAL;
}

static void qspi_linear_mode_setup(void)
{
    QSPI_EN  = 0;
    QSPI_IDR = ISR_MASK;
    qspi_drain_rxfifo();
    QSPI_ISR = ISR_MASK;
    QSPI_CR  = CR_IFMODE | CR_HOLD_B | CR_SSFORCE | CR_PCS_CS0
             | CR_FIFO_WIDTH | CR_BAUD_DIV_4 | CR_MSTREN;
    QSPI_LQSPI_CR = 0x8000010BU;   /* single-bit FAST_READ (0x0B), 1 dummy */
    QSPI_EN  = EN_VAL;
}

static int spi_flash_read_id(uint8_t out[3])
{
    uint8_t cmd[4] = { SPI_CMD_RDID, 0, 0, 0 };
    uint8_t rx[4]  = { 0, 0, 0, 0 };
    int rc = qspi_xfer(cmd, rx, sizeof(cmd));
    if (rc == 0) {
        out[0] = rx[1];
        out[1] = rx[2];
        out[2] = rx[3];
    }
    return rc;
}

static int spi_flash_status(uint8_t* status)
{
    uint8_t cmd[2] = { SPI_CMD_RDSR, 0 };
    uint8_t rx[2]  = { 0, 0 };
    int rc = qspi_xfer(cmd, rx, sizeof(cmd));
    if (rc == 0) {
        *status = rx[1];
    }
    return rc;
}

static int spi_flash_wait_ready(void)
{
    uint8_t status = 0xFF;
    uint64_t deadline = zynq_millis() + FWTPM_QSPI_WIP_TIMEOUT_MS;

    do {
        if (spi_flash_status(&status) != 0) {
            return -1;
        }
        if (zynq_millis() >= deadline) {
            /* WIP stuck: fail with a distinct code rather than hang forever, so
             * the caller can surface the error and continue degraded. */
            return -2;
        }
    } while ((status & SPI_STATUS_WIP) != 0);
    return 0;
}

static int spi_flash_write_enable(void)
{
    uint8_t cmd = SPI_CMD_WREN;
    uint8_t status = 0;
    int rc;

    rc = qspi_xfer(&cmd, NULL, 1);
    if (rc != 0) {
        return rc;
    }
    if (spi_flash_status(&status) != 0) {
        return -1;
    }
    if ((status & SPI_STATUS_WEL) == 0) {
        return -1;
    }
    return 0;
}

/* Hard guard: the whole [address, address+len) span must lie within the reserved
 * NV/PUF window [MIN_SAFE, MAX_SAFE) - never the boot region below, never past
 * the device. The len check (with an overflow-safe upper bound) blocks an
 * erase/program from spilling out of the window if a layout constant changes. */
static int qspi_addr_safe(uint32_t address, uint32_t len)
{
    if (address < FWTPM_QSPI_MIN_SAFE) {
        return 0;
    }
    if (len == 0U || len > (FWTPM_QSPI_MAX_SAFE - FWTPM_QSPI_MIN_SAFE)) {
        return 0;
    }
    if (address > FWTPM_QSPI_MAX_SAFE - len) {
        return 0;
    }
    return 1;
}

static int spi_flash_sector_erase(uint32_t address)
{
    uint8_t cmd[4];
    int rc;

    if (!qspi_addr_safe(address, SPI_NOR_SECTOR_SIZE)) {
        return -1;
    }
    rc = spi_flash_write_enable();
    if (rc != 0) {
        return rc;
    }
    cmd[0] = SPI_CMD_SECTOR_ERASE;
    cmd[1] = (uint8_t)((address >> 16) & 0xFFU);
    cmd[2] = (uint8_t)((address >>  8) & 0xFFU);
    cmd[3] = (uint8_t)((address >>  0) & 0xFFU);
    rc = qspi_xfer(cmd, NULL, sizeof(cmd));
    if (rc != 0) {
        return rc;
    }
    return spi_flash_wait_ready();
}

static int spi_flash_page_program(uint32_t address, const uint8_t* data,
    unsigned int len)
{
    uint8_t hdr[4];
    unsigned int off;
    unsigned int chunk;
    int rc;

    if (len == 0 || len > SPI_NOR_PAGE_SIZE) {
        return -1;
    }
    if (!qspi_addr_safe(address, len)) {
        return -1;
    }
    rc = spi_flash_write_enable();
    if (rc != 0) {
        return rc;
    }
    hdr[0] = SPI_CMD_PAGE_PROGRAM;
    hdr[1] = (uint8_t)((address >> 16) & 0xFFU);
    hdr[2] = (uint8_t)((address >>  8) & 0xFFU);
    hdr[3] = (uint8_t)((address >>  0) & 0xFFU);

    qspi_cs_assert();
    qspi_xfer4(hdr, NULL, 4);
    off = 0;
    while (off < len) {
        chunk = len - off;
        if (chunk > 4) {
            chunk = 4;
        }
        qspi_xfer4(&data[off], NULL, chunk);
        off += chunk;
    }
    qspi_cs_release();

    return spi_flash_wait_ready();
}

/* Bulk read via Linear/XIP mode: word reads from 0xFC000000+addr, decomposed to
 * bytes so any address/length alignment works. Restores I/O mode after. */
static int spi_flash_read(uint32_t address, uint8_t* data, unsigned int len)
{
    const volatile uint32_t* xipw;
    uint32_t aligned;
    uint32_t w;
    unsigned int byteOff;
    unsigned int i;

    if (len == 0) {
        return 0;
    }
    qspi_linear_mode_setup();

    aligned = address & ~3U;
    byteOff = address & 3U;
    xipw = (const volatile uint32_t*)(uintptr_t)(QSPI_LINEAR_BASE + aligned);
    (void)xipw[0];   /* prime the controller pipeline */

    i = 0;
    if (byteOff != 0) {
        w = *xipw++;
        for (; byteOff < 4U && i < len; byteOff++, i++) {
            data[i] = (uint8_t)(w >> (byteOff * 8U));
        }
    }
    while (i + 4U <= len) {
        w = *xipw++;
        data[i++] = (uint8_t)(w >>  0);
        data[i++] = (uint8_t)(w >>  8);
        data[i++] = (uint8_t)(w >> 16);
        data[i++] = (uint8_t)(w >> 24);
    }
    if (i < len) {
        w = *xipw;
        for (byteOff = 0; i < len; byteOff++, i++) {
            data[i] = (uint8_t)(w >> (byteOff * 8U));
        }
    }

    qspi_io_mode_setup();
    return 0;
}

/* Erase a 64 KB sector then program it from the given buffer (must be one full
 * SPI_NOR_SECTOR_SIZE). */
static int spi_flash_rewrite_sector(uint32_t base, const uint8_t* buf)
{
    unsigned int off;
    int rc;

    rc = spi_flash_sector_erase(base);
    if (rc != 0) {
        return rc;
    }
    for (off = 0; off < SPI_NOR_SECTOR_SIZE; off += SPI_NOR_PAGE_SIZE) {
        rc = spi_flash_page_program(base + off, &buf[off], SPI_NOR_PAGE_SIZE);
        if (rc != 0) {
            return rc;
        }
    }
    return 0;
}

/* ---- FWTPM_NV_HAL: RAM shadow + write-through of the touched sector ---- */
static uint8_t g_nv[FWTPM_NV_SIZE];

static int nv_in_bounds(word32 offset, word32 size)
{
    return (size <= FWTPM_NV_SIZE && offset <= FWTPM_NV_SIZE - size);
}

static int nv_read(void* halCtx, word32 offset, byte* buf, word32 size)
{
    (void)halCtx;
    if (!nv_in_bounds(offset, size)) {
        return -1;
    }
    memcpy(buf, &g_nv[offset], size);
    return 0;
}

/* NV is one 64 KB sector, so any write rewrites the whole sector from the
 * updated shadow. Writes are infrequent (NV_DefineSpace / NV_Write), so the
 * full-sector rewrite is acceptable and keeps the mapping trivial. */
static int nv_flush(void)
{
    return spi_flash_rewrite_sector(FWTPM_QSPI_NV_BASE, g_nv);
}

static int nv_write(void* halCtx, word32 offset, const byte* buf, word32 size)
{
    (void)halCtx;
    if (!nv_in_bounds(offset, size)) {
        return -1;
    }
    memcpy(&g_nv[offset], buf, size);
    return nv_flush();
}

static int nv_erase(void* halCtx, word32 offset, word32 size)
{
    (void)halCtx;
    if (!nv_in_bounds(offset, size)) {
        return -1;
    }
    memset(&g_nv[offset], 0xFF, size);
    return nv_flush();
}

int FWTPM_NV_QSPI_Init(FWTPM_NV_HAL* hal)
{
    uint8_t id[3];

    qspi_io_mode_setup();

    if (spi_flash_read_id(id) == 0) {
        printf("QSPI NV: flash JEDEC ID %02X %02X %02X, NV @ 0x%06lX (64 KB)\r\n",
            id[0], id[1], id[2], (unsigned long)FWTPM_QSPI_NV_BASE);
    }

    /* Load the NV region from flash into the shadow. A blank (erased) region
     * reads 0xFF, which the wolfTPM core treats as unformatted (formats on
     * first boot). */
    if (spi_flash_read(FWTPM_QSPI_NV_BASE, g_nv, FWTPM_NV_SIZE) != 0) {
        return -1;
    }

    hal->read    = nv_read;
    hal->write   = nv_write;
    hal->erase   = nv_erase;
    hal->ctx     = NULL;
    hal->maxSize = FWTPM_NV_SIZE;
    return 0;
}

/* ---- Persistent SRAM-PUF helper-data store (overrides the weak no-ops in
 * fwtpm_puf.c). Record at the top of the PUF sector:
 *   [0..3]  magic 'P','U','F','1'
 *   [4..7]  profileId (LE)
 *   [8..11] helperLen (LE)
 *   [12..]  helper bytes
 * ---- */
#define PUF_MAGIC0  'P'
#define PUF_MAGIC1  'U'
#define PUF_MAGIC2  'F'
#define PUF_MAGIC3  '1'
#define PUF_HDR_LEN 12U

int fwtpm_puf_helper_load(unsigned char* helper, unsigned int helperSz,
    unsigned int* profileId)
{
    uint8_t hdr[PUF_HDR_LEN];
    uint32_t storedLen;

    if (helper == NULL || profileId == NULL) {
        return -1;
    }
    if (spi_flash_read(FWTPM_QSPI_PUF_BASE, hdr, PUF_HDR_LEN) != 0) {
        return -1;
    }
    if (hdr[0] != PUF_MAGIC0 || hdr[1] != PUF_MAGIC1 ||
        hdr[2] != PUF_MAGIC2 || hdr[3] != PUF_MAGIC3) {
        return -1;   /* no record (blank/erased sector) -> caller enrolls */
    }
    *profileId = (uint32_t)hdr[4] | ((uint32_t)hdr[5] << 8) |
                 ((uint32_t)hdr[6] << 16) | ((uint32_t)hdr[7] << 24);
    storedLen  = (uint32_t)hdr[8] | ((uint32_t)hdr[9] << 8) |
                 ((uint32_t)hdr[10] << 16) | ((uint32_t)hdr[11] << 24);
    if (storedLen != helperSz) {
        return -1;   /* profile/size mismatch -> re-enroll */
    }
    if (spi_flash_read(FWTPM_QSPI_PUF_BASE + PUF_HDR_LEN, helper, helperSz)
            != 0) {
        return -1;
    }
    return 0;
}

int fwtpm_puf_helper_store(const unsigned char* helper, unsigned int helperSz,
    unsigned int profileId)
{
    static uint8_t sector[SPI_NOR_SECTOR_SIZE];

    if (helper == NULL || (PUF_HDR_LEN + helperSz) > SPI_NOR_SECTOR_SIZE) {
        return -1;
    }
    memset(sector, 0xFF, sizeof(sector));
    sector[0] = PUF_MAGIC0; sector[1] = PUF_MAGIC1;
    sector[2] = PUF_MAGIC2; sector[3] = PUF_MAGIC3;
    sector[4] = (uint8_t)(profileId & 0xFF);
    sector[5] = (uint8_t)((profileId >> 8) & 0xFF);
    sector[6] = (uint8_t)((profileId >> 16) & 0xFF);
    sector[7] = (uint8_t)((profileId >> 24) & 0xFF);
    sector[8]  = (uint8_t)(helperSz & 0xFF);
    sector[9]  = (uint8_t)((helperSz >> 8) & 0xFF);
    sector[10] = (uint8_t)((helperSz >> 16) & 0xFF);
    sector[11] = (uint8_t)((helperSz >> 24) & 0xFF);
    memcpy(&sector[PUF_HDR_LEN], helper, helperSz);

    return spi_flash_rewrite_sector(FWTPM_QSPI_PUF_BASE, sector);
}

#endif /* FWTPM_NV_QSPI */
