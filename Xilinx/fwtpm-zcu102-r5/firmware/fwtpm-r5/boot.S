/* boot.S
 *
 * Cortex-R5 lock-step entry for fwTPM firmware loaded by Linux
 * remoteproc. The Cortex-R5 reset vector is hardware-fixed at
 * VA 0x00000000 (V=0, the default for ZynqMP RPU); the linker
 * therefore places .vectors in TCM atcm0 (mapped by remoteproc to
 * physical 0xFFE00000 in lock-step). _entry / main / heap / stack /
 * trace_buffer all live in the rproc_0_reserved DDR carveout at
 * 0x3ED00000, which is ~158 MiB away from the vectors -- beyond ARM
 * `b`'s +/- 32 MiB reach -- so the vectors use absolute pc-loads via
 * a literal pool (.ltorg).
 *
 * The BSP libxilstandalone _boot is intentionally NOT used: its MPU /
 * cache init depends on peripheral regions whose addresses don't
 * match the rproc-loaded layout, and it faults before reaching main().
 * Our custom entry sets up CPSR + SP + BSS only; caches/MPU stay off
 * (the R5 falls back to its background "Strongly-ordered" region,
 * which is correct for shared rproc/vring memory and DDR-only code).
 *
 * Copyright (C) 2006-2026 wolfSSL Inc.
 */

    .syntax unified
    .arch armv7-r
    .arm

    .section .vectors, "ax"
    .global _vector_table
_vector_table:
    ldr pc, =_tcm_reset         /* 0x00 reset -- TCM stub clears MPU before DDR */
    ldr pc, =_undef             /* 0x04 undef */
    ldr pc, =_swi               /* 0x08 swi */
    ldr pc, =_prefetch_abort    /* 0x0C prefetch abort */
    ldr pc, =_data_abort        /* 0x10 data abort */
    nop                         /* 0x14 reserved */
    ldr pc, =_irq               /* 0x18 IRQ */
    ldr pc, =_fiq               /* 0x1C FIQ */
    .ltorg                      /* literal pool for the loads above */

    /*
     * TCM-resident reset stub. ZynqMP remoteproc stop / start preserves
     * CP15 state across firmware reloads. If a prior firmware left MPU
     * enabled with regions that do not cover our DDR carveout, the
     * first DDR instruction fetch by _entry (located in DDR) would
     * prefetch-abort before any C code runs. Disable MPU + I/D caches
     * here in TCM so the jump to _entry succeeds regardless of any
     * stale CP15 state. A cold reset has MPU already off; this stub
     * is a no-op in that case.
     */
    .global _tcm_reset
    .type _tcm_reset, %function
_tcm_reset:
    cpsid   if, #0x1F                       /* System mode, IRQ/FIQ off  */
    mrc     p15, 0, r0, c1, c0, 0           /* SCTLR                     */
    bic     r0, r0, #0x1                    /* clear M (MPU)             */
    bic     r0, r0, #(1 << 2)               /* clear C (D-cache)         */
    bic     r0, r0, #(1 << 12)              /* clear I (I-cache)         */
    dsb
    mcr     p15, 0, r0, c1, c0, 0
    isb
    ldr     pc, =_entry                     /* hop to DDR _entry         */
    .ltorg
    .size _tcm_reset, . - _tcm_reset

    .section .text._exception_handlers, "ax"
    .global _undef, _swi, _prefetch_abort, _data_abort, _irq, _fiq
_undef:           b _undef
_swi:             b _swi
_prefetch_abort:  b _prefetch_abort
_data_abort:      b _data_abort
_fiq:             b _fiq

    /* IRQ trampoline: ARMv7-R uses lr-4 for the return address; save
     * caller-saved + lr, dispatch to C, then return via ^ (restore
     * SPSR -> CPSR). _irq_dispatch is defined in platform_info.c
     * and calls XScuGic_InterruptHandler(&g_gic). */
    .global _irq
    .type _irq, %function
_irq:
    sub     lr, lr, #4
    stmfd   sp!, {r0-r3, r12, lr}
    bl      _irq_dispatch
    ldmfd   sp!, {r0-r3, r12, lr}
    movs    pc, lr
    .size _irq, . - _irq

    .section .text._entry, "ax"
    .global _entry
    .global _boot                /* alias so libxilstandalone refs resolve */
    .type _entry, %function
_entry:
_boot:
    /* IRQ mode -- set the IRQ banked SP. _irq_stack is the high end of
     * the IRQ stack region from lscript.ld. IRQs stay masked here; we
     * just need a valid SP for when an IRQ taken later in System mode
     * switches CPU to IRQ mode. */
    cpsid   if, #0x12
    ldr     sp, =__irq_stack
    /* System mode, IRQ + FIQ disabled. */
    cpsid   if, #0x1F
    /* Stack to top of .stack region (linker-supplied). */
    ldr     sp, =_stack
    /* Zero BSS. */
    ldr     r0, =__bss_start__
    ldr     r1, =__bss_end__
    movs    r2, #0
1:
    cmp     r0, r1
    bge     2f
    str     r2, [r0], #4
    b       1b
2:
    bl      main
    /* If main returns, halt. */
3:
    wfi
    b       3b
    .size _entry, . - _entry
