/* tpm_io_i2c_c3.c
 *
 * wolfTPM IO callback driving the TPM directly from the PSOC Control C3 over
 * I2C, using wolfBoot's I2C driver.
 *
 * The MCU performs the whole TPM exchange itself; no host takes part in the
 * cryptography.
 *
 * The transfer sequence is wolfBoot's, not a rewrite. The register write is
 * followed by a stop and a guard delay rather than a repeated start, because
 * the part NAKs briefly after being addressed while it wakes and the
 * specification asks for a guard time that a repeated start cannot provide.
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
#include <stdint.h>
#include <string.h>

#include <wolftpm/tpm2.h>

#include "i2c_drv.h"
#include "i2c_trace.h"
#include "demo_util.h"

#ifndef TPM2_I2C_ADDR
#define TPM2_I2C_ADDR       0x2E    /* 7-bit TCG TIS address */
#endif
#ifndef TPM_I2C_TRIES
#define TPM_I2C_TRIES       10
#endif
#ifndef TPM_I2C_GUARD_LOOPS
#define TPM_I2C_GUARD_LOOPS 20000
#endif

static int i2c_ready;

static void tpm_i2c_guard(void)
{
    volatile uint32_t i;

    for (i = 0; i < (uint32_t)TPM_I2C_GUARD_LOOPS; i++)
        ;
}

/* used: attempts consumed, register phase in the low nibble and data phase
 * in the high nibble. Only the trace reads it. */
static int tpm_i2c_read(uint8_t reg, uint8_t* data, uint32_t len,
    uint8_t* used)
{
    uint8_t regbuf = reg;
    int tries = TPM_I2C_TRIES;
    int ret;

    do {
        ret = i2c_write(TPM2_I2C_ADDR, &regbuf, 1, 1);
        tpm_i2c_guard();
    } while ((ret != I2C_OK) && (--tries > 0));

    *used = (uint8_t)(TPM_I2C_TRIES - tries + 1);

    if (ret != I2C_OK)
        return ret;

    tries = TPM_I2C_TRIES;
    do {
        ret = i2c_read(TPM2_I2C_ADDR, data, len, 1);
        if (ret != I2C_OK)
            tpm_i2c_guard();
    } while ((ret != I2C_OK) && (--tries > 0));

    *used |= (uint8_t)((TPM_I2C_TRIES - tries + 1) << 4);

    return ret;
}

/* Each transfer is the register byte followed by payload. The TPM sizes a
 * FIFO burst from its own burst count, which can exceed what fits in one
 * transfer here, so a long write is split across several. Every chunk
 * re-addresses the same register, which is what the FIFO expects. */
static int tpm_i2c_write(uint8_t reg, const uint8_t* data, uint32_t len,
    uint8_t* used)
{
    static uint8_t buf[MAX_SPI_FRAMESIZE + 1];
    uint32_t done = 0;
    int tries = TPM_I2C_TRIES;
    int ret = I2C_OK;

    *used = 0;

    do {
        uint32_t chunk = len - done;
        if (chunk > MAX_SPI_FRAMESIZE)
            chunk = MAX_SPI_FRAMESIZE;

        buf[0] = reg;
        memcpy(&buf[1], data + done, chunk);

        tries = TPM_I2C_TRIES;
        do {
            ret = i2c_write(TPM2_I2C_ADDR, buf, chunk + 1, 1);
            if (ret != I2C_OK)
                tpm_i2c_guard();
        } while ((ret != I2C_OK) && (--tries > 0));

        if ((uint8_t)(TPM_I2C_TRIES - tries + 1) > *used)
            *used = (uint8_t)(TPM_I2C_TRIES - tries + 1);

        done += chunk;
    } while ((ret == I2C_OK) && (done < len));

    /* A command payload can carry an authValue; do not leave it behind. */
    TPM2_ForceZero(buf, sizeof(buf));
    return ret;
}

int TPM2_IoCb(TPM2_CTX* ctx, INT32 isRead, UINT32 addr, BYTE* buf,
    UINT16 size, void* userCtx)
{
    uint8_t reg = (uint8_t)(addr & 0xFF);
    uint8_t used = 0;
    uint32_t start;
    int ret;

    (void)ctx;
    (void)userCtx;

    if (!i2c_ready) {
        i2c_init();
        i2c_ready = 1;
    }

    start = demo_cycles();

    if (isRead)
        ret = tpm_i2c_read(reg, buf, (uint32_t)size, &used);
    else
        ret = tpm_i2c_write(reg, buf, (uint32_t)size, &used);

    i2c_trace_add(reg, (uint8_t)(isRead != 0), (uint8_t)(ret & 0xFF), used,
        (size > 0) ? buf[0] : 0, size, demo_cycles() - start);

    return (ret == I2C_OK) ? TPM_RC_SUCCESS : TPM_RC_FAILURE;
}
