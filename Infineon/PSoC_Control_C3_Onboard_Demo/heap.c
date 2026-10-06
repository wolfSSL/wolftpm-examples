/* heap.c
 *
 * A fixed heap for the on-board TPM applications.
 *
 * wolfTPM allocates internally, so newlib's malloc needs somewhere to grow.
 * A static pool is used rather than the region between the end of .bss and
 * the stack, because a bounded heap turns an allocation failure into a clear
 * error instead of silent stack corruption. ML-DSA verification itself
 * allocates nothing under WOLFSSL_MLDSA_VERIFY_SMALLEST_MEM; this is for
 * wolfTPM's own use.
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
#include <sys/types.h>
#include <errno.h>

#ifndef APP_HEAP_SIZE
#define APP_HEAP_SIZE (24 * 1024)
#endif

static char heap[APP_HEAP_SIZE];
static char *brk = heap;

void *_sbrk(ptrdiff_t incr)
{
    char *prev = brk;

    if ((brk + incr) > (heap + sizeof(heap))) {
        errno = ENOMEM;
        return (void *)-1;
    }
    brk += incr;
    return prev;
}

/* How much of the heap the sbrk pointer has handed out, and how big it is.
 * An allocation failure on an MCU is worth a number, not a guess. */
void app_heap_usage(unsigned long *used, unsigned long *total)
{
    *used = (unsigned long)(brk - heap);
    *total = (unsigned long)sizeof(heap);
}
