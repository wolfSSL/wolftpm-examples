/* startup.S
 *
 * Minimal bare-metal Cortex-A9 (ARMv7-A) startup for the Zynq-7000 PS. The
 * prebuilt FSBL does ps7_init (DDR, clocks, MIO/UART) and parks; this image is
 * then loaded over JTAG at ZYNQ_LOAD_ADDR and entered at _start in SVC mode
 * with IRQ/FIQ masked (the JTAG script sets pc and cpsr = 0xD3).
 *
 * _start puts the core into a known state independent of what the FSBL left
 * behind: it disables the MMU and D-cache, invalidates the caches / TLB /
 * branch predictor, then enables the I-cache and branch prediction. VFP/NEON is
 * enabled for the hard-float ABI. It sets the stacks, installs the vector table,
 * and zeroes .bss. It then calls mmu_init() (mmu.c), which installs a flat map
 * and enables the MMU and D-cache - required so newlib printf's unaligned
 * accesses do not abort (data is Strongly-Ordered while the MMU is off). There
 * is no DMA in this build, so full caching is coherency-safe. Finally it runs
 * the C library init array and calls main().
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

    .equ CPSR_MODE_FIQ, 0x11
    .equ CPSR_MODE_IRQ, 0x12
    .equ CPSR_MODE_SVC, 0x13
    .equ CPSR_MODE_ABT, 0x17
    .equ CPSR_MODE_UND, 0x1B
    .equ CPSR_MODE_SYS, 0x1F
    .equ CPSR_I_BIT,    0x80
    .equ CPSR_F_BIT,    0x40

    /* Exception vector table (VBAR points here). Reset dispatches to _start;
     * every other exception parks in a self-loop for halt-and-inspect. */
    .section .vectors, "ax"
    .globl _vectors
_vectors:
    b   _start          /* reset */
    b   _hang           /* undefined instruction */
    b   _hang           /* supervisor call */
    b   _hang           /* prefetch abort */
    b   _hang           /* data abort */
    b   _hang           /* reserved */
    b   _hang           /* IRQ */
    b   _hang           /* FIQ */
_hang:
    b   _hang

    .section .entry, "ax"
    .globl _start
    .type _start, %function
_start:
    /* Ensure SVC mode, IRQ/FIQ masked. */
    msr     cpsr_c, #(CPSR_MODE_SVC | CPSR_I_BIT | CPSR_F_BIT)

    /* Install the vector base. */
    ldr     r0, =_vectors
    mcr     p15, 0, r0, c12, c0, 0      /* VBAR = _vectors */

    /* Read SCTLR, force MMU (M) and D-cache (C) off for a known state. */
    mrc     p15, 0, r0, c1, c0, 0
    bic     r0, r0, #0x00000001         /* M = 0 (MMU off) */
    bic     r0, r0, #0x00000004         /* C = 0 (D-cache off) */
    mcr     p15, 0, r0, c1, c0, 0
    dsb
    isb

    /* Invalidate I-cache, branch predictor and unified TLB. */
    mov     r0, #0
    mcr     p15, 0, r0, c7, c5, 0       /* ICIALLU: invalidate I-cache */
    mcr     p15, 0, r0, c7, c5, 6       /* BPIALL: invalidate branch pred */
    mcr     p15, 0, r0, c8, c7, 0       /* TLBIALL: invalidate TLB */
    dsb
    isb

    /* Enable I-cache (I, bit12) and program-flow prediction (Z, bit11). */
    mrc     p15, 0, r0, c1, c0, 0
    orr     r0, r0, #0x00001000         /* I = 1 (I-cache) */
    orr     r0, r0, #0x00000800         /* Z = 1 (branch prediction) */
    mcr     p15, 0, r0, c1, c0, 0
    isb

    /* Enable VFP/NEON for the hard-float ABI: full CP10/CP11 access, then set
     * the FPEXC EN bit. */
    mrc     p15, 0, r0, c1, c0, 2       /* CPACR */
    orr     r0, r0, #(0xF << 20)        /* cp10 + cp11 full access */
    mcr     p15, 0, r0, c1, c0, 2
    isb
    mov     r0, #(1 << 30)              /* FPEXC.EN */
    vmsr    fpexc, r0

    /* Per-mode stacks. Switch to each mode, set sp, return to SVC last. */
    msr     cpsr_c, #(CPSR_MODE_IRQ | CPSR_I_BIT | CPSR_F_BIT)
    ldr     sp, =_irq_stack_top
    msr     cpsr_c, #(CPSR_MODE_ABT | CPSR_I_BIT | CPSR_F_BIT)
    ldr     sp, =_abt_stack_top
    msr     cpsr_c, #(CPSR_MODE_UND | CPSR_I_BIT | CPSR_F_BIT)
    ldr     sp, =_und_stack_top
    msr     cpsr_c, #(CPSR_MODE_SVC | CPSR_I_BIT | CPSR_F_BIT)
    ldr     sp, =_stack_top

    /* Zero .bss. */
    ldr     r0, =_bss_start
    ldr     r1, =_bss_end
    mov     r2, #0
1:
    cmp     r0, r1
    bhs     2f
    str     r2, [r0], #4
    b       1b
2:
    /* Copy .data from LMA to VMA (no-op for a JTAG image where LMA == VMA). */
    ldr     r0, =_data_load
    ldr     r1, =_data_start
    ldr     r2, =_data_end
3:
    cmp     r1, r2
    bhs     4f
    ldr     r3, [r0], #4
    str     r3, [r1], #4
    b       3b
4:
    /* Enable the MMU (flat map, DDR Normal cacheable) before any C library or
     * application code: with the MMU off all data is Strongly-Ordered and the
     * unaligned accesses newlib emits (e.g. in printf) would fault. */
    bl      mmu_init

    /* C library constructors, then main(0, NULL). */
    bl      __libc_init_array
    mov     r0, #0
    mov     r1, #0
    bl      main

    /* main should not return; park the core if it does. */
5:
    wfi
    b       5b
    .size _start, . - _start

    /* Empty _init/_fini for __libc_init_array (crti/crtn are omitted under
     * -nostartfiles; there are no C++ static constructors to run here). */
    .globl _init
    .type _init, %function
_init:
    bx      lr
    .size _init, . - _init

    .globl _fini
    .type _fini, %function
_fini:
    bx      lr
    .size _fini, . - _fini
