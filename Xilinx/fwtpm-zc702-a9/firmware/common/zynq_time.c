/* zynq_time.c
 *
 * Timekeeping for the Zynq-7000 Cortex-A9.
 *
 * MPCore Global Timer register map (offsets from ZYNQ_GLOBAL_TIMER_BASE, the
 * SCU private-peripheral region; UG585 ch. "Global Timer"):
 *   0x00  COUNTER0   lower 32 bits of the 64-bit up-counter
 *   0x04  COUNTER1   upper 32 bits
 *   0x08  CONTROL    bit0 Timer Enable, bit3 Auto-increment, [15:8] prescaler
 *   0x0C  ISR        interrupt status
 *
 * The Global Timer is a free-running 64-bit up-counter shared by both A9 cores,
 * clocked at ZYNQ_GLOBAL_TIMER_FREQ. A correct 64-bit read reads the high word,
 * the low word, then the high word again and retries if the high word changed.
 *
 * The A9 PMU cycle counter (PMCCNTR, CP15 c9) provides a fast free-running
 * counter for entropy timing; it is enabled once and read via MRC.
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

#include "zynq_time.h"
#include "zynq7000.h"

#define GT_COUNTER0     0x00u
#define GT_COUNTER1     0x04u
#define GT_CONTROL      0x08u

#define GT_CTRL_ENABLE  0x01u

static volatile uint32_t* gt_reg(uint32_t offset)
{
    return (volatile uint32_t*)(ZYNQ_GLOBAL_TIMER_BASE + offset);
}

void zynq_timer_init(void)
{
    /* Enable the Global Timer (harmless if the FSBL already started it):
     * no prescaler, no auto-increment, no interrupt. */
    *gt_reg(GT_CONTROL) = GT_CTRL_ENABLE;
}

uint64_t zynq_global_ticks(void)
{
    uint32_t hi, lo, hi2;

    do {
        hi  = *gt_reg(GT_COUNTER1);
        lo  = *gt_reg(GT_COUNTER0);
        hi2 = *gt_reg(GT_COUNTER1);
    } while (hi != hi2);

    return ((uint64_t)hi << 32) | (uint64_t)lo;
}

uint64_t zynq_millis(void)
{
    uint64_t t = zynq_global_ticks();
    /* Split whole-seconds from the remainder so we keep sub-kHz precision (exact
     * even when FREQ is not a whole number of kHz) WITHOUT the range-shortening
     * (ticks*1000) pre-multiply, which would overflow 64 bits in ~1.75 years and
     * send the monotonic clock (and UART frame-timeout deadlines) backwards. The
     * remainder is < FREQ, so remainder*1000 stays far below 2^64; the raw 64-bit
     * tick counter itself only wraps after centuries. */
    return (t / (uint64_t)ZYNQ_GLOBAL_TIMER_FREQ) * 1000u
         + ((t % (uint64_t)ZYNQ_GLOBAL_TIMER_FREQ) * 1000u)
             / (uint64_t)ZYNQ_GLOBAL_TIMER_FREQ;
}

void zynq_delay_us(uint32_t us)
{
    uint64_t start = zynq_global_ticks();
    uint64_t ticks = ((uint64_t)us * (uint64_t)ZYNQ_GLOBAL_TIMER_FREQ)
                     / 1000000u;

    while ((zynq_global_ticks() - start) < ticks) {
        /* busy wait */
    }
}

void zynq_delay_ms(uint32_t ms)
{
    uint64_t start = zynq_global_ticks();
    uint64_t ticks = ((uint64_t)ms * (uint64_t)ZYNQ_GLOBAL_TIMER_FREQ)
                     / 1000u;

    while ((zynq_global_ticks() - start) < ticks) {
        /* busy wait */
    }
}

void zynq_pmu_init(void)
{
    uint32_t val;

    /* PMCR (c9,c12,0): set E (enable, bit0), P (reset event counters, bit1),
     * C (reset cycle counter, bit2). */
    val = (1u << 0) | (1u << 1) | (1u << 2);
    __asm__ volatile("mcr p15, 0, %0, c9, c12, 0" :: "r"(val));

    /* PMCNTENSET (c9,c12,1): bit31 enables the cycle counter (PMCCNTR). */
    val = (1u << 31);
    __asm__ volatile("mcr p15, 0, %0, c9, c12, 1" :: "r"(val));
}

uint32_t zynq_cycle_count(void)
{
    uint32_t val;

    /* PMCCNTR (c9,c13,0). */
    __asm__ volatile("mrc p15, 0, %0, c9, c13, 0" : "=r"(val));
    return val;
}
