/* lscript-dcache.ld
 *
 * Default (D-cache) linker script, used by the Makefile unless built
 * with FWTPM_DCACHE=0 (which falls back to lscript.ld). It is identical
 * to lscript.ld EXCEPT that the two
 * APU-shared DDR objects -- the OpenAMP resource table and the trace
 * buffer -- are grouped into a single 64 KiB non-cacheable window at the
 * carveout base (__shared_nc_start .. __shared_nc_end). Everything else
 * (code, rodata, data, bss, heap, stack) follows and is left cacheable.
 *
 * mpu_setup.c (under #ifdef FWTPM_DCACHE) then maps:
 *   - the whole carveout Normal write-back cacheable (region 0),
 *   - this 64 KiB window Normal non-cacheable (overrides region 0),
 *   - the vring/buffer pool at 0x3EE00000 non-cacheable,
 * and enables both the I-cache and D-cache. Caching the SHA3/entropy
 * working set (otherwise uncached DDR) cuts MemUse seeding / FWTPM_Init
 * from ~30 s to a few seconds, while the grouped NC window keeps the
 * trace ring and virtio handshake coherent with the APU.
 *
 * The vrings live OUTSIDE this carveout (0x3EE00000), so they are not
 * part of this window; mpu_setup.c covers them with their own NC region.
 *
 * Copyright (C) 2006-2026 wolfSSL Inc.
 */

_STACK_SIZE            = DEFINED(_STACK_SIZE)            ? _STACK_SIZE            : 0x10000;
_HEAP_SIZE             = DEFINED(_HEAP_SIZE)             ? _HEAP_SIZE             : 0x40000;
_ABORT_STACK_SIZE      = DEFINED(_ABORT_STACK_SIZE)      ? _ABORT_STACK_SIZE      : 1024;
_SUPERVISOR_STACK_SIZE = DEFINED(_SUPERVISOR_STACK_SIZE) ? _SUPERVISOR_STACK_SIZE : 4096;
_IRQ_STACK_SIZE        = DEFINED(_IRQ_STACK_SIZE)        ? _IRQ_STACK_SIZE        : 4096;
_FIQ_STACK_SIZE        = DEFINED(_FIQ_STACK_SIZE)        ? _FIQ_STACK_SIZE        : 1024;
_UNDEF_STACK_SIZE      = DEFINED(_UNDEF_STACK_SIZE)      ? _UNDEF_STACK_SIZE      : 1024;

/* Size of the grouped non-cacheable window (rsc_table + trace_buffer).
 * MUST match SHARED_NC_SIZE in mpu_setup.c and be a power of two the
 * ARMv7-R MPU can encode. */
_SHARED_NC_SIZE = 0x10000;

MEMORY
{
    TCM : ORIGIN = 0x00000000, LENGTH = 0x10000
    DDR : ORIGIN = 0x3ED00000, LENGTH = 0x100000
}

ENTRY(_vector_table)

