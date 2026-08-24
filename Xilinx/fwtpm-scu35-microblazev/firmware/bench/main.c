/* main.c
 *
 * wolfCrypt benchmark harness for the MicroBlaze V (RISC-V rv32imc) soft core on
 * the AMD Spartan UltraScale+ SCU35. Brings up the console UART and the AXI
 * Timer, provides the benchmark time source and a deterministic (bench-only)
 * RNG seed, then runs wolfCrypt's benchmark_test() and reports over the UART.
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
#include <stdio.h>

#include "scu35_board.h"
#include "mbv_uart.h"
#include "mbv_time.h"

#include <wolfssl/wolfcrypt/settings.h>
#include <wolfssl/wolfcrypt/wc_port.h>

extern int benchmark_test(void* args);

/* Benchmark time source: fractional seconds from the free-running AXI Timer.
 * mbv_ticks() is a 64-bit tick count at the AXI clock (SCU35_SYS_CLK_FREQ). */
double current_time(int reset)
{
    (void)reset;
    return (double)mbv_ticks() / (double)SCU35_SYS_CLK_FREQ;
}

/* Deterministic bench-only RNG seed (an LCG). This is NOT an entropy source and
 * must never be used to generate real keys; it only makes the benchmark's
 * key-generation and DRBG paths run reproducibly. */
int bench_seed(unsigned char* out, unsigned int sz)
{
    static uint32_t s = 0x2468ACE1u;
    unsigned int i;

    for (i = 0; i < sz; i++) {
        s = (s * 1103515245u) + 12345u;
        out[i] = (unsigned char)(s >> 16);
    }
    return 0;
}

int main(void)
{
    mbv_uart_init(SCU35_CONSOLE_UART_BASE);
    mbv_timer_init();

    printf("\r\n");
    printf("========================================================\r\n");
    printf("  wolfCrypt benchmark on AMD Spartan UltraScale+ SCU35\r\n");
    printf("  MicroBlaze V (RISC-V rv32imc) soft core @ %lu MHz\r\n",
        (unsigned long)(SCU35_SYS_CLK_FREQ / 1000000UL));
    printf("  32-bit portable-C SP math, ECC-only (no RSA)\r\n");
    printf("========================================================\r\n");

    (void)wolfCrypt_Init();
    benchmark_test(NULL);
    (void)wolfCrypt_Cleanup();

    printf("=== benchmark complete ===\r\n");
    for (;;) {
    }
    return 0;
}
