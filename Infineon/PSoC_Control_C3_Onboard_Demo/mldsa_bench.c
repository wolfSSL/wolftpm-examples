/* mldsa_bench.c
 *
 * Time an ML-DSA-87 verification on the PSOC Control C3 itself.
 *
 * This answers the one question that decides whether the demo can run on the
 * board: a TPM-made post-quantum signature is only worth verifying locally if
 * the MCU can do it inside a demo beat. No TPM is involved; the signature is a
 * fixed vector generated once on a host, so the measurement is repeatable and
 * the board needs nothing attached.
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1335, USA
 */
#include <stdint.h>
#include <string.h>

#include <wolfssl/wolfcrypt/settings.h>
#include <wolfssl/wolfcrypt/wc_mldsa.h>

#include "mldsa_tv.h"

extern void uart_init(void);
extern void uart_write(const char *buf, unsigned int sz);

/* Data Watchpoint and Trace unit: a free-running cycle counter, which is the
 * only timebase this needs. The lock register must be opened first on this
 * core or the enable is ignored. */
#define DEMCR       (*(volatile uint32_t *)0xE000EDFCUL)
#define DEMCR_TRCENA (1UL << 24)
#define DWT_CTRL    (*(volatile uint32_t *)0xE0001000UL)
#define DWT_CYCCNT  (*(volatile uint32_t *)0xE0001004UL)
#define DWT_LAR     (*(volatile uint32_t *)0xE0001FB0UL)
#define DWT_CTRL_CYCCNTENA (1UL << 0)

/* SysTick, used when the trace unit is absent or locked out. It is only a
 * 24 bit down-counter, so wraps are accumulated by hand. */
#define SYST_CSR    (*(volatile uint32_t *)0xE000E010UL)
#define SYST_RVR    (*(volatile uint32_t *)0xE000E014UL)
#define SYST_CVR    (*(volatile uint32_t *)0xE000E018UL)
#define SYST_CSR_ENABLE   (1UL << 0)
#define SYST_CSR_CLKSOURCE (1UL << 2)
#define SYST_CSR_COUNTFLAG (1UL << 16)
#define SYST_RELOAD 0x00FFFFFFUL

static int use_dwt;

/* Reported alongside the cycle count so the figure can be read as time.
 * Measured at 180.17 MHz on a C3M6 by timing CLKCAL_CYCLES against a host
 * clock; note this is the core clock and not the 48 MHz peripheral clock the
 * console divider is derived from. */
#ifndef CPU_HZ
#define CPU_HZ 180000000UL
#endif

#ifndef BENCH_ITERATIONS
#define BENCH_ITERATIONS 5
#endif

/* Cycles to spin between two console markers so a host can derive the core
 * clock from the wall time between them. Large enough that serial latency is
 * noise against it. */
#ifndef CLKCAL_CYCLES
#define CLKCAL_CYCLES 100000000UL
#endif
#ifndef CLKCAL_ROUNDS
#define CLKCAL_ROUNDS 3
#endif

/* The key object carries the verification working buffers under
 * WOLFSSL_MLDSA_VERIFY_SMALLEST_MEM, so it is static rather than on the
 * stack. */
static MlDsaKey key;

static void put(const char *s)
{
    unsigned int n = 0;
    while (s[n] != 0)
        n++;
    uart_write(s, n);
}

static void put_u32(uint32_t v)
{
    char tmp[12];
    char out[12];
    int t = 0, n = 0;

    if (v == 0) {
        out[n++] = '0';
    }
    else {
        while (v > 0) {
            tmp[t++] = (char)('0' + (v % 10));
            v /= 10;
        }
        while (t > 0)
            out[n++] = tmp[--t];
    }
    uart_write(out, (unsigned int)n);
}

static int cyccnt_init(void)
{
    DWT_LAR = 0xC5ACCE55UL;             /* unlock, ignored where not needed */
    DEMCR |= DEMCR_TRCENA;
    DWT_CYCCNT = 0;
    DWT_CTRL |= DWT_CTRL_CYCCNTENA;
    /* Prove it is actually counting before trusting any measurement. Not
     * every Cortex-M33 implements the trace unit, and TrustZone can put it
     * out of reach, so fall back to SysTick rather than report nonsense. */
    if (DWT_CYCCNT != 0) {
        use_dwt = 1;
        return 0;
    }

    use_dwt = 0;
    SYST_CSR = 0;
    SYST_RVR = SYST_RELOAD;
    SYST_CVR = 0;
    SYST_CSR = SYST_CSR_CLKSOURCE | SYST_CSR_ENABLE;
    return (SYST_CVR != 0) ? 0 : -1;
}

