/* startup.S
 *
 * Minimal bare-metal startup for PolarFire SoC U54 hart in M-mode.
 * HSS AMP payload jumps here after PLL and DDR are initialized.
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

    /* Shared-memory debug offsets (must match fwtpm_tis_mpfs.h layout).
     * This is FWTPM_DBG_BASE: ALWAYS L2 LIM, independent of FWTPM_XPORT,
     * so the boot/trap breadcrumbs are reachable before any PMP/cache
     * setup and stay visible to Linux even when the live mailbox moves. */
    .equ FWTPM_SHM_BASE,        0x08000000
    .equ FWTPM_OFF_PROGRESS,    0x14
    .equ FWTPM_OFF_TRAP_MARKER, 0x20
    .equ FWTPM_OFF_TRAP_MCAUSE, 0x28
    .equ FWTPM_OFF_TRAP_MEPC,   0x30
    .equ FWTPM_OFF_TRAP_MTVAL,  0x38
    .equ FWTPM_TRAP_MAGIC,      0xDEADC0DE
    .equ FWTPM_PROG_ENTRY,      0x10
    .equ FWTPM_PROG_TRAPCAUGHT, 0xBADC0DE0

    .section .entry, "ax"
    .globl _start
    .type _start, @function
_start:
    /* Disable interrupts */
    csrw mie, zero
    csrw mstatus, zero

#ifdef FWTPM_XPORT_L1D_OFF
    /* Coherency spike candidate L1D_OFF -- DEAD END (kept for reference).
     * Intended to disable hart 4's L1 data cache via the SiFive U54
     * Feature-Disable CSR (0x7C1). Bench result on the MPFS250T Video Kit:
     * this csrw halts hart 4 before the first breadcrumb (progress stays
     * 0), and mtvec is not set yet so no trap is recorded. The 0x7C1
     * access is not usable here as written. It is also unnecessary: the
     * cacheable L2 LIM mailbox is coherent for hart 4 under HSS AMP
     * (LIM_CACHED works), and DDR_NONCACHED is the uncached alternative. */
    csrr t0, 0x7c1
    ori  t0, t0, 0x2
    csrw 0x7c1, t0
    fence
    fence.i
#endif

    /* Earliest possible breadcrumb: write progress=0x10 to shared mem
     * BEFORE touching gp/sp/bss so we know hart 4 reached _start at all.
     * Also zero the trap-debug fields so stale data from a previous boot
     * (DDR retains contents across reset) can't masquerade as a current
     * trap. */
    li   t0, FWTPM_SHM_BASE
    li   t1, FWTPM_PROG_ENTRY
    sw   t1, FWTPM_OFF_PROGRESS(t0)
    sw   zero, FWTPM_OFF_TRAP_MARKER(t0)
    sd   zero, FWTPM_OFF_TRAP_MCAUSE(t0)
    sd   zero, FWTPM_OFF_TRAP_MEPC(t0)
    sd   zero, FWTPM_OFF_TRAP_MTVAL(t0)
    fence ow, ow

    /* Install M-mode trap handler. Mode = direct (low 2 bits 0). */
    la   t0, _trap_vector
    csrw mtvec, t0

    /* Set global pointer */
    .option push
    .option norelax
    la gp, __global_pointer$
    .option pop

    /* Set stack pointer */
    la sp, __stack_top

    /* Clear BSS */
    la t0, __bss_start
    la t1, __bss_end
1:
    beq t0, t1, 2f
    sd zero, 0(t0)
    addi t0, t0, 8
    j 1b
2:

#ifdef FWTPM_XPORT_DDR_NONCACHED
    /* Coherency spike candidate DDR_NONCACHED: the live mailbox moves to
     * the 64-bit non-cached DDR alias (0x1400000000). Under skip-opensbi
     * nothing set up PMP; install one NAPOT entry covering all address
     * space (R|W|X) so M-mode access to the alias is explicit. Must be in
     * place before main() first touches the alias. Note: the breadcrumb
     * stores above stay in LIM (0x08000000), which needs no PMP. */
    li   t0, -1               /* pmpaddr0 all-ones -> whole space (NAPOT) */
    csrw pmpaddr0, t0
    li   t0, 0x1f             /* pmpcfg0[0]: A=NAPOT(0x18)|X|W|R(0x07) */
    csrw pmpcfg0, t0
    fence
#endif

    /* Jump to C main */
    call main

    /* If main returns, spin */
3:
    wfi
    j 3b

    .size _start, . - _start

/*
 * M-mode trap vector. Stack pointer may be invalid, so use absolute
 * addresses into the shared-memory debug block. Records mcause/mepc/mtval
 * and a sentinel marker, then halts in WFI loop.
 */
    .align 4
    .globl _trap_vector
    .type _trap_vector, @function
_trap_vector:
    li    t0, FWTPM_SHM_BASE
    li    t1, FWTPM_TRAP_MAGIC
    sw    t1, FWTPM_OFF_TRAP_MARKER(t0)
    csrr  t1, mcause
    sd    t1, FWTPM_OFF_TRAP_MCAUSE(t0)
    csrr  t1, mepc
    sd    t1, FWTPM_OFF_TRAP_MEPC(t0)
    csrr  t1, mtval
    sd    t1, FWTPM_OFF_TRAP_MTVAL(t0)
    li    t1, FWTPM_PROG_TRAPCAUGHT
    sw    t1, FWTPM_OFF_PROGRESS(t0)
    fence ow, ow
4:
    wfi
    j 4b
    .size _trap_vector, . - _trap_vector
