/* zynq_time.h
 *
 * Timekeeping for the Zynq-7000 Cortex-A9 using the MPCore 64-bit Global Timer
 * as the monotonic time base, plus the A9 PMU cycle counter for a high-
 * resolution entropy timer. The A9 has no ARMv7 generic timer (CNTPCT is
 * undefined and traps), so the Global Timer is used instead.
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

#ifndef ZYNQ_TIME_H
#define ZYNQ_TIME_H

#include <stdint.h>

/* Enable and snapshot the Global Timer. Call once at startup. */
void zynq_timer_init(void);

/* Raw 64-bit Global Timer count (at ZYNQ_GLOBAL_TIMER_FREQ). */
uint64_t zynq_global_ticks(void);

/* Milliseconds since power-on (Global Timer is free-running from reset). */
uint64_t zynq_millis(void);

/* Busy-wait delays built on the Global Timer. */
void zynq_delay_ms(uint32_t ms);
void zynq_delay_us(uint32_t us);

/* Enable the A9 performance monitor cycle counter (PMCCNTR). Call once. */
void zynq_pmu_init(void);

/* 32-bit CPU cycle counter snapshot (PMCCNTR), for high-resolution entropy
 * timing. Wraps; only inter-sample deltas are meaningful. */
uint32_t zynq_cycle_count(void);

#endif /* ZYNQ_TIME_H */
