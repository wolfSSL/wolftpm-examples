/* retarget.c
 *
 * Newlib bare-metal syscall stubs for the MicroBlaze V firmware. Routes stdout
 * and stderr to the console AXI UARTLite and provides a simple _sbrk heap that
 * grows from the linker 'end' symbol up to _heap_end.
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

#include <errno.h>
#include <stddef.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>
#include <unistd.h>

#include "scu35_board.h"
#include "mbv_uart.h"
#include "mbv_time.h"

/* Linker-provided heap bounds. */
extern char end[];      /* start of heap (after .bss) */
extern char _heap_end[];

static char* heap_ptr = NULL;

#ifndef SCU35_CONSOLE_UART_BASE
/* Fallback only; scu35_board.h defines this as UARTLITE1 (the USB-UART console,
 * design serial1). Kept in sync so stdout never routes to the wrong UART. */
#define SCU35_CONSOLE_UART_BASE   SCU35_UARTLITE1_BASE
#endif

int _write(int fd, const char* buf, int len)
{
    static char prev = 0;   /* last byte emitted, kept across calls */
    int i;

    (void)fd;
    if (buf == NULL) {
        return -1;
    }
    for (i = 0; i < len; i++) {
        /* Translate LF to CRLF, but do not double a CR the caller already sent. */
        if (buf[i] == '\n' && prev != '\r') {
            mbv_uart_putc(SCU35_CONSOLE_UART_BASE, '\r');
        }
        mbv_uart_putc(SCU35_CONSOLE_UART_BASE, buf[i]);
        prev = buf[i];
    }
    return len;
}

int _read(int fd, char* buf, int len)
{
    int count = 0;
    uint8_t c;

    (void)fd;
    if (buf == NULL) {
        return -1;
    }
    if (len <= 0) {
        return (len == 0) ? 0 : -1;
    }
    while (count < len) {
        if (mbv_uart_getc(SCU35_CONSOLE_UART_BASE, &c)) {
            buf[count++] = (char)c;
            if (c == '\n' || c == '\r') {
                break;
            }
        }
        else if (count > 0) {
            break;
        }
    }
    return count;
}

void* _sbrk(ptrdiff_t incr)
{
    char* prev;
    char* next;

    if (heap_ptr == NULL) {
        heap_ptr = end;
    }
    next = heap_ptr + incr;
    if (next < end || next > _heap_end) {
        errno = ENOMEM;
        return (void*)-1;
    }
    prev = heap_ptr;
    heap_ptr = next;
    return (void*)prev;
}

int _close(int fd)
{
    (void)fd;
    return -1;
}

int _fstat(int fd, struct stat* st)
{
    (void)fd;
    if (st == NULL) {
        return -1;
    }
    st->st_mode = S_IFCHR;
    return 0;
}

int _isatty(int fd)
{
    (void)fd;
    return 1;
}

off_t _lseek(int fd, off_t offset, int whence)
{
    (void)fd;
    (void)offset;
    (void)whence;
    return 0;
}

int _getpid(void)
{
    return 1;
}

int _kill(int pid, int sig)
{
    (void)pid;
    (void)sig;
    errno = EINVAL;
    return -1;
}

void _exit(int code)
{
    (void)code;
    for (;;) {
        /* park */
    }
}

/* No RTC; report a monotonic time since boot (from AXI Timer) so the output
 * object is always defined. Not wall-clock, so wolfCrypt also defines
 * NO_ASN_TIME. */
int _gettimeofday(struct timeval* tv, void* tz)
{
    uint64_t ms;

    (void)tz;
    if (tv == NULL) {
        return -1;
    }
    ms = mbv_millis();
    tv->tv_sec = (time_t)(ms / 1000U);
    tv->tv_usec = (suseconds_t)((ms % 1000U) * 1000U);
    return 0;
}
