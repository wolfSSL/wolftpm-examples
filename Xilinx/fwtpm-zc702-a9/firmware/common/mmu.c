/* mmu.c
 *
 * Minimal ARMv7-A MMU setup for the Zynq-7000 Cortex-A9: a flat (identity)
 * first-level translation table using 1 MB sections. It exists so that DDR is
 * mapped as Normal memory - with the MMU disabled the A9 treats all data as
 * Strongly-Ordered, where unaligned accesses always fault (newlib's printf
 * emits unaligned stack stores, so it aborts without this). Mapping DDR as
 * Normal write-back cacheable also enables the D-cache for a large speed-up.
 *
 * Map:
 *   DDR   0x00000000-0x3FFFFFFF  Normal, write-back cacheable
 *   OCM   0xFFF00000-0xFFFFFFFF  Normal, non-cacheable (true SRAM PUF reads)
 *   else                        Device (UART/SLCR/QSPI/Global Timer/GIC MMIO)
 *
 * Called from startup.S after the stacks and .bss are set up and before the C
 * library init / main, so all C code above it runs with the MMU on.
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

#define MMU_NUM_SECTIONS    4096U

/* Short-descriptor 1 MB section attributes (AP[1:0]=11 full access; domain 0;
 * DACR sets domain 0 to manager so AP is not actually checked). Memory type is
 * (TEX[2:0],C,B):
 *   Normal WB cacheable  TEX=000 C=1 B=1  -> 0x00C
 *   Normal non-cacheable TEX=001 C=0 B=0  -> 0x1000
 *   Device               TEX=000 C=0 B=1  -> 0x004
 * plus AP=11 (0xC00) and the section type bits[1:0]=10 (0x2). */
#define MMU_SEC_TYPE        0x00000002U
#define MMU_SEC_AP_FULL     0x00000C00U
#define MMU_SEC_NORMAL_WB   (0x0000000CU | MMU_SEC_AP_FULL | MMU_SEC_TYPE)
#define MMU_SEC_NORMAL_NC   (0x00001000U | MMU_SEC_AP_FULL | MMU_SEC_TYPE)
#define MMU_SEC_DEVICE      (0x00000004U | MMU_SEC_AP_FULL | MMU_SEC_TYPE)

/* 16 KB first-level table (4096 x 4 B), 16 KB aligned as required by TTBR0. */
static uint32_t mmu_l1[MMU_NUM_SECTIONS] __attribute__((aligned(16384)));

void mmu_init(void)
{
    uint32_t i;
    uint32_t base;
    uint32_t desc;
    uint32_t sctlr;

    for (i = 0; i < MMU_NUM_SECTIONS; i++) {
        base = i << 20;
        if (i < 0x400U) {
            desc = base | MMU_SEC_NORMAL_WB;    /* DDR (1 GB) */
        }
        else if (i == 0xFFFU) {
            desc = base | MMU_SEC_NORMAL_NC;     /* OCM high (SRAM PUF source) */
        }
        else {
            desc = base | MMU_SEC_DEVICE;        /* MMIO */
        }
        mmu_l1[i] = desc;
    }

    /* DACR: domain 0 = manager (0b11), so section AP permissions are not
     * checked - appropriate for this single-privilege bare-metal image. */
    __asm__ volatile("mcr p15, 0, %0, c3, c0, 0" : : "r"(0xFFFFFFFFU));
    /* TTBCR = 0: use TTBR0 only, 16 KB table. */
    __asm__ volatile("mcr p15, 0, %0, c2, c0, 2" : : "r"(0U));
    /* TTBR0 = table base (non-cacheable table walks; table is never modified
     * after this, and it is written before the D-cache is enabled below). */
    __asm__ volatile("mcr p15, 0, %0, c2, c0, 0"
        : : "r"((uint32_t)(uintptr_t)mmu_l1));
    /* Invalidate the unified TLB. */
    __asm__ volatile("mcr p15, 0, %0, c8, c7, 0" : : "r"(0U));
    __asm__ volatile("dsb" ::: "memory");
    __asm__ volatile("isb" ::: "memory");

    /* Enable MMU (M) and D-cache (C), keep I-cache (I), and clear strict
     * alignment checking (A) so unaligned Normal-memory accesses are allowed. */
    __asm__ volatile("mrc p15, 0, %0, c1, c0, 0" : "=r"(sctlr));
    sctlr |=  (1U << 0);    /* M: MMU enable */
    sctlr |=  (1U << 2);    /* C: data cache enable */
    sctlr |=  (1U << 12);   /* I: instruction cache enable */
    sctlr &= ~(1U << 1);    /* A: alignment fault checking off */
    __asm__ volatile("mcr p15, 0, %0, c1, c0, 0" : : "r"(sctlr));
    __asm__ volatile("isb" ::: "memory");
}
