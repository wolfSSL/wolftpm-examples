/* fwtpm_nv_ram.c
 *
 * RAM-backed NV storage HAL for fwTPM on PolarFire SoC.
 * All state is lost on power cycle. Replace with FRAM driver for
 * persistent storage.
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
#include <wolftpm/fwtpm/fwtpm_nv.h>
#include <string.h>
#include <stdio.h>

#ifndef FWTPM_NV_RAM_SIZE
#define FWTPM_NV_RAM_SIZE  (64 * 1024)  /* 64 KB */
#endif

static uint8_t g_nv_ram[FWTPM_NV_RAM_SIZE];
static int g_nv_initialized = 0;

/* Note: callback signatures must match struct FWTPM_NV_HAL_S in fwtpm.h
 * exactly: ctx is the FIRST argument, then offset, buffer, size. */
/* Bounds checks use the overflow-safe form (offset > MAX ||
 * size > MAX - offset) so a wrapped (offset + size) cannot pass the
 * check and drive the memcpy/memset out of bounds. */
static int NvRamRead(void *ctx, uint32_t offset, uint8_t *buf, uint32_t size)
{
    (void)ctx;
    if (offset > FWTPM_NV_RAM_SIZE || size > FWTPM_NV_RAM_SIZE - offset)
        return -1;
    memcpy(buf, g_nv_ram + offset, size);
    return 0;
}

static int NvRamWrite(void *ctx, uint32_t offset, const uint8_t *buf,
    uint32_t size)
{
    (void)ctx;
    if (offset > FWTPM_NV_RAM_SIZE || size > FWTPM_NV_RAM_SIZE - offset)
        return -1;
    memcpy(g_nv_ram + offset, buf, size);
    return 0;
}

static int NvRamErase(void *ctx, uint32_t offset, uint32_t size)
{
    (void)ctx;
    if (offset > FWTPM_NV_RAM_SIZE || size > FWTPM_NV_RAM_SIZE - offset)
        return -1;
    memset(g_nv_ram + offset, 0xFF, size);
    return 0;
}

int FWTPM_NV_RAM_Init(FWTPM_NV_HAL *hal)
{
    if (hal == NULL)
        return -1;

    if (g_nv_initialized == 0) {
        memset(g_nv_ram, 0xFF, FWTPM_NV_RAM_SIZE);
        g_nv_initialized = 1;
    }

    hal->read    = NvRamRead;
    hal->write   = NvRamWrite;
    hal->erase   = NvRamErase;
    hal->ctx     = NULL;
    hal->maxSize = FWTPM_NV_RAM_SIZE;

    printf("fwTPM NV: RAM-backed (%u bytes, volatile)\n",
           (unsigned)FWTPM_NV_RAM_SIZE);
    return 0;
}

#endif /* WOLFTPM_FWTPM */
