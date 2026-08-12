/* fwtpm_nv_ram.c
 *
 * Volatile RAM-backed FWTPM_NV_HAL for the Zynq-7000 A9 fwTPM bring-up. A
 * 64 KiB DDR buffer whose contents are lost across power cycles. Drop-in
 * replacement for fwtpm_nv_qspi.c (persistent) - select with the default build;
 * the wolfTPM core owns the log-structured NV journal on top of this flat store.
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

#include <wolftpm/fwtpm/fwtpm.h>
#include <wolftpm/fwtpm/fwtpm_nv.h>

#include <stdint.h>
#include <string.h>

#define NV_RAM_SIZE  0x10000U   /* 64 KiB */

static uint8_t g_nv_ram[NV_RAM_SIZE];

/* Overflow-safe bounds check: reject an oversized size first, then the offset,
 * without forming offset+size (which can wrap). */
static int nv_ram_in_bounds(word32 offset, word32 size)
{
    return (size <= NV_RAM_SIZE && offset <= NV_RAM_SIZE - size);
}

static int nv_ram_read(void* halCtx, word32 offset, byte* buf, word32 size)
{
    (void)halCtx;
    if (!nv_ram_in_bounds(offset, size)) {
        return -1;
    }
    memcpy(buf, &g_nv_ram[offset], size);
    return 0;
}

static int nv_ram_write(void* halCtx, word32 offset, const byte* buf,
                        word32 size)
{
    (void)halCtx;
    if (!nv_ram_in_bounds(offset, size)) {
        return -1;
    }
    memcpy(&g_nv_ram[offset], buf, size);
    return 0;
}

static int nv_ram_erase(void* halCtx, word32 offset, word32 size)
{
    (void)halCtx;
    if (!nv_ram_in_bounds(offset, size)) {
        return -1;
    }
    memset(&g_nv_ram[offset], 0xFF, size);
    return 0;
}

int zynq_nv_ram_init(FWTPM_NV_HAL* hal)
{
    memset(g_nv_ram, 0xFF, sizeof(g_nv_ram));
    hal->read    = nv_ram_read;
    hal->write   = nv_ram_write;
    hal->erase   = nv_ram_erase;
    hal->ctx     = NULL;
    hal->maxSize = NV_RAM_SIZE;
    return 0;
}
