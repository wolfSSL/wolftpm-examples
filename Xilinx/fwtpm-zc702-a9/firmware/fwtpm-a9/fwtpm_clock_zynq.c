/* fwtpm_clock_zynq.c
 *
 * FWTPM_CLOCK_HAL implementation for the Zynq-7000 Cortex-A9. Uses the MPCore
 * 64-bit Global Timer for monotonic millisecond time, and the A9 PMU cycle
 * counter for the high-resolution timestamp that wolfCrypt's MemUse entropy
 * source samples via CUSTOM_ENTROPY_TIMEHIRES.
 *
 * The Global Timer is a true 64-bit up-counter, so unlike the ZCU102 R5 port
 * (32-bit TTC + software accumulator) no wrap handling is needed here.
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

#include "zynq_time.h"

/* FWTPM_CLOCK_HAL get_ms callback: milliseconds since power-on. */
static UINT64 fwtpm_zynq_get_ms(void* halCtx)
{
    (void)halCtx;
    return (UINT64)zynq_millis();
}

/* Enable the PMU cycle counter (entropy time source) and the Global Timer
 * (monotonic ms), then register the clock HAL. Called from main() before
 * FWTPM_Init(), so the timer is live before the first RNG use. */
int FWTPM_Clock_ZYNQ_Init(FWTPM_CTX* ctx)
{
    zynq_pmu_init();
    zynq_timer_init();
    return FWTPM_Clock_SetHAL(ctx, fwtpm_zynq_get_ms, (void*)0);
}

/* XSLEEP_MS shim (see user_settings.h): busy-wait ms on the Global Timer. */
void fwtpm_sleep_ms(unsigned int ms)
{
    zynq_delay_ms((uint32_t)ms);
}

/* High-resolution time source for MemUse entropy (see user_settings.h). Returns
 * the A9 PMU cycle counter, which advances every CPU clock (~667 MHz). MemUse
 * conditions this jitter through SHA3-256 and gates it behind SP800-90B health
 * tests; this only supplies the raw timestamp. */
unsigned long long fwtpm_entropy_timer(void)
{
    return (unsigned long long)zynq_cycle_count();
}
