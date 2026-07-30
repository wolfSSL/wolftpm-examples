/* miv_time.c
 *
 * Timekeeping for the MIV_RV32 soft core using CoreTimer0.
 *
 * This design's MIV_RV32 implements neither the memory-mapped MTIME block (a
 * load from 0x4400BFF8 faults) nor the mcycle/minstret performance counters
 * (hardwired to 0). CoreTimer0 is a 32-bit down-counter that runs freely in
 * continuous mode, reloading from its LOAD value at zero, so it serves as the
 * time base. Elapsed ticks between two reads are (prev - now) mod 2^32, which
 * is correct across one reload because the counter decrements.
 *
 * CoreTimer register map (32-bit APB word slots):
 *   0x00  LOAD      reload value (continuous mode reloads here at zero)
 *   0x04  VALUE     current down-counter value (read for elapsed time)
 *   0x08  CONTROL   bit0 enable, bit1 int-enable, bit2 mode (0 = continuous)
 *   0x0C  PRESCALE  clock divider (0 = /2, the minimum)
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

#include "miv_time.h"
#include "miv_board.h"

#define TMR_LOAD_OFFSET         0x00u
#define TMR_VALUE_OFFSET        0x04u
#define TMR_CONTROL_OFFSET      0x08u
#define TMR_PRESCALE_OFFSET     0x0Cu

#define TMR_CTRL_ENABLE         0x01u   /* continuous mode, interrupts off */

/* Software accumulator that extends the 32-bit down-counter to a monotonic
 * 64-bit tick count. Updated on every miv_ticks() call. */
static uint64_t s_accum;
static uint32_t s_last;

static volatile uint32_t* tmr_reg(uint32_t offset)
{
    return (volatile uint32_t*)(MIV_CORETIMER0_BASE + offset);
}

static uint32_t tmr_value(void)
{
    return *tmr_reg(TMR_VALUE_OFFSET);
}

void miv_timer_init(void)
{
    *tmr_reg(TMR_CONTROL_OFFSET)  = 0u;                 /* disable */
    *tmr_reg(TMR_PRESCALE_OFFSET) = MIV_CORETIMER_PRESCALE;
    *tmr_reg(TMR_LOAD_OFFSET)     = 0xFFFFFFFFu;        /* max, continuous */
    *tmr_reg(TMR_CONTROL_OFFSET)  = TMR_CTRL_ENABLE;    /* enable */

    s_last  = tmr_value();
    s_accum = 0;
}

uint64_t miv_ticks(void)
{
    uint32_t now;
    uint32_t delta;

    now   = tmr_value();
    delta = (uint32_t)(s_last - now);   /* down-counter: elapsed since s_last */
    s_last = now;
    s_accum += (uint64_t)delta;

    return s_accum;
}

uint64_t miv_millis(void)
{
    return miv_ticks() / (uint64_t)(MIV_CORETIMER_CLK_FREQ / 1000U);
}

void miv_delay_us(uint32_t us)
{
    uint32_t start;
    uint32_t ticks;

    start = tmr_value();
    ticks = (uint32_t)(((uint64_t)us * (uint64_t)MIV_CORETIMER_CLK_FREQ)
                        / 1000000U);
    while ((uint32_t)(start - tmr_value()) < ticks) {
        /* busy wait */
    }
}

void miv_delay_ms(uint32_t ms)
{
    uint32_t start;
    uint32_t ticks;

    /* One reload spans 2^32 / CLK ticks (~1.9 h at 625 kHz), so any practical
     * millisecond delay fits in a single 32-bit elapsed span. */
    start = tmr_value();
    ticks = (uint32_t)(((uint64_t)ms * (uint64_t)MIV_CORETIMER_CLK_FREQ)
                        / 1000U);
    while ((uint32_t)(start - tmr_value()) < ticks) {
        /* busy wait */
    }
}
