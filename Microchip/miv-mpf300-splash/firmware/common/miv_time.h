/* miv_time.h
 *
 * Timekeeping for the MIV_RV32 soft core using CoreTimer0 as a free-running
 * down-counter. This design's MIV_RV32 has no usable MTIME or mcycle counter,
 * so CoreTimer provides the time base. Call miv_timer_init() once before using
 * the clock or delays.
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

#ifndef MIV_TIME_H
#define MIV_TIME_H

#include <stdint.h>

/* Configure CoreTimer0 as a free-running time base. Call once at startup. */
void miv_timer_init(void);

/* Monotonic 64-bit tick count (CoreTimer clock = MIV_CORETIMER_CLK_FREQ).
 * Software accumulator over a 32-bit down-counter: must be called at least once
 * per counter wrap to stay monotonic. With the /128 prescaler (625 kHz) a wrap
 * is ~1.9 h, far longer than any single TPM command or idle gap, so the hello
 * heartbeat, the millisecond clock, and the fwTPM's idle UART receive spin all
 * satisfy this with wide margin. */
uint64_t miv_ticks(void);

/* Milliseconds since miv_timer_init(). */
uint64_t miv_millis(void);

/* Busy-wait delays. */
void miv_delay_ms(uint32_t ms);
void miv_delay_us(uint32_t us);

#endif /* MIV_TIME_H */
