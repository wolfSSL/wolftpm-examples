/* fwtpm_clock_zynqmp.c
 *
 * FWTPM_CLOCK_HAL implementation for the Cortex-R5 on ZCU102.
 * Reads a free-running ZynqMP TTC counter for monotonic ms time, and
 * provides the high-resolution PMU cycle-counter timestamp used by
 * wolfCrypt's MemUse entropy source (CUSTOM_ENTROPY_TIMEHIRES).
 *
 * The TTC is used (not the R5 PMU cycle counter) because the PMU CCNT
 * is 32-bit and wraps every ~8 s at 533 MHz, and the rpmsg server loop
 * sleeps in wfi between commands -- idle gaps longer than one wrap
 * silently lost 2^32 cycles of elapsed time. A TTC counter prescaled
 * down to a multi-day wrap, plus a 64-bit software accumulator, stays
 * monotonic across idle. The PMU CCNT is still enabled here as the
 * high-resolution timestamp for the MemUse entropy timer hook below.
 *
 * Copyright (C) 2006-2026 wolfSSL Inc.
 *
 * This file is part of wolfTPM.
 *
 * wolfTPM is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 */

#include "user_settings.h"
#include "zcu102_r5.h"
#include "include/xparameters.h"

#include <wolftpm/fwtpm/fwtpm.h>

#include <stdint.h>

/* ARMv7-R PMU access via CP15. PMUSERENR (USEREN), PMCR, PMCNTENSET,
 * PMCCNTR -- see ARM ARMv7-R / Cortex-R5 TRM. */
static inline void pmu_enable(void)
{
    uint32_t v;
    /* PMUSERENR: enable user-mode access (harmless in privileged mode). */
    __asm__ volatile("mcr p15, 0, %0, c9, c14, 0" : : "r"(1U));
    /* PMCR: bit 0 (E) enable counters, bit 2 (C) reset CCNT.
     * Also bit 3 (D) divide by 64 -- leave clear for full resolution. */
    __asm__ volatile("mrc p15, 0, %0, c9, c12, 0" : "=r"(v));
    v |= (1U << 0) | (1U << 2);
    __asm__ volatile("mcr p15, 0, %0, c9, c12, 0" : : "r"(v));
    /* PMCNTENSET: enable cycle counter (bit 31). */
    __asm__ volatile("mcr p15, 0, %0, c9, c12, 1" : : "r"(0x80000000U));
}

static inline uint32_t pmu_ccnt(void)
{
    uint32_t v;
    __asm__ volatile("mrc p15, 0, %0, c9, c13, 0" : "=r"(v));
    return v;
}

/* ------------------------------------------------------------------ */
/* Free-running TTC counter (monotonic ms time base).                 */
/* ------------------------------------------------------------------ */
/* Direct MMIO to the TTC (no XTtcPs dependency, matching the raw-CP15
 * style above). The LPD peripheral region is already mapped as device
 * memory by mpu_setup.c (QSPI/IPI/UART live in the same window).
 *
 * TTC counter 0 registers, offsets from ZCU102_TTC_BASEADDR:
 *   Clock_Control_0   0x00  bit0 PS_EN, bits1-4 PS_VAL, bit5 SRC(0=pclk)
 *   Counter_Control_0 0x0C  bit0 DIS, bit1 INT(0=overflow), bit2 DEC,
 *                           bit4 RST
 *   Counter_Value_0   0x18  current 32-bit count (ZynqMP TTC is 32-bit)
 *
 * Prescale by 2^(PS_VAL+1). PS_VAL=15 -> /65536: at a 100 MHz TTC clock
 * the count advances ~1526 Hz and the 32-bit counter wraps about every
 * 32 days. The 64-bit accumulator below covers even longer uptimes. */
#define TTC_CLK_CNTRL_OFFSET    0x00U
#define TTC_CNT_CNTRL_OFFSET    0x0CU
#define TTC_CNT_VALUE_OFFSET    0x18U

#define TTC_PS_VAL              15U
#define TTC_PRESCALE            (1UL << (TTC_PS_VAL + 1U))   /* 65536 */
#define TTC_CLK_CNTRL_VAL       (((TTC_PS_VAL) << 1) | 0x1U) /* PS_EN + PS_VAL */
#define TTC_CNT_CNTRL_STOP      0x01U   /* DIS = 1 */
#define TTC_CNT_CNTRL_RESET     0x10U   /* RST = 1, DIS = 0 */
#define TTC_CNT_CNTRL_RUN       0x00U   /* overflow mode, count up, enabled */

#define TTC_REG(off) \
    (*(volatile uint32_t*)(uintptr_t)(ZCU102_TTC_BASEADDR + (off)))

static uint32_t s_last_ttc;
static uint64_t s_ttc_total;

static void ttc_start(void)
{
    TTC_REG(TTC_CNT_CNTRL_OFFSET) = TTC_CNT_CNTRL_STOP;
    TTC_REG(TTC_CLK_CNTRL_OFFSET) = TTC_CLK_CNTRL_VAL;
    TTC_REG(TTC_CNT_CNTRL_OFFSET) = TTC_CNT_CNTRL_RESET;
    TTC_REG(TTC_CNT_CNTRL_OFFSET) = TTC_CNT_CNTRL_RUN;
}

static inline uint32_t ttc_count(void)
{
    return TTC_REG(TTC_CNT_VALUE_OFFSET);
}

uint64_t zynqmp_r5_get_ms(void* halCtx)
{
    uint32_t now = ttc_count();
    (void)halCtx;
    if (now < s_last_ttc) {
        s_ttc_total += 0x100000000ULL;
    }
    s_last_ttc = now;
    /* ms = ticks * prescale * 1000 / ttc_clk_hz. The intermediate fits
     * in 64-bit for many years of uptime at this tick rate. */
    return ((s_ttc_total + now) * TTC_PRESCALE * 1000ULL) /
           (uint64_t)ZCU102_TTC_CLK_HZ;
}

int zynqmp_r5_clock_init(FWTPM_CTX* ctx)
{
    /* Enable the PMU CCNT (high-resolution time source for the MemUse
     * entropy timer hook), and start the TTC as the monotonic ms time
     * base. pmu_enable() must run before the first RNG use; main()
     * calls zynqmp_r5_clock_init() before FWTPM_Init(). */
    pmu_enable();
    s_last_ttc = 0;
    s_ttc_total = 0;
    ttc_start();
    return FWTPM_Clock_SetHAL(ctx, zynqmp_r5_get_ms, (void*)0);
}

/* ------------------------------------------------------------------ */
/* High-resolution time source for MemUse entropy.                    */
/* ------------------------------------------------------------------ */
/* wolfCrypt's MemUse entropy source (HAVE_ENTROPY_MEMUSE) calls this
 * via CUSTOM_ENTROPY_TIMEHIRES (see user_settings.h). It samples the
 * timing of memory accesses, so the time base must be as fine-grained
 * as possible: we return the ARMv7-R PMU cycle counter (PMCCNTR),
 * which advances every CPU clock (~533 MHz). The Cortex-R5 has no ARM
 * generic timer, so wolfentropy.c's default __arm__ CNTVCT path cannot
 * be used. MemUse conditions this jitter through SHA3-256 and gates it
 * behind SP800-90B health tests; this function only supplies the raw
 * timestamp. */
uint64_t fwtpm_entropy_timer(void)
{
    return (uint64_t)pmu_ccnt();
}
