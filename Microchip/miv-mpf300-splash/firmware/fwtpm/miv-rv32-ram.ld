/* miv-rv32-ram.ld
 *
 * Linker script for the wolfTPM fwTPM on the MIV_RV32 soft core.
 * The RAM region here is intentionally large so the image links while the
 * footprint is being measured; size it to the actual Libero design memory
 * (LSRAM or DDR4) once the footprint is known. ORIGIN must be the core reset
 * vector.
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

OUTPUT_ARCH("riscv")
ENTRY(_start)

__stack_size = DEFINED(__stack_size) ? __stack_size : 24K;
__heap_size  = DEFINED(__heap_size)  ? __heap_size  : 128K;

/* 512 KB LSRAM at the reset vector (design memory expanded from the reference
 * 64 KB). Static image ~283 KB + heap + stack fits with margin. */
MEMORY
{
    ram (rwx) : ORIGIN = 0x80000000, LENGTH = 512K
}

SECTIONS
{
    .entry : ALIGN(4) { KEEP (*(.entry)) } > ram

    .text : ALIGN(4)
    {
        *(.text .text.*)
        *(.gnu.linkonce.t.*)
        *(.rodata .rodata.*)
        *(.srodata .srodata.*)
        *(.gnu.linkonce.r.*)
        . = ALIGN(4);
    } > ram

    .preinit_array : ALIGN(4)
    {
        PROVIDE_HIDDEN (__preinit_array_start = .);
        KEEP (*(.preinit_array))
        PROVIDE_HIDDEN (__preinit_array_end = .);
    } > ram
    .init_array : ALIGN(4)
    {
        PROVIDE_HIDDEN (__init_array_start = .);
        KEEP (*(SORT(.init_array.*)))
        KEEP (*(.init_array))
        PROVIDE_HIDDEN (__init_array_end = .);
    } > ram
    .fini_array : ALIGN(4)
    {
        PROVIDE_HIDDEN (__fini_array_start = .);
        KEEP (*(SORT(.fini_array.*)))
        KEEP (*(.fini_array))
        PROVIDE_HIDDEN (__fini_array_end = .);
    } > ram

    .data : ALIGN(4)
    {
        _data_start = .;
        __global_pointer$ = . + 0x800;
        *(.data .data.*)
        *(.gnu.linkonce.d.*)
        *(.sdata .sdata.*)
        *(.sdata2 .sdata2.*)
        . = ALIGN(4);
        _data_end = .;
    } > ram
    /* RAM image: LMA follows VMA in the one region, so _data_load == _data_start
     * and startup.S's copy loop is a no-op. Deriving from LOADADDR (rather than a
     * separate AT()) keeps them equal even if an 8-aligned global bumps .data. */
    _data_load = LOADADDR(.data);

    .bss (NOLOAD) : ALIGN(4)
    {
        _bss_start = .;
        *(.sbss .sbss.*)
        *(.gnu.linkonce.sb.*)
        *(.bss .bss.*)
        *(.gnu.linkonce.b.*)
        *(COMMON)
        . = ALIGN(4);
        _bss_end = .;
    } > ram

    . = ALIGN(8);
    PROVIDE (end = .);
    PROVIDE (_end = .);

    .heap (NOLOAD) : ALIGN(8)
    {
        . = . + __heap_size;
        . = ALIGN(8);
        _heap_end = .;
    } > ram

    .stack (NOLOAD) : ALIGN(16)
    {
        . = . + __stack_size;
        . = ALIGN(16);
        _stack_top = .;
    } > ram
}
