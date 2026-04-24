/* fwtpm_clock_stm32.c
 *
 * Generic STM32 clock HAL for fwTPM using SysTick (HAL_GetTick).
 * Returns milliseconds since boot.
 *
 * Copyright (C) 2006-2025 wolfSSL Inc.
 *
 * This file is part of wolfTPM.
 */

#include "user_settings.h"
#include "stm32h5xx_hal.h"
#include <wolftpm/fwtpm/fwtpm.h>

static UINT64 StmClockGetMs(void* ctx)
{
    (void)ctx;
    return (UINT64)HAL_GetTick();
}

int FWTPM_Clock_STM32_Init(FWTPM_CTX* ctx)
{
    return FWTPM_Clock_SetHAL(ctx, StmClockGetMs, NULL);
}