/* Elapsed processor cycles since start_mark(), by whichever counter works. */
static uint32_t start_lo;
static uint32_t wraps;

static void start_mark(void)
{
    wraps = 0;
    if (use_dwt) {
        start_lo = DWT_CYCCNT;
    }
    else {
        (void)SYST_CSR;                 /* clear a stale COUNTFLAG */
        SYST_CVR = 0;
        start_lo = SYST_RELOAD;
    }
}

static void tick_poll(void)
{
    if (!use_dwt && (SYST_CSR & SYST_CSR_COUNTFLAG) != 0)
        wraps++;
}

static uint32_t elapsed(void)
{
    if (use_dwt)
        return DWT_CYCCNT - start_lo;
    tick_poll();
    /* SysTick counts down from the reload value. */
    return (wraps * (SYST_RELOAD + 1UL)) + (start_lo - SYST_CVR);
}

/* Spin for a known number of processor cycles, bracketed by markers. The
 * board has no wall clock of its own, so the only way to turn cycles into
 * seconds is to let something that does have one watch the markers. */
static void clock_calibrate(void)
{
    uint32_t i;
    int r;

    for (r = 0; r < CLKCAL_ROUNDS; r++) {
        put("CLKCAL begin "); put_u32(CLKCAL_CYCLES); put("\r\n");
        start_mark();
        do {
            i = elapsed();
        } while (i < CLKCAL_CYCLES);
        put("CLKCAL end "); put_u32(i); put("\r\n");
    }
}

void main(void)
{
    uint32_t cycles, best = 0xFFFFFFFFUL, worst = 0, total = 0, completed = 0;
    int res, rc, i;

    uart_init();
    put("\r\nML-DSA-87 verify benchmark (PSOC Control C3)\r\n");

    if (cyccnt_init() != 0) {
        put("FAIL: cycle counter not running\r\n");
        while (1)
            ;
    }

    put(use_dwt ? "  timebase     DWT cycle counter\r\n"
                : "  timebase     SysTick\r\n");
    clock_calibrate();

    put("  public key   "); put_u32(MLDSA_TV_PUB_LEN); put(" bytes\r\n");
    put("  signature    "); put_u32(MLDSA_TV_SIG_LEN); put(" bytes\r\n");
    put("  key object   "); put_u32((uint32_t)sizeof(MlDsaKey)); put(" bytes\r\n");

    for (i = 0; i < BENCH_ITERATIONS; i++) {
        rc = wc_MlDsaKey_Init(&key, NULL, INVALID_DEVID);
        if (rc == 0)
            rc = wc_MlDsaKey_SetParams(&key, WC_ML_DSA_87);
        if (rc == 0)
            rc = wc_MlDsaKey_ImportPubRaw(&key, mldsa_tv_pub,
                    MLDSA_TV_PUB_LEN);
        if (rc != 0) {
            put("FAIL: setup rc "); put_u32((uint32_t)-rc); put("\r\n");
            break;
        }

        res = 0;
        start_mark();
        rc = wc_MlDsaKey_VerifyCtx(&key, mldsa_tv_sig, MLDSA_TV_SIG_LEN,
                NULL, 0, mldsa_tv_msg, (word32)sizeof(mldsa_tv_msg), &res);
        cycles = elapsed();
        wc_MlDsaKey_Free(&key);

        if (rc != 0 || res != 1) {
            put("FAIL: verify rc "); put_u32((uint32_t)-rc);
            put(" res "); put_u32((uint32_t)res); put("\r\n");
            break;
        }

        put("  run "); put_u32((uint32_t)(i + 1)); put(": ");
        put_u32(cycles); put(" cycles, ");
        put_u32(cycles / (CPU_HZ / 1000UL)); put(" ms at ");
        put_u32(CPU_HZ / 1000000UL); put(" MHz\r\n");

        if (cycles < best)
            best = cycles;
        if (cycles > worst)
            worst = cycles;
        total += cycles;
        completed++;
    }

    /* Averaged over the runs that finished, not over the ones asked for: a
     * verification that fails breaks out of the loop, and dividing by the
     * full count would quietly understate the result. */
    if (completed == BENCH_ITERATIONS) {
        total /= completed;
        put("  best "); put_u32(best); put(" worst "); put_u32(worst);
        put(" mean "); put_u32(total); put(" cycles\r\n");
        put("  mean "); put_u32(total / (CPU_HZ / 1000UL)); put(" ms\r\n");
        put("PASS\r\n");
    }

    while (1)
        ;
}
