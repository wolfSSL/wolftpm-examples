/* startup.S
 *
 * Minimal bare-metal RV32 startup for the MicroBlaze V soft core. The core reset
 * vector is 0x00000000, where .entry (this _start) is placed by the linker. We
 * set gp and sp, clear .bss, copy .data from its load address, install a trap
 * handler, run the C library init array, and call main().
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

    .section .entry, "ax"
    .globl _start
    .type _start, @function
_start:
    /* Global pointer (relaxation must be off across this load). */
    .option push
    .option norelax
    la      gp, __global_pointer$
    .option pop

    /* Stack pointer. */
    la      sp, _stack_top

    /* Zero the .bss / .sbss region. */
    la      a0, _bss_start
    la      a1, _bss_end
    bgeu    a0, a1, 2f
1:
    sw      zero, 0(a0)
    addi    a0, a0, 4
    bltu    a0, a1, 1b
2:
    /* Copy initialized data from its load address to RAM. This is a no-op when
     * the image is loaded (via JTAG) with LMA == VMA. */
    la      a0, _data_load
    la      a1, _data_start
    la      a2, _data_end
    bgeu    a1, a2, 4f
3:
    lw      t0, 0(a0)
    sw      t0, 0(a1)
    addi    a0, a0, 4
    addi    a1, a1, 4
    bltu    a1, a2, 3b
4:
    /* Install the trap handler (direct mode). */
    la      t0, trap_entry
    csrw    mtvec, t0

    /* Run C library constructors (init_array). */
    call    __libc_init_array

    /* main(0, NULL). */
    li      a0, 0
    li      a1, 0
    call    main

    /* main should not return; park the core if it does. */
hang:
    j       hang
    .size _start, . - _start

    .align 2
    .globl trap_entry
    .type trap_entry, @function
trap_entry:
    /* Minimal handler: park the core. */
    j       trap_entry
    .size trap_entry, . - trap_entry
