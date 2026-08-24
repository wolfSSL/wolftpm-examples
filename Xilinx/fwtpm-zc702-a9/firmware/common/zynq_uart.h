/* zynq_uart.h
 *
 * Minimal polled Cadence (Zynq-7000) UART console driver.
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

#ifndef ZYNQ_UART_H
#define ZYNQ_UART_H

#include <stdint.h>
#include <stddef.h>

/* Enable the given UART's transmitter and receiver and reset its FIFOs. The
 * prebuilt FSBL has already configured the mode (8N1) and baud generator via
 * ps7_init, so this does NOT reprogram them - it only (re)enables the paths so
 * the console works after we take the core. Use zynq_uart_set_baud() only if a
 * design needs a baud rate the FSBL did not set. */
void zynq_uart_init(uintptr_t base);

/* Program the baud generator for the requested rate from a known UART
 * reference clock (Hz). Optional; not needed when inheriting the FSBL setup. */
void zynq_uart_set_baud(uintptr_t base, uint32_t ref_clk_hz, uint32_t baud);

/* Blocking single character transmit (waits for TX FIFO space). */
void zynq_uart_putc(uintptr_t base, char c);

/* Blocking write of len bytes. */
void zynq_uart_write(uintptr_t base, const uint8_t* buf, size_t len);

/* Blocking write of a NUL-terminated string. */
void zynq_uart_puts(uintptr_t base, const char* str);

/* Non-blocking receive: returns 1 and stores a byte in *out if one is
 * available, else returns 0. */
int zynq_uart_getc(uintptr_t base, uint8_t* out);

#endif /* ZYNQ_UART_H */
