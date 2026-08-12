/* zynq_uart.c
 *
 * Minimal polled Cadence (Zynq-7000) UART console driver.
 *
 * Cadence UART register map (offsets from the controller base, UG585 ch. 19):
 *   0x00  CR       Control (RXEN/TXEN, FIFO resets)
 *   0x04  MR       Mode (parity, char length, stop bits)
 *   0x18  BAUDGEN  Baud rate generator (CD divisor)
 *   0x2C  SR       Channel Status
 *   0x30  FIFO     TX / RX data FIFO (write = TX, read = RX)
 *   0x34  BDIV     Baud rate divider (BDIV, actual = ref/(CD*(BDIV+1)))
 *
 * SR bits used here: RXEMPTY (bit 1) = RX FIFO empty, TXFULL (bit 4) = TX FIFO
 * full. baud = ref_clk / (CD * (BDIV + 1)).
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

#include "zynq_uart.h"

#define ZUART_CR        0x00u
#define ZUART_MR        0x04u
#define ZUART_BAUDGEN   0x18u
#define ZUART_SR        0x2Cu
#define ZUART_FIFO      0x30u
#define ZUART_BDIV      0x34u

/* CR bits. */
#define ZUART_CR_RXRST  0x00000001u   /* reset RX FIFO (self-clearing) */
#define ZUART_CR_TXRST  0x00000002u   /* reset TX FIFO (self-clearing) */
#define ZUART_CR_RXEN   0x00000004u
#define ZUART_CR_RXDIS  0x00000008u
#define ZUART_CR_TXEN   0x00000010u
#define ZUART_CR_TXDIS  0x00000020u

/* SR bits. */
#define ZUART_SR_RXEMPTY 0x00000002u
#define ZUART_SR_TXFULL  0x00000010u

/* MR: 8 data bits, no parity, 1 stop bit (PAR field = 100b = 0x20). */
#define ZUART_MR_8N1     0x00000020u

static volatile uint32_t* zuart_reg(uintptr_t base, uint32_t offset)
{
    return (volatile uint32_t*)(base + offset);
}

void zynq_uart_init(uintptr_t base)
{
    /* Reset the FIFOs (bits self-clear), then enable TX and RX while clearing
     * the disable bits. Baud generator and mode are left as the FSBL set them. */
    *zuart_reg(base, ZUART_CR) = ZUART_CR_TXRST | ZUART_CR_RXRST;
    *zuart_reg(base, ZUART_CR) = ZUART_CR_RXEN | ZUART_CR_TXEN;
}

void zynq_uart_set_baud(uintptr_t base, uint32_t ref_clk_hz, uint32_t baud)
{
    uint32_t bestCd = 0u, bestBdiv = 0u, bestErr = 0xFFFFFFFFu;
    uint32_t bdiv, cd, actual, err;

    if (baud == 0u || ref_clk_hz == 0u) {
        return;
    }
    /* Search BDIV in [4,255] (hardware minimum 4) for the CD that lands closest
     * to the target rate: CD = ref / (baud * (BDIV+1)). */
    for (bdiv = 4u; bdiv <= 255u; bdiv++) {
        cd = ref_clk_hz / (baud * (bdiv + 1u));
        if (cd < 1u || cd > 0xFFFFu) {
            continue;
        }
        actual = ref_clk_hz / (cd * (bdiv + 1u));
        err = (actual > baud) ? (actual - baud) : (baud - actual);
        if (err < bestErr) {
            bestErr = err;
            bestCd = cd;
            bestBdiv = bdiv;
        }
    }
    if (bestCd == 0u) {
        return;
    }
    *zuart_reg(base, ZUART_CR) = ZUART_CR_TXRST | ZUART_CR_RXRST;
    *zuart_reg(base, ZUART_MR) = ZUART_MR_8N1;
    *zuart_reg(base, ZUART_BAUDGEN) = bestCd;
    *zuart_reg(base, ZUART_BDIV) = bestBdiv;
    *zuart_reg(base, ZUART_CR) = ZUART_CR_RXEN | ZUART_CR_TXEN;
}

void zynq_uart_putc(uintptr_t base, char c)
{
    while ((*zuart_reg(base, ZUART_SR) & ZUART_SR_TXFULL) != 0u) {
        /* wait for TX FIFO space */
    }
    *zuart_reg(base, ZUART_FIFO) = (uint32_t)(uint8_t)c;
}

void zynq_uart_write(uintptr_t base, const uint8_t* buf, size_t len)
{
    size_t i;

    if (buf == NULL) {
        return;
    }
    for (i = 0; i < len; i++) {
        zynq_uart_putc(base, (char)buf[i]);
    }
}

void zynq_uart_puts(uintptr_t base, const char* str)
{
    if (str == NULL) {
        return;
    }
    while (*str != '\0') {
        zynq_uart_putc(base, *str);
        str++;
    }
}

int zynq_uart_getc(uintptr_t base, uint8_t* out)
{
    if (out == NULL) {
        return 0;
    }
    if ((*zuart_reg(base, ZUART_SR) & ZUART_SR_RXEMPTY) != 0u) {
        return 0;
    }
    *out = (uint8_t)(*zuart_reg(base, ZUART_FIFO) & 0xFFu);
    return 1;
}
