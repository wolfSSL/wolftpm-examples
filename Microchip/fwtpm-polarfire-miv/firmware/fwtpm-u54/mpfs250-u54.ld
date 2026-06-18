/* mpfs250-u54.ld
 *
 * Linker script for bare-metal U54 hart on PolarFire SoC MPFS250T.
 * HSS AMP payload loads this ELF at 0x91C00000 (DDR4).
 *
 * Copyright (C) 2006-2026 wolfSSL Inc.
 */

OUTPUT_ARCH("riscv")
ENTRY(_start)

MEMORY
{
    /* 4MB DDR region for bare-metal hart (matches amp.yaml exec-addr) */
    ddr (rwx) : ORIGIN = 0x91C00000, LENGTH = 4M
}

STACK_SIZE = 64K;
HEAP_SIZE  = 64K;

SECTIONS
{
    .entry : ALIGN(0x10)
    {
        KEEP (*(SORT_NONE(.entry)))
        . = ALIGN(0x10);
    } > ddr

    .text : ALIGN(0x10)
    {
        *(.text .text.* .gnu.linkonce.t.*)
        *(.plt)
        . = ALIGN(0x10);

        KEEP (*crtbegin.o(.ctors))
        KEEP (*(EXCLUDE_FILE (*crtend.o) .ctors))
        KEEP (*(SORT(.ctors.*)))
        KEEP (*crtend.o(.ctors))
        KEEP (*crtbegin.o(.dtors))
        KEEP (*(EXCLUDE_FILE (*crtend.o) .dtors))
        KEEP (*(SORT(.dtors.*)))
        KEEP (*crtend.o(.dtors))

        *(.rodata .rodata.* .gnu.linkonce.r.*)
        *(.gcc_except_table)
        *(.eh_frame_hdr)
        *(.eh_frame)

        KEEP (*(.init))
        KEEP (*(.fini))

        PROVIDE_HIDDEN (__preinit_array_start = .);
        KEEP (*(.preinit_array))
        PROVIDE_HIDDEN (__preinit_array_end = .);
        PROVIDE_HIDDEN (__init_array_start = .);
        KEEP (*(SORT(.init_array.*)))
        KEEP (*(.init_array))
        PROVIDE_HIDDEN (__init_array_end = .);
        PROVIDE_HIDDEN (__fini_array_start = .);
        KEEP (*(.fini_array))
        KEEP (*(SORT(.fini_array.*)))
        PROVIDE_HIDDEN (__fini_array_end = .);
        . = ALIGN(0x10);
    } > ddr

    .sdata : ALIGN(0x10)
    {
        __sdata_load = LOADADDR(.sdata);
        __sdata_start = .;
        PROVIDE( __global_pointer$ = . + 0x800);
        *(.srodata.cst16) *(.srodata.cst8) *(.srodata.cst4) *(.srodata.cst2)
        *(.srodata*)
        *(.sdata .sdata.* .gnu.linkonce.s.*)
        . = ALIGN(0x10);
        __sdata_end = .;
    } > ddr

    .data : ALIGN(0x10)
    {
        __data_load = LOADADDR(.data);
        __data_start = .;
        *(.got.plt) *(.got)
        *(.shdata)
        *(.data .data.* .gnu.linkonce.d.*)
        . = ALIGN(0x10);
        __data_end = .;
    } > ddr

    /* startup.S clears [__bss_start, __bss_end). Mark __bss_start at the
     * START of .sbss so the zero-init loop covers both .sbss and .bss
     * -- RISC-V GCC small-data optimization puts pointers/scalars in
     * .sbss by default, and missing them was causing nonzero g_console_ring
     * and similar at boot. */
    .sbss : ALIGN(0x10)
    {
        __bss_start = .;
        __sbss_start = .;
        *(.sbss .sbss.* .gnu.linkonce.sb.*)
        *(.scommon)
        . = ALIGN(0x10);
        __sbss_end = .;
    } > ddr

    .bss : ALIGN(0x10)
    {
        *(.shbss)
        *(.bss .bss.* .gnu.linkonce.b.*)
        *(COMMON)
        . = ALIGN(0x10);
        __bss_end = .;
    } > ddr

    _end = .;
    PROVIDE(end = .);   /* newlib _sbrk */

    .heap : ALIGN(0x10)
    {
        __heap_start = .;
        . += HEAP_SIZE;
        __heap_end = .;
        _heap_end = __heap_end;
    } > ddr

    .stack : ALIGN(0x10)
    {
        __stack_bottom = .;
        . += STACK_SIZE;
        __stack_top = .;
    } > ddr
}
