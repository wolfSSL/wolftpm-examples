/* retarget.c
 *
 * Newlib bare-metal syscall stubs for the Zynq-7000 Cortex-A9 firmware. Routes
 * stdout and stderr to the console Cadence UART and provides a simple _sbrk
 * heap that grows from the linker 'end' symbol up to _heap_end.
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

#include "zynq7000.h"
#include "zynq_uart.h"
#include "zynq_time.h"

/* Linker-provided heap bounds. */
extern char end[];          /* start of heap (after .bss) */
extern char _heap_end[];

static char* heap_ptr = NULL;

int _write(int fd, const char* buf, int len)
{
    static char prev = 0;   /* last byte emitted, kept across calls */
    int i;

    (void)fd;
    if (buf == NULL) {
        return -1;
    }
    for (i = 0; i < len; i++) {
        /* Translate LF to CRLF, but do not double a CR the caller already sent
         * (avoids "\r\r\n" for the common explicit "\r\n" format strings). */
        if (buf[i] == '\n' && prev != '\r') {
            zynq_uart_putc(ZYNQ_CONSOLE_UART_BASE, '\r');
        }
        zynq_uart_putc(ZYNQ_CONSOLE_UART_BASE, buf[i]);
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
        return (len == 0) ? 0 : -1;   /* zero-length read returns 0 (POSIX) */
    }
    /* Block for at least one byte, then drain what is available. */
    while (count < len) {
        if (zynq_uart_getc(ZYNQ_CONSOLE_UART_BASE, &c)) {
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
    /* Reject growth past the heap top and, since incr is signed, any negative
     * increment that would rewind the cursor below the heap base. */
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

/* No RTC on this board; report a monotonic time since boot (from the Global
 * Timer) so the output object is always defined rather than stack garbage. Not
 * wall-clock, so the wolfCrypt builds also define NO_ASN_TIME. */
int _gettimeofday(struct timeval* tv, void* tz)
{
    uint64_t ms;

    (void)tz;
    if (tv == NULL) {
        return -1;
    }
    ms = zynq_millis();
    tv->tv_sec = (time_t)(ms / 1000u);
    tv->tv_usec = (suseconds_t)((ms % 1000u) * 1000u);
    return 0;
}
