/* main.c
 *
 * Run wolfcrypt_test and benchmark_test on the Mi-V RV32 soft
 * core (PolarFire MPF300 Splash Kit). Provides the platform hooks wolfCrypt
 * needs on bare metal: a console (via common/ retarget to CoreUARTapb), a
 * benchmark clock (current_time from CoreTimer), and an RNG source.
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

#include <wolfssl/wolfcrypt/settings.h>
#include <wolfssl/wolfcrypt/wc_port.h>
#include <wolfcrypt/test/test.h>
#include <wolfcrypt/benchmark/benchmark.h>

#include <stdio.h>
#include <stdint.h>

#include "miv_board.h"
#include "miv_uart.h"
#include "miv_time.h"

/* -specs=nano.specs omits float printf; the benchmark needs it. Gate on
 * NO_CRYPT_BENCHMARK so the wolfcrypt_test-only smoke build really drops the
 * float printf (matching the Makefile, which also drops -u _printf_float). */
#ifndef NO_CRYPT_BENCHMARK
asm (".global _printf_float");
#endif

/* Benchmark clock: seconds since timer init, from CoreTimer ticks. */
double current_time(int reset)
{
    (void)reset;
    return (double)miv_ticks() / (double)MIV_CORETIMER_CLK_FREQ;
}

/* RNG source. PLACEHOLDER for bring-up only - a counter is NOT entropy. The
 * fwTPM example uses the PolarFire System Controller nonce service instead. */
static unsigned int g_rng_counter;

unsigned int my_rng_seed_gen(void)
{
    return ++g_rng_counter;
}

int main(void)
{
    int ret;

    miv_uart_init(MIV_COREUARTAPB0_BASE, MIV_SYS_CLK_FREQ, MIV_CONSOLE_BAUD,
        (uint8_t)(MIV_UART_DATA_8_BITS | MIV_UART_NO_PARITY));
    miv_timer_init();

    printf("\r\n");
    printf("========================================================\r\n");
    printf("  wolfCrypt test + benchmark on PolarFire MPF300\r\n");
    printf("  Mi-V RV32 soft core\r\n");
    printf("========================================================\r\n");

    if ((ret = wolfCrypt_Init()) != 0) {
        printf("wolfCrypt_Init failed: %d\r\n", ret);
        return ret;
    }

#ifndef NO_CRYPT_TEST
    printf("\r\n--- wolfcrypt_test ---\r\n");
    ret = wolfcrypt_test(NULL);
    printf("wolfcrypt_test result: %d\r\n", ret);
#endif

#ifndef NO_CRYPT_BENCHMARK
    printf("\r\n--- benchmark_test ---\r\n");
    benchmark_test(NULL);
#endif

    wolfCrypt_Cleanup();
    printf("\r\nDone.\r\n");

    for (;;) {
        /* idle */
    }
    return 0;
}
