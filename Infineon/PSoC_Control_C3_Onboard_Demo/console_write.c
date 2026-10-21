/* console_write.c
 *
 * Route printf to the demo console.
 *
 * The acts emit their own events through demo_put and need none of this.
 * wolfTPM's and wolfSPDM's debug output goes through printf, though, so a
 * diagnostic build has to give newlib somewhere to put it. Only built when
 * DEBUG_WOLFTPM is on.
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
#ifdef DEBUG_WOLFTPM

#include <stddef.h>

void uart_write(const char* buf, unsigned int len);

int _write(int fd, const char* buf, int len)
{
    (void)fd;
    if (buf == NULL || len <= 0)
        return 0;
    uart_write(buf, (unsigned int)len);
    return len;
}

/* newlib wants these to link a printf; nothing here uses them. */
int _close(int fd) { (void)fd; return -1; }
int _fstat(int fd, void* st) { (void)fd; (void)st; return -1; }
int _isatty(int fd) { (void)fd; return 1; }
int _lseek(int fd, int off, int whence)
{
    (void)fd; (void)off; (void)whence;
    return -1;
}
int _read(int fd, char* buf, int len)
{
    (void)fd; (void)buf; (void)len;
    return -1;
}
void _exit(int status) { (void)status; while (1) ; }
int _kill(int pid, int sig) { (void)pid; (void)sig; return -1; }
int _getpid(void) { return 1; }

#endif /* DEBUG_WOLFTPM */
