/* mbv_uart.c
 *
 * Minimal polled AXI UARTLite console driver for the MicroBlaze V soft core.
 *
 * AXI UARTLite register map (offsets from the controller base, PG142):
 *   0x00  RX FIFO   (read received byte)
 *   0x04  TX FIFO   (write byte to transmit)
 *   0x08  STAT      status
 *   0x0C  CTRL      control (FIFO resets, interrupt enable)
 *
 * STAT bits: RX_VALID (bit0) = RX FIFO has data, TX_FULL (bit3) = TX FIFO full.
 * CTRL bits: RST_TX (bit0), RST_RX (bit1). Baud is fixed by the bitstream.
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

#include "mbv_uart.h"

#define UL_RX_FIFO      0x00u
#define UL_TX_FIFO      0x04u
#define UL_STAT         0x08u
#define UL_CTRL         0x0Cu

#define UL_STAT_RX_VALID 0x00000001u
#define UL_STAT_TX_FULL  0x00000008u

#define UL_CTRL_RST_TX   0x00000001u
#define UL_CTRL_RST_RX   0x00000002u

static volatile uint32_t* ul_reg(uintptr_t base, uint32_t offset)
{
    return (volatile uint32_t*)(base + offset);
}

void mbv_uart_init(uintptr_t base)
{
    /* Clear both FIFOs (the reset bits are self-clearing). */
    *ul_reg(base, UL_CTRL) = UL_CTRL_RST_TX | UL_CTRL_RST_RX;
}

void mbv_uart_putc(uintptr_t base, char c)
{
    while ((*ul_reg(base, UL_STAT) & UL_STAT_TX_FULL) != 0u) {
        /* wait for TX FIFO space */
    }
    *ul_reg(base, UL_TX_FIFO) = (uint32_t)(uint8_t)c;
}

void mbv_uart_write(uintptr_t base, const uint8_t* buf, size_t len)
{
    size_t i;

    if (buf == NULL) {
        return;
    }
    for (i = 0; i < len; i++) {
        mbv_uart_putc(base, (char)buf[i]);
    }
}

void mbv_uart_puts(uintptr_t base, const char* str)
{
    if (str == NULL) {
        return;
    }
    while (*str != '\0') {
        mbv_uart_putc(base, *str);
        str++;
    }
}

int mbv_uart_getc(uintptr_t base, uint8_t* out)
{
    if (out == NULL) {
        return 0;
    }
    if ((*ul_reg(base, UL_STAT) & UL_STAT_RX_VALID) == 0u) {
        return 0;
    }
    *out = (uint8_t)(*ul_reg(base, UL_RX_FIFO) & 0xFFu);
    return 1;
}
