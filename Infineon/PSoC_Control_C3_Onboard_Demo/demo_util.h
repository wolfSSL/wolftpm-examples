/* demo_util.h
 *
 * Console output, timing and board setup shared by the on-board demo acts.
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1335, USA
 */
#ifndef DEMO_UTIL_H_INCLUDED
#define DEMO_UTIL_H_INCLUDED

#include <stdint.h>

/* Measured on this part; see the benchmark in this directory. */
#ifndef CPU_HZ
#define CPU_HZ 180000000UL
#endif

/* Claim the TPM's interface-select strap and bring up the console. Must be
 * the first thing an application does: the TPM latches its bus from that pin
 * while its own reset is low, about a millisecond after the CPU starts. */
void demo_board_init(void);

void demo_put(const char *s);
void demo_put_u32(uint32_t v);
void demo_put_hex32(uint32_t v);
void demo_put_hexbuf(const uint8_t *b, uint32_t n);

/* Base64 three bytes at a time with no staging buffer, so a 4627 byte
 * signature can leave a part that has nowhere to hold the encoded form. */
void demo_put_b64(const uint8_t *b, uint32_t n);

/* Processor cycle counter, started by demo_board_init(). */
uint32_t demo_cycles(void);
uint32_t demo_ms_since(uint32_t startCycles);

/* Milliseconds since boot, valid only while called more often than the cycle
 * counter wraps. wolfTPM's wait loops use it as their monotonic source. */
uint32_t demo_uptime_ms(void);

/* One blocking byte from the console. */
uint8_t demo_getc(void);

/* {"event":"error","where":"...","rc":"0x..."} */
void demo_fail(const char *where, int rc);

#endif /* DEMO_UTIL_H_INCLUDED */
