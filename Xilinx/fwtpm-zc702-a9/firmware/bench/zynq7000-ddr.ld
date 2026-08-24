/* zynq7000-ddr.ld
 *
 * Linker script for the Zynq-7000 Cortex-A9 hello-world image, linked into DDR
 * at 0x04000000 (the FSBL brings DDR up via ps7_init; we load over the top with
 * JTAG). Vector table first so VBAR lands on it.
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

OUTPUT_ARCH(arm)
ENTRY(_start)

__stack_size     = DEFINED(__stack_size)     ? __stack_size     : 16K;
__irq_stack_size = DEFINED(__irq_stack_size) ? __irq_stack_size : 4K;
__abt_stack_size = DEFINED(__abt_stack_size) ? __abt_stack_size : 1K;
__und_stack_size = DEFINED(__und_stack_size) ? __und_stack_size : 1K;
__heap_size      = DEFINED(__heap_size)      ? __heap_size      : 64K;

MEMORY
{
    ddr (rwx) : ORIGIN = 0x04000000, LENGTH = 64M
}

SECTIONS
{
    .vectors : ALIGN(32) { KEEP(*(.vectors)) } > ddr
    .entry   : ALIGN(4)  { KEEP(*(.entry)) } > ddr

    .text : ALIGN(4)
    {
        *(.text .text.*)
        *(.gnu.linkonce.t.*)
        *(.rodata .rodata.*)
        *(.gnu.linkonce.r.*)
        . = ALIGN(4);
    } > ddr

    .ARM.exidx : ALIGN(4)
    {
        __exidx_start = .;
        *(.ARM.exidx* .gnu.linkonce.armexidx.*)
        __exidx_end = .;
    } > ddr
    .ARM.extab : ALIGN(4) { *(.ARM.extab* .gnu.linkonce.armextab.*) } > ddr

    .preinit_array : ALIGN(4)
    {
        PROVIDE_HIDDEN (__preinit_array_start = .);
        KEEP (*(.preinit_array))
        PROVIDE_HIDDEN (__preinit_array_end = .);
    } > ddr
    .init_array : ALIGN(4)
    {
        PROVIDE_HIDDEN (__init_array_start = .);
        KEEP (*(SORT(.init_array.*)))
        KEEP (*(.init_array))
        PROVIDE_HIDDEN (__init_array_end = .);
    } > ddr
    .fini_array : ALIGN(4)
    {
        PROVIDE_HIDDEN (__fini_array_start = .);
        KEEP (*(SORT(.fini_array.*)))
        KEEP (*(.fini_array))
        PROVIDE_HIDDEN (__fini_array_end = .);
    } > ddr

    .data : ALIGN(8)
    {
        _data_start = .;
        *(.data .data.*)
        *(.gnu.linkonce.d.*)
        . = ALIGN(8);
        _data_end = .;
    } > ddr
    /* RAM image: LMA follows VMA in the one region, so _data_load == _data_start
     * and startup.S's copy loop is a no-op. */
    _data_load = LOADADDR(.data);

    .bss (NOLOAD) : ALIGN(8)
    {
        _bss_start = .;
        *(.bss .bss.*)
        *(.gnu.linkonce.b.*)
        *(COMMON)
        . = ALIGN(8);
        _bss_end = .;
    } > ddr

    . = ALIGN(8);
    PROVIDE (end = .);
    PROVIDE (_end = .);

    .heap (NOLOAD) : ALIGN(8)
    {
        . = . + __heap_size;
        . = ALIGN(8);
        _heap_end = .;
    } > ddr

    /* Exception-mode stacks then the SVC/main stack (top of each region). */
    .stacks (NOLOAD) : ALIGN(16)
    {
        . = . + __irq_stack_size; . = ALIGN(16); _irq_stack_top = .;
        . = . + __abt_stack_size; . = ALIGN(16); _abt_stack_top = .;
        . = . + __und_stack_size; . = ALIGN(16); _und_stack_top = .;
        . = . + __stack_size;     . = ALIGN(16); _stack_top = .;
    } > ddr
}
