/* main.c
 *
 * Hello-world over CoreUARTapb with an LED heartbeat on the
 * Mi-V RV32 soft core (PolarFire MPF300 Splash Kit). Proves the FPGA soft-core
 * platform, the console UART, GPIO and the machine timer before layering
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

#include "miv_board.h"
#include "miv_uart.h"
#include "miv_gpio.h"
#include "miv_time.h"

/* Direct UART output helpers. This bring-up build avoids newlib printf so the
 * image fits the stock 16 KB MIV_RV32 AHB window (no FPGA widen required - the
 * wolfCrypt and fwTPM builds need the widened 512 KB window, hello does not). */
static void uputs(const char* s)
{
    while (*s != '\0') {
        miv_uart_putc(MIV_COREUARTAPB0_BASE, *s++);
    }
}

static void uputu(uint32_t v)
{
    char buf[10];
    int i = 0;

    if (v == 0U) {
        miv_uart_putc(MIV_COREUARTAPB0_BASE, '0');
        return;
    }
    while (v > 0U) {
        buf[i++] = (char)('0' + (v % 10U));
        v /= 10U;
    }
    while (i > 0) {
        miv_uart_putc(MIV_COREUARTAPB0_BASE, buf[--i]);
    }
}

static void uputhex32(uint32_t v)
{
    static const char hexd[] = "0123456789ABCDEF";
    int i;

    for (i = 28; i >= 0; i -= 4) {
        miv_uart_putc(MIV_COREUARTAPB0_BASE, hexd[(v >> i) & 0xFU]);
    }
}

int main(void)
{
    uint32_t counter = 0;
    uint32_t leds = 0x1;

    miv_uart_init(MIV_COREUARTAPB0_BASE, MIV_SYS_CLK_FREQ, MIV_CONSOLE_BAUD,
        (uint8_t)(MIV_UART_DATA_8_BITS | MIV_UART_NO_PARITY));

    miv_timer_init();

    uputs("\r\n");
    uputs("========================================================\r\n");
    uputs("  wolfSSL / wolfTPM on PolarFire MPF300 Splash Kit\r\n");
    uputs("  Mi-V RV32 soft core - Hello World\r\n");
    uputs("========================================================\r\n");
    uputs("Core clock : ");
    uputu((uint32_t)MIV_SYS_CLK_FREQ);
    uputs(" Hz\r\n");
    uputs("Console    : CoreUARTapb @ 0x");
    uputhex32((uint32_t)MIV_COREUARTAPB0_BASE);
    uputs(", ");
    uputu((uint32_t)MIV_CONSOLE_BAUD);
    uputs(" 8N1\r\n");
    uputs("Watch the LEDs walk; a heartbeat prints each second.\r\n\r\n");

    for (;;) {
        miv_gpio_set_outputs(MIV_COREGPIO_OUT_BASE, leds & 0xFU);
        leds <<= 1;
        if ((leds & 0xFU) == 0U) {
            leds = 0x1;
        }

        uputs("heartbeat ");
        uputu(counter);
        uputs(" (uptime ");
        uputu((uint32_t)miv_millis());
        uputs(" ms)\r\n");
        counter++;

        miv_delay_ms(1000);
    }

    return 0;
}
