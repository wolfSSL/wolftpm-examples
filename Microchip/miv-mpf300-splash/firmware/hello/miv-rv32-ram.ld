/* miv-rv32-ram.ld
 *
 * Linker script for running from a single RAM region (LSRAM) on the MIV_RV32
 * soft core, loaded via JTAG for fast iteration. The core reset vector must
 * point at ORIGIN(ram), where .entry (_start) is placed.
 *
 * The stock Splash Kit MIV_RV32 maps a 16 KB AHB window at 0x80000000 (the
 * physical LSRAM is 64 KB, but the core's AHB_END_ADDR caps addressing at 16 KB).
 * This hello build fits that window, so it runs on the unmodified reference
 * design; the wolfCrypt/fwTPM builds need the window widened to 512 KB (see
 * fpga/README.md). Adjust ORIGIN/LENGTH to match your Libero design's memory map.
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

__stack_size = DEFINED(__stack_size) ? __stack_size : 2K;
__heap_size  = DEFINED(__heap_size)  ? __heap_size  : 1K;

MEMORY
{
    ram (rwx) : ORIGIN = 0x80000000, LENGTH = 16K
}

SECTIONS
{
    .entry : ALIGN(4)
    {
        KEEP (*(.entry))
    } > ram

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
