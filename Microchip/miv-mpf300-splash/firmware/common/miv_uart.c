/* miv_uart.c
 *
 * Minimal polled CoreUARTapb console driver for the Mi-V RV32 soft core.
 *
 * CoreUARTapb register map (32-bit APB word slots):
 *   0x00  TXDATA   (write transmit byte)
 *   0x04  RXDATA   (read receive byte)
 *   0x08  CTRL1    (baud value bits [7:0])
 *   0x0C  CTRL2    (line config bits [2:0], baud value bits [12:8] in [7:3])
 *   0x10  STATUS   (bit0 TXRDY, bit1 RXFULL)
 *
 * The baud divisor is BAUD = (sys_clk / (16 * baud_rate)) - 1, a 13-bit value.
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

#include "miv_uart.h"

#define MIV_UART_TXDATA         0x00u
#define MIV_UART_RXDATA         0x04u
#define MIV_UART_CTRL1          0x08u
#define MIV_UART_CTRL2          0x0Cu
#define MIV_UART_STATUS         0x10u

#define MIV_UART_STATUS_TXRDY   0x01u
#define MIV_UART_STATUS_RXFULL  0x02u

static volatile uint32_t* miv_uart_reg(uintptr_t base, uint32_t offset)
{
    return (volatile uint32_t*)(base + offset);
}

void miv_uart_init(uintptr_t base, uint32_t sys_clk_hz, uint32_t baud,
    uint8_t line_config)
{
    uint32_t baud_value;
    uint32_t ctrl2;

    /* Out-of-range baud would underflow the divisor (giving 0xFFFFFFFF and a
     * silently mute console) or overflow 16*baud; reject it instead. */
    if (baud == 0U || baud > sys_clk_hz / 16U) {
        return;
    }

    /* 13-bit baud divisor, rounded to nearest. */
    baud_value = ((sys_clk_hz + 8U * baud) / (16U * baud)) - 1U;
    if (baud_value > 0x1FFFU) {
        baud_value = 0x1FFFU;
    }

    /* Low 8 bits into CTRL1. */
    *miv_uart_reg(base, MIV_UART_CTRL1) = baud_value & 0xFFU;

    /* High 5 bits into CTRL2[7:3], line config into CTRL2[2:0]. */
    ctrl2 = ((baud_value >> 8) & 0x1FU) << 3;
    ctrl2 |= (uint32_t)(line_config & 0x07U);
    *miv_uart_reg(base, MIV_UART_CTRL2) = ctrl2;
}

void miv_uart_putc(uintptr_t base, char c)
{
    while ((*miv_uart_reg(base, MIV_UART_STATUS) &
            MIV_UART_STATUS_TXRDY) == 0U) {
        /* wait for transmit ready */
    }
    *miv_uart_reg(base, MIV_UART_TXDATA) = (uint32_t)(uint8_t)c;
}

void miv_uart_write(uintptr_t base, const uint8_t* buf, size_t len)
{
    size_t i;

    if (buf == NULL) {
        return;
    }
    for (i = 0; i < len; i++) {
        miv_uart_putc(base, (char)buf[i]);
    }
}

void miv_uart_puts(uintptr_t base, const char* str)
{
    if (str == NULL) {
        return;
    }
    while (*str != '\0') {
        miv_uart_putc(base, *str);
        str++;
    }
}

int miv_uart_getc(uintptr_t base, uint8_t* out)
{
    if (out == NULL) {
        return 0;
    }
    if ((*miv_uart_reg(base, MIV_UART_STATUS) & MIV_UART_STATUS_RXFULL) == 0U) {
        return 0;
    }
    *out = (uint8_t)(*miv_uart_reg(base, MIV_UART_RXDATA) & 0xFFU);
    return 1;
}
