/* main.c
 *
 * wolfCrypt benchmark harness for a single Cortex-A9 of the AMD Zynq-7000
 * (ZC702). Brings up the console UART and the MPCore Global Timer, provides the
 * benchmark time source and a deterministic (bench-only) RNG seed, then runs
 * wolfCrypt's benchmark_test() and reports over the UART.
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

#include "zynq7000.h"
#include "zynq_uart.h"
#include "zynq_time.h"

#include <wolfssl/wolfcrypt/settings.h>
#include <wolfssl/wolfcrypt/wc_port.h>

extern int benchmark_test(void* args);

/* Benchmark time source: fractional seconds from the free-running 64-bit MPCore
 * Global Timer (ZYNQ_GLOBAL_TIMER_FREQ). */
double current_time(int reset)
{
    (void)reset;
    return (double)zynq_global_ticks() / (double)ZYNQ_GLOBAL_TIMER_FREQ;
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
    zynq_uart_init(ZYNQ_CONSOLE_UART_BASE);
    zynq_timer_init();

    printf("\r\n");
    printf("========================================================\r\n");
    printf("  wolfCrypt benchmark on AMD Zynq-7000 Cortex-A9 (ZC702)\r\n");
    printf("  ARMv7-A @ 667 MHz, 32-bit SP math (RSA-2048 + ECC)\r\n");
    printf("========================================================\r\n");

    (void)wolfCrypt_Init();
    benchmark_test(NULL);
    (void)wolfCrypt_Cleanup();

    printf("=== benchmark complete ===\r\n");
    for (;;) {
    }
    return 0;
}
