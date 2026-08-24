/* mbv-bram.ld
 *
 * Linker script for the MicroBlaze V wolfTPM fwTPM on the SCU35, linked into the
 * local BRAM at the reset vector (0x00000000). The RAM length here (768 KB)
 * covers the full RSA+ECC fwTPM footprint (~652 KB); the stock reference design
 * provides only 192 KB, so running that image requires a bitstream with enlarged
 * local memory (see fpga/README.md). The minimal ECC-only build (FWTPM_TINY_ECC
 * plus the reduced-heap link in the Makefile) lands at ~190 KB and runs from the
 * stock 192 KB. Size LENGTH to your bitstream's actual local-memory size.
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

MEMORY
{
    ram (rwx) : ORIGIN = 0x00000000, LENGTH = 768K
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
