/* main.c
 *
 * Hello-world over the Cadence UART with a Global-Timer heartbeat on the
 * Zynq-7000 Cortex-A9 (ZC702). Proves the JTAG-over-FSBL load flow, the console
 * UART, the 64-bit Global Timer and the cache/VFP bring-up before layering
 * wolfCrypt and the wolfTPM fwTPM on top.
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

#include "zynq7000.h"
#include "zynq_uart.h"
#include "zynq_time.h"

/* Direct UART output helpers. This bring-up build avoids newlib printf. */
static void uputs(const char* s)
{
    while (*s != '\0') {
        zynq_uart_putc(ZYNQ_CONSOLE_UART_BASE, *s++);
    }
}

static void uputu(uint32_t v)
{
    char buf[10];
    int i = 0;

    if (v == 0u) {
        zynq_uart_putc(ZYNQ_CONSOLE_UART_BASE, '0');
        return;
    }
    while (v > 0u) {
        buf[i++] = (char)('0' + (v % 10u));
        v /= 10u;
    }
    while (i > 0) {
        zynq_uart_putc(ZYNQ_CONSOLE_UART_BASE, buf[--i]);
    }
}

static void uputhex32(uint32_t v)
{
    static const char hexd[] = "0123456789ABCDEF";
    int i;

    for (i = 28; i >= 0; i -= 4) {
        zynq_uart_putc(ZYNQ_CONSOLE_UART_BASE, hexd[(v >> i) & 0xFu]);
    }
}

int main(void)
{
    uint32_t counter = 0;

    zynq_uart_init(ZYNQ_CONSOLE_UART_BASE);
    zynq_timer_init();

    uputs("\r\n");
    uputs("========================================================\r\n");
    uputs("  wolfSSL / wolfTPM on AMD Zynq-7000 (ZC702)\r\n");
    uputs("  Cortex-A9 bare-metal - Hello World\r\n");
    uputs("========================================================\r\n");
    uputs("Console      : Cadence UART @ 0x");
    uputhex32((uint32_t)ZYNQ_CONSOLE_UART_BASE);
    uputs(", ");
    uputu((uint32_t)ZYNQ_CONSOLE_BAUD);
    uputs(" 8N1\r\n");
    uputs("Global Timer : ");
    uputu((uint32_t)ZYNQ_GLOBAL_TIMER_FREQ);
    uputs(" Hz\r\n");
    uputs("A heartbeat prints each second.\r\n\r\n");

    for (;;) {
        uputs("heartbeat ");
        uputu(counter);
        uputs(" (uptime ");
        uputu((uint32_t)zynq_millis());
        uputs(" ms)\r\n");
        counter++;

        zynq_delay_ms(1000);
    }

    return 0;
}
