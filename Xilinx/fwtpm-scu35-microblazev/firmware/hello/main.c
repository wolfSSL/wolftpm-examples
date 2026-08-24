/* main.c
 *
 * Hello-world over the AXI UARTLite with an AXI Timer heartbeat on the
 * MicroBlaze V soft core (SCU35). Proves the FPGA soft-core platform, the
 * console UART and the timer before layering wolfCrypt and the wolfTPM fwTPM on
 * top. This bring-up image fits the stock reference design's 192 KB BRAM, as
 * does the minimal ECC-only fwTPM (FWTPM_TINY_ECC, ~190 KB) on the SYSMON TRNG
 * bitstream. Only the full RSA+ECC fwTPM (~652 KB) needs a larger-memory
 * bitstream (see fpga/README.md).
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

#include <stdint.h>

#include "scu35_board.h"
#include "mbv_uart.h"
#include "mbv_time.h"

static void uputs(const char* s)
{
    while (*s != '\0') {
        mbv_uart_putc(SCU35_CONSOLE_UART_BASE, *s++);
    }
}

static void uputu(uint32_t v)
{
    char buf[10];
    int i = 0;

    if (v == 0u) {
        mbv_uart_putc(SCU35_CONSOLE_UART_BASE, '0');
        return;
    }
    while (v > 0u) {
        buf[i++] = (char)('0' + (v % 10u));
        v /= 10u;
    }
    while (i > 0) {
        mbv_uart_putc(SCU35_CONSOLE_UART_BASE, buf[--i]);
    }
}

static void uputhex32(uint32_t v)
{
    static const char hexd[] = "0123456789ABCDEF";
    int i;

    for (i = 28; i >= 0; i -= 4) {
        mbv_uart_putc(SCU35_CONSOLE_UART_BASE, hexd[(v >> i) & 0xFu]);
    }
}

int main(void)
{
    uint32_t counter = 0;

    mbv_uart_init(SCU35_CONSOLE_UART_BASE);
    mbv_timer_init();

    uputs("\r\n");
    uputs("========================================================\r\n");
    uputs("  wolfSSL / wolfTPM on AMD Spartan UltraScale+ SCU35\r\n");
    uputs("  MicroBlaze V (RISC-V rv32imc) soft core - Hello World\r\n");
    uputs("========================================================\r\n");
    uputs("Core clock : ");
    uputu((uint32_t)SCU35_SYS_CLK_FREQ);
    uputs(" Hz\r\n");
    uputs("Console    : AXI UARTLite @ 0x");
    uputhex32((uint32_t)SCU35_CONSOLE_UART_BASE);
    uputs(", ");
    uputu((uint32_t)SCU35_CONSOLE_BAUD);
    uputs(" 8N1\r\n");
    uputs("A heartbeat prints each second.\r\n\r\n");

    for (;;) {
        uputs("heartbeat ");
        uputu(counter);
        uputs(" (uptime ");
        uputu((uint32_t)mbv_millis());
        uputs(" ms)\r\n");
        counter++;

        mbv_delay_ms(1000);
    }

    return 0;
}
