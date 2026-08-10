/* miv_uart.h
 *
 * Minimal polled CoreUARTapb console driver for the Mi-V RV32 soft core on
 * the PolarFire MPF300 Splash Kit.
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

#ifndef MIV_UART_H
#define MIV_UART_H

#include <stdint.h>
#include <stddef.h>

/* Line configuration bits (CoreUARTapb CTRL2 low field). */
#define MIV_UART_DATA_8_BITS    0x01u
#define MIV_UART_NO_PARITY      0x00u
#define MIV_UART_EVEN_PARITY    0x02u
#define MIV_UART_ODD_PARITY     0x06u

/* Initialize the console UART at the given base for the requested baud rate
 * and line configuration. sys_clk_hz is the Mi-V core clock. */
void miv_uart_init(uintptr_t base, uint32_t sys_clk_hz, uint32_t baud,
    uint8_t line_config);

/* Blocking single character transmit (waits for TX ready). */
void miv_uart_putc(uintptr_t base, char c);

/* Blocking write of len bytes. */
void miv_uart_write(uintptr_t base, const uint8_t* buf, size_t len);

/* Blocking write of a NUL-terminated string. */
void miv_uart_puts(uintptr_t base, const char* str);

/* Non-blocking receive: returns 1 and stores a byte in *out if one is
 * available, else returns 0. */
int miv_uart_getc(uintptr_t base, uint8_t* out);

#endif /* MIV_UART_H */
