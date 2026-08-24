/* mbv_time.h
 *
 * Timekeeping for the MicroBlaze V soft core using AXI Timer 0 as a free-running
 * 32-bit up-counter, extended to 64 bits by a software accumulator. Call
 * mbv_timer_init() once before using the clock or delays, and call mbv_ticks()
 * (or mbv_millis()) at least once per counter wrap (~19 s at 225 MHz) to stay
 * monotonic.
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

#ifndef MBV_TIME_H
#define MBV_TIME_H

#include <stdint.h>

/* Configure AXI Timer 0 as a free-running up-counter. Call once at startup. */
void mbv_timer_init(void);

/* Monotonic 64-bit tick count at SCU35_SYS_CLK_FREQ. Software accumulator over
 * the 32-bit counter: must be called at least once per counter wrap. */
uint64_t mbv_ticks(void);

/* Milliseconds since mbv_timer_init(). */
uint64_t mbv_millis(void);

/* Busy-wait delays. */
void mbv_delay_ms(uint32_t ms);
void mbv_delay_us(uint32_t us);

#endif /* MBV_TIME_H */
