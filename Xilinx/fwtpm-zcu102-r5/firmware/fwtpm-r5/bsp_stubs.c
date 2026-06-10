/* bsp_stubs.c
 *
 * Glue for symbols pulled in by newlib + libxilstandalone:
 *
 *   _init / _fini   -- newlib's __libc_init_array iterates these
 *                      (no static c'tors / d'tors to run).
 *
 *   outbyte()       -- BSP libxilstandalone's stdout char sink. The
 *                      default in the BSP writes to PSU UART0, which
 *                      collides with the APU Linux console on a
 *                      remoteproc-loaded R5. Override here to append
 *                      to the .trace_buffer ring instead, which
 *                      Linux remoteproc surfaces as
 *                      /sys/kernel/debug/remoteproc/.../trace0 .
 *
 * Copyright (C) 2006-2026 wolfSSL Inc.
 */

#include <stddef.h>
#include <stdint.h>

void _init(void) { }
void _fini(void) { }

/*
 * Custom _sbrk that hands out memory from the .heap region defined
 * in lscript.ld. nosys.specs supplies a stub that always returns
 * (void*)-1, which makes malloc() / XMALLOC() always fail. wolfTPM's
 * FWTPM_NV_Init relies on a working malloc, so we need a real
 * implementation. Single-threaded, single-arena -- adequate for a
 * bare-metal R5 firmware. */
extern char _heap_start;       /* lscript.ld: bottom of heap        */
extern char _heap_end;         /* lscript.ld: top of heap           */

void *_sbrk(int incr)
{
    static char *brk_ptr = NULL;
    char *prev;
    if (brk_ptr == NULL) {
        brk_ptr = &_heap_start;
    }
    if (brk_ptr + incr > &_heap_end) {
        return (void *)-1;
    }
    prev = brk_ptr;
    brk_ptr += incr;
    return prev;
}

/* The trace_buffer is declared in rsc_table.c so the resource-table
 * entry and outbyte() share the same memory. Linux remoteproc reads
 * it via /sys/kernel/debug/remoteproc/.../trace0 . */
#define TRACE_RING_SIZE 0x8000U
extern char trace_buffer[TRACE_RING_SIZE];

/* Head of the next byte to write -- placed at the very start of the
 * ring would corrupt the Linux trace0 view, so keep it as a separate
 * .data variable. Readers see a flat log starting at trace_buffer[0]
 * until we wrap. */
static volatile uint32_t g_trace_head;

void outbyte(char c)
{
    uint32_t h = g_trace_head;
    trace_buffer[h] = c;
    h = (h + 1U) % TRACE_RING_SIZE;
    g_trace_head = h;
}

/* libxilstandalone often references inbyte() too; we never read input
 * on this server, so just block. */
char inbyte(void)
{
    return 0;
}
