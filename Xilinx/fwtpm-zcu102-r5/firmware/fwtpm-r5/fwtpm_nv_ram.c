/* fwtpm_nv_ram.c
 *
 * Volatile RAM-backed FWTPM_NV_HAL for ZCU102 R5 V1. Mirrors the
 * Microchip PolarFire bring-up port's fwtpm_nv_ram.c -- 64 KiB DDR
 * buffer, content lost across power cycles. Drop-in replacement for
 * fwtpm_nv_qspi.c when the QSPI controller is owned by Linux.
 *
 * Copyright (C) 2006-2026 wolfSSL Inc.
 */

#include "user_settings.h"
#include "zcu102_r5.h"

#include <wolftpm/fwtpm/fwtpm.h>
#include <wolftpm/fwtpm/fwtpm_nv.h>

#include <stdint.h>
#include <string.h>

#define NV_RAM_SIZE  0x10000U   /* 64 KiB */

static uint8_t g_nv_ram[NV_RAM_SIZE];

/* Overflow-safe bounds check: reject oversized size first, then the
 * offset, without forming offset+size which can wrap (see STM32 HAL). */
static int nv_ram_in_bounds(word32 offset, word32 size)
{
    return (size <= NV_RAM_SIZE && offset <= NV_RAM_SIZE - size);
}

static int nv_ram_read(void* halCtx, word32 offset, byte* buf, word32 size)
{
    (void)halCtx;
    if (!nv_ram_in_bounds(offset, size)) return -1;
    memcpy(buf, &g_nv_ram[offset], size);
    return 0;
}

static int nv_ram_write(void* halCtx, word32 offset, const byte* buf,
                         word32 size)
{
    (void)halCtx;
    if (!nv_ram_in_bounds(offset, size)) return -1;
    memcpy(&g_nv_ram[offset], buf, size);
    return 0;
}

static int nv_ram_erase(void* halCtx, word32 offset, word32 size)
{
    (void)halCtx;
    if (!nv_ram_in_bounds(offset, size)) return -1;
    memset(&g_nv_ram[offset], 0xFF, size);
    return 0;
}

int zynqmp_r5_nv_ram_init(FWTPM_NV_HAL* hal)
{
    memset(g_nv_ram, 0xFF, sizeof(g_nv_ram));
    hal->read    = nv_ram_read;
    hal->write   = nv_ram_write;
    hal->erase   = nv_ram_erase;
    hal->ctx     = NULL;
    hal->maxSize = NV_RAM_SIZE;
    return 0;
}
