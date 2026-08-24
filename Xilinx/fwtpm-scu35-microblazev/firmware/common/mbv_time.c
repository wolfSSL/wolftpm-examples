/* mbv_time.c
 *
 * Timekeeping for the MicroBlaze V soft core using AXI Timer 0.
 *
 * AXI Timer register map (offsets from SCU35_TIMER0_BASE, PG079):
 *   0x00  TCSR0   control/status
 *   0x04  TLR0    load register
 *   0x08  TCR0    current counter value (read for elapsed time)
 *
 * TCSR0 bits: UDT (bit1, 0 = count up), ARHT (bit4, auto-reload), LOAD (bit5),
 * ENT (bit7, enable). We run timer 0 as a free-running 32-bit up-counter at the
 * AXI clock and extend it to 64 bits with a software accumulator. At 225 MHz the
 * 32-bit counter wraps about every 19 s, far longer than the hello heartbeat or
 * any UART idle gap between TPM commands, so no wrap is dropped.
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

#include "mbv_time.h"
#include "scu35_board.h"

#define TMR_TCSR0_OFFSET    0x00u
#define TMR_TLR0_OFFSET     0x04u
#define TMR_TCR0_OFFSET     0x08u

#define TCSR_ARHT           0x00000010u   /* auto reload/hold */
#define TCSR_LOAD           0x00000020u   /* load counter from TLR */
#define TCSR_ENT            0x00000080u   /* enable timer */

/* Software accumulator that extends the 32-bit up-counter to a monotonic 64-bit
 * tick count. Updated on every mbv_ticks() call. */
static uint64_t s_accum;
static uint32_t s_last;

static volatile uint32_t* tmr_reg(uint32_t offset)
{
    return (volatile uint32_t*)(SCU35_TIMER0_BASE + offset);
}

static uint32_t tmr_value(void)
{
    return *tmr_reg(TMR_TCR0_OFFSET);
}

void mbv_timer_init(void)
{
    *tmr_reg(TMR_TCSR0_OFFSET) = 0u;              /* disable */
    *tmr_reg(TMR_TLR0_OFFSET)  = 0u;              /* reload value 0 */
    *tmr_reg(TMR_TCSR0_OFFSET) = TCSR_LOAD;       /* load 0 into counter */
    *tmr_reg(TMR_TCSR0_OFFSET) = TCSR_ENT | TCSR_ARHT;  /* up, auto-reload, on */

    s_last  = tmr_value();
    s_accum = 0;
}

uint64_t mbv_ticks(void)
{
    uint32_t now;
    uint32_t delta;

    now   = tmr_value();
    delta = (uint32_t)(now - s_last);   /* up-counter: elapsed since s_last */
    s_last = now;
    s_accum += (uint64_t)delta;

    return s_accum;
}

uint64_t mbv_millis(void)
{
    return mbv_ticks() / (uint64_t)(SCU35_SYS_CLK_FREQ / 1000U);
}

void mbv_delay_us(uint32_t us)
{
    uint32_t start = tmr_value();
    uint32_t ticks = (uint32_t)(((uint64_t)us * (uint64_t)SCU35_SYS_CLK_FREQ)
                                / 1000000U);

    while ((uint32_t)(tmr_value() - start) < ticks) {
        /* busy wait */
    }
}

void mbv_delay_ms(uint32_t ms)
{
    uint32_t start = tmr_value();
    uint32_t ticks = (uint32_t)(((uint64_t)ms * (uint64_t)SCU35_SYS_CLK_FREQ)
                                / 1000U);

    while ((uint32_t)(tmr_value() - start) < ticks) {
        /* busy wait */
    }
}
