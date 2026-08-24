/* mbv_uart.h
 *
 * Minimal polled AXI UARTLite console driver for the MicroBlaze V soft core on
 * the SCU35. AXI UARTLite has a fixed baud rate set at bitstream build time
 * (115200 here), so there is no runtime baud configuration.
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

#ifndef MBV_UART_H
#define MBV_UART_H

#include <stdint.h>
#include <stddef.h>

/* Reset the UARTLite RX/TX FIFOs. Baud is fixed by the bitstream. */
void mbv_uart_init(uintptr_t base);

/* Blocking single character transmit (waits for TX FIFO space). */
void mbv_uart_putc(uintptr_t base, char c);

/* Blocking write of len bytes. */
void mbv_uart_write(uintptr_t base, const uint8_t* buf, size_t len);

/* Blocking write of a NUL-terminated string. */
void mbv_uart_puts(uintptr_t base, const char* str);

/* Non-blocking receive: returns 1 and stores a byte in *out if one is
 * available, else returns 0. */
int mbv_uart_getc(uintptr_t base, uint8_t* out);

#endif /* MBV_UART_H */