SECTIONS
{
    /DISCARD/ : {
        *(.note .note.*)
        *(.comment)
        *(.ARM.attributes)
    }

    .vectors : {
        KEEP (*(.vectors))
        *(.boot)
    } > TCM

    /* --- APU-shared non-cacheable window (carveout base) --- */
    .resource_table : {
        __shared_nc_start = .;
        . = ALIGN(4);
        KEEP (*(.resource_table))
    } > DDR

    /* Pad the window to 64 KiB from INSIDE the section: a bare "." set
     * between ">DDR" sections is ignored (ld tracks the region pointer
     * separately), so the padding must extend this NOLOAD section -- the
     * same idiom .heap/.stack use below -- to push .text onto the window
     * boundary and keep code out of the non-cacheable window. */
    .trace_buffer (NOLOAD) : {
        . = ALIGN(16);
        *(.trace_buffer)
        __shared_nc_used_end = .;
        . = __shared_nc_start + _SHARED_NC_SIZE;
        __shared_nc_end = .;
    } > DDR

    ASSERT(__shared_nc_used_end <= __shared_nc_end,
           "rsc_table + trace_buffer exceed the 64 KiB shared-NC window")
    /* --- end shared window; everything below is cacheable --- */

    .text : {
        *(.text)
        *(.text.*)
        *(.gnu.linkonce.t.*)
        *(.plt)
        *(.gnu_warning)
        *(.gcc_execpt_table)
        *(.glue_7) *(.glue_7t)
        *(.vfp11_veneer)
        *(.ARM.extab)
        *(.gnu.linkonce.armextab.*)
    } > DDR

    .init   : { KEEP (*(.init)) } > DDR
    .fini   : { KEEP (*(.fini)) } > DDR

    .rodata : {
        __rodata_start = .;
        *(.rodata) *(.rodata.*) *(.gnu.linkonce.r.*)
        __rodata_end = .;
    } > DDR

    .rodata1 : {
        __rodata1_start = .;
        *(.rodata1) *(.rodata1.*)
        __rodata1_end = .;
    } > DDR

    .data : {
        __data_start = .;
        *(.data) *(.data.*) *(.gnu.linkonce.d.*)
        *(.jcr) *(.got) *(.got.plt)
        __data_end = .;
    } > DDR

    .ctors : {
        __CTOR_LIST__ = .;
        ___CTORS_LIST___ = .;
        KEEP (*crtbegin.o(.ctors))
        KEEP (*(EXCLUDE_FILE(*crtend.o) .ctors))
        KEEP (*(SORT(.ctors.*)))
        KEEP (*(.ctors))
        __CTOR_END__ = .;
        ___CTORS_END___ = .;
    } > DDR

    .dtors : {
        __DTOR_LIST__ = .;
        ___DTORS_LIST___ = .;
        KEEP (*crtbegin.o(.dtors))
        KEEP (*(EXCLUDE_FILE(*crtend.o) .dtors))
        KEEP (*(SORT(.dtors.*)))
        KEEP (*(.dtors))
        __DTOR_END__ = .;
        ___DTORS_END___ = .;
    } > DDR

    .preinit_array : {
        __preinit_array_start = .;
        KEEP (*(SORT(.preinit_array.*)))
        KEEP (*(.preinit_array))
        __preinit_array_end = .;
    } > DDR

    .init_array : {
        __init_array_start = .;
        KEEP (*(SORT(.init_array.*)))
        KEEP (*(.init_array))
        __init_array_end = .;
    } > DDR

    .fini_array : {
        __fini_array_start = .;
        KEEP (*(SORT(.fini_array.*)))
        KEEP (*(.fini_array))
        __fini_array_end = .;
    } > DDR

    .ARM.exidx : {
        __exidx_start = .;
        *(.ARM.exidx*) *(.gnu.linkonce.armexidx.*)
        __exidx_end = .;
    } > DDR

    .eh_frame    : { *(.eh_frame) } > DDR
    .eh_framehdr : {
        __eh_framehdr_start = .;
        *(.eh_framehdr)
        __eh_framehdr_end = .;
    } > DDR

    .gcc_except_table : { *(.gcc_except_table) } > DDR

    .ARM.attributes : {
        __ARM.attributes_start = .;
        *(.ARM.attributes)
        __ARM.attributes_end = .;
    } > DDR

    .sdata : {
        __sdata_start = .;
        *(.sdata) *(.sdata.*) *(.gnu.linkonce.s.*)
        __sdata_end = .;
    } > DDR

    .sbss (NOLOAD) : {
        __sbss_start = .;
        *(.sbss) *(.sbss.*) *(.gnu.linkonce.sb.*)
        __sbss_end = .;
    } > DDR

    .bss (NOLOAD) : {
        . = ALIGN(4);
        __bss_start__ = .;
        __bss_start = .;
        *(.bss) *(.bss.*) *(.gnu.linkonce.b.*)
        *(COMMON)
        . = ALIGN(4);
        __bss_end__ = .;
        __bss_end = .;
    } > DDR

    .heap (NOLOAD) : {
        . = ALIGN(16);
        _heap = .;
        HeapBase = .;
        _heap_start = .;
        . += _HEAP_SIZE;
        _heap_end = .;
        HeapLimit = .;
    } > DDR

    .stack (NOLOAD) : {
        . = ALIGN(16);
        _stack_end = .;
        . += _STACK_SIZE;
        _stack = .;
        __stack = _stack;
        . = ALIGN(16);
        _irq_stack_end = .;
        . += _IRQ_STACK_SIZE;
        __irq_stack = .;
        _supervisor_stack_end = .;
        . += _SUPERVISOR_STACK_SIZE;
        . = ALIGN(16);
        __supervisor_stack = .;
        _abort_stack_end = .;
        . += _ABORT_STACK_SIZE;
        . = ALIGN(16);
        __abort_stack = .;
        _fiq_stack_end = .;
        . += _FIQ_STACK_SIZE;
        . = ALIGN(16);
        __fiq_stack = .;
        _undef_stack_end = .;
        . += _UNDEF_STACK_SIZE;
        . = ALIGN(16);
        __undef_stack = .;
    } > DDR

    .drvcfg_sec : {
        . = ALIGN(8);
        __drvcfgsecdata_start = .;
        KEEP (*(.drvcfg_sec))
        __drvcfgsecdata_end = .;
        __drvcfgsecdata_size = __drvcfgsecdata_end - __drvcfgsecdata_start;
    } > DDR

    end = .;
    _end = .;
}
