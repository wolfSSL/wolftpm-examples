/* fwtpm_clock_mbv.c
 *
 * FWTPM_CLOCK_HAL implementation for the MicroBlaze V soft core (SCU35). Uses
 * AXI Timer 0 for monotonic millisecond time, and the raw AXI Timer counter as
 * the high-resolution timestamp that wolfCrypt's MemUse entropy source samples
 * via CUSTOM_ENTROPY_TIMEHIRES.
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

#include <stdint.h>

#include "scu35_board.h"
#include "mbv_time.h"

/* AXI Timer 0 counter register (TCR0), read directly for the entropy timer. */
#define MBV_TCR0    (*(volatile uint32_t*)(SCU35_TIMER0_BASE + 0x08u))

static UINT64 fwtpm_mbv_get_ms(void* halCtx)
{
    (void)halCtx;
    return (UINT64)mbv_millis();
}

int FWTPM_Clock_MBV_Init(FWTPM_CTX* ctx)
{
    mbv_timer_init();
    return FWTPM_Clock_SetHAL(ctx, fwtpm_mbv_get_ms, (void*)0);
}

/* XSLEEP_MS shim (see user_settings.h). */
void fwtpm_sleep_ms(unsigned int ms)
{
    mbv_delay_ms((uint32_t)ms);
}

/* High-resolution time source for MemUse entropy: the free-running AXI Timer
 * counter, advancing every AXI clock (~225 MHz). MemUse conditions this jitter
 * through SHA3-256 and gates it behind SP800-90B health tests. */
unsigned long long fwtpm_entropy_timer(void)
{
    return (unsigned long long)MBV_TCR0;
}
