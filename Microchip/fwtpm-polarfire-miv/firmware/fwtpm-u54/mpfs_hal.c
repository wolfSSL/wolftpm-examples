/* mpfs_hal.c
 *
 * Minimal MPFS250T HAL for bare-metal U54 fwTPM context.
 * UART driver, console ring buffer, and RNG seed.
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

#include "user_settings.h"
#include "mpfs_hal.h"
#include <stdint.h>
#include <string.h>

/* -------------------------------------------------------------------- */
/* Console ring buffer pointer (set by main after SHM init)             */
/* -------------------------------------------------------------------- */
static FWTPM_CONSOLE_RING *g_console_ring = NULL;

/* -------------------------------------------------------------------- */
/* UART                                                                  */
/* -------------------------------------------------------------------- */

void mpfs_uart_init(uint32_t base, uint32_t baud_clk, uint32_t baud_rate)
{
    uint32_t divisor;
    /* MSS MMUART uses 16x oversampling: integer divisor = PCLK/(16*baud)
     * (matches the wolfBoot mpfs250 UART driver). Omitting the 16 would
     * run the line 16x too slow. */
    divisor = baud_clk / (baud_rate * 16);

    MMUART_FCR(base) = 0x00;
    MMUART_LCR(base) = MSS_UART_DATA_8_BITS | DLAB_MASK;
    MMUART_DLR(base) = (uint8_t)(divisor & 0xFF);
    MMUART_DMR(base) = (uint8_t)((divisor >> 8) & 0xFF);
    MMUART_LCR(base) = MSS_UART_DATA_8_BITS;
    MMUART_MM0(base) = 0;
    MMUART_MM1(base) = 0;
    MMUART_MM2(base) = 0;
    MMUART_IER(base) = 0;
}

void mpfs_uart_putc(uint32_t base, char c)
{
    uint64_t start = mpfs_rdtime();

    /* Bound the THRE wait (~10 ms at the 1 MHz MTIME tick) so an unrouted
     * or stalled UART4 cannot hang the server; the console ring is the
     * primary readback path. Drop the byte on timeout. */
    while ((MMUART_LSR(base) & MSS_UART_THRE) == 0) {
        if ((mpfs_rdtime() - start) > (MTIME_FREQ / 100))
            return;
    }
    MMUART_THR(base) = (uint8_t)c;
}

void mpfs_uart_puts(uint32_t base, const char *s)
{
    while (*s != '\0') {
        if (*s == '\n')
            mpfs_uart_putc(base, '\r');
        mpfs_uart_putc(base, *s++);
    }
}

int mpfs_uart_getc(uint32_t base)
{
    if (MMUART_LSR(base) & MSS_UART_DR)
        return MMUART_RBR(base);
    return -1;
}

/* -------------------------------------------------------------------- */
/* Console ring buffer                                                   */
/* -------------------------------------------------------------------- */

void mpfs_console_init(FWTPM_CONSOLE_RING *ring)
{
    ring->write_pos = 0;
    ring->read_pos = 0;
    ring->size = FWTPM_CONSOLE_RING_SIZE;
    ring->overflow_cnt = 0;
    memset((void *)ring->reserved, 0, sizeof(ring->reserved));
    memset((void *)ring->data, 0, FWTPM_CONSOLE_RING_SIZE);
    mpfs_fence_w();
    g_console_ring = ring;
}

void mpfs_console_putc(FWTPM_CONSOLE_RING *ring, char c)
{
    uint64_t wp = ring->write_pos;
    uint64_t rp = ring->read_pos;

    /* Overwrite-oldest: nothing drains this diagnostic ring, so when full
     * drop the OLDEST byte (advance read_pos, count it) rather than the
     * newest -- keeps the most recent output. */
    if ((wp - rp) >= ring->size) {
        ring->overflow_cnt++;
        ring->read_pos = rp + 1;
    }

    ring->data[wp % ring->size] = c;
#if defined(FWTPM_XPORT_DDR_WCB)
    /* DDR_WCB: the ring lives in the non-cached write-combine window, so
     * publish the byte and then the write_pos with a full fence rw,rw on
     * each side -- the same ordering the mailbox poll path uses. A bare
     * fence w,w (used below for LIM/other transports) proved insufficient
     * to force the write-combine buffer out to DDR for the mailbox
     * (see TisMpfsWaitRequest), so the ring producer must not rely on it
     * either: without this a Linux reader could observe an advanced
     * write_pos ahead of the data byte it gates. */
    mpfs_fence_rw();
    ring->write_pos = wp + 1;
    mpfs_fence_rw();
#else
    mpfs_fence_w();
    ring->write_pos = wp + 1;
#endif
}

/* -------------------------------------------------------------------- */
/* printf backend (newlib _write syscall)                                */
/*                                                                       */
/* Writes to the console ring (read from Linux via /dev/mem) and UART4.   */
/* The ring is written FIRST -- it is the primary readback path (UART4    */
/* may be unrouted), so it is populated even if the UART TX wait times    */
/* out.                                                                   */
/* -------------------------------------------------------------------- */

int _write(int fd, const char *buf, int len)
{
    int i;
    (void)fd;
    for (i = 0; i < len; i++) {
        if (buf[i] == '\n') {
            if (g_console_ring != NULL)
                mpfs_console_putc(g_console_ring, '\r');
            mpfs_uart_putc(FWTPM_UART_BASE, '\r');
        }
        if (g_console_ring != NULL)
            mpfs_console_putc(g_console_ring, buf[i]);
        mpfs_uart_putc(FWTPM_UART_BASE, buf[i]);
    }
    return len;
}

/* -------------------------------------------------------------------- */
/* Bare-metal stubs for POSIX functions used by wolfTPM fwtpm_tis.c     */
/* -------------------------------------------------------------------- */

/* fwtpm_tis.c calls sigaction()/sigemptyset() to ignore SIGPIPE -- not
 * needed bare-metal. Keep struct sigaction an incomplete (opaque) type
 * rather than redeclaring a guessed layout: these stubs only take it by
 * pointer and ignore it, so there is no risk of an ABI/layout mismatch
 * with the toolchain's <signal.h> in other translation units. */
struct sigaction;
int sigemptyset(void *set) { (void)set; return 0; }
int sigaction(int sig, const struct sigaction *act, void *old)
{
    (void)sig; (void)act; (void)old;
    return 0;
}
#ifndef SIG_IGN
#define SIG_IGN 0
#endif

/* fwtpm_io.c stop-request check -- bare-metal never stops */
int FWTPM_IO_IsStopRequested(void) { return 0; }

/* fwtpm_tis.c FWTPM_TIS_Init falls back to FWTPM_TIS_SetDefaultHAL when
 * no HAL is registered. That fallback is the POSIX shared-memory variant
 * (fwtpm_tis_shm.c) which we don't compile in. We always register our
 * own HAL before init, so the fallback is dead code at runtime -- but
 * the linker still needs the symbol. */
struct FWTPM_CTX;
void FWTPM_TIS_SetDefaultHAL(struct FWTPM_CTX *ctx) { (void)ctx; }

/* -------------------------------------------------------------------- */
/* RNG seed (MTIME jitter -- DEVELOPMENT ONLY)                           */
/*                                                                       */
/* Built only for the opt-in FWTPM_RNG=JITTER backend (NOT the default). */
/* The default SCB_NONCE backend (fwtpm_trng_scb.c) supplies             */
/* mpfs_rng_seed_scb instead and needs no FWTPM_DEV_INSECURE_RNG opt-in. */
/* -------------------------------------------------------------------- */
#ifndef FWTPM_RNG_SCB_NONCE

/*                                                                       */
/* Derives DRBG seed bytes from MTIME read jitter mixed through an LCG.  */
/* This is low-entropy and largely predictable across boots and MUST NOT */
/* be used to generate production keys. Replace with the PolarFire SoC   */
/* hardware TRNG / User Crypto block before any non-development use.      */
/*                                                                       */
/* The build must opt in explicitly on the command line                  */
/* (make FWTPM_RNG=JITTER FWTPM_DEV_INSECURE_RNG=1) so this stub can      */
/* never be linked in silently. The #warning fires on every such build   */
/* as a standing reminder.                                               */
/* -------------------------------------------------------------------- */
#ifndef FWTPM_DEV_INSECURE_RNG
#error "mpfs_rng_seed_cb is a development-only MTIME-jitter entropy source. Build the default FWTPM_RNG=SCB_NONCE (hardware TRNG), or acknowledge the weak dev seed with FWTPM_RNG=JITTER FWTPM_DEV_INSECURE_RNG=1."
#endif
#warning "fwTPM RNG seeded from MTIME jitter (FWTPM_DEV_INSECURE_RNG) -- DEV ONLY, do not generate production keys"

int mpfs_rng_seed_cb(unsigned char *output, unsigned int sz)
{
    unsigned int i;
    uint64_t t;
    uint64_t mix = 0;
    volatile int k;

    for (i = 0; i < sz; i++) {
        t = mpfs_rdtime();
        mix ^= t;
        mix = (mix * 6364136223846793005ULL) + 1442695040888963407ULL;
        output[i] = (unsigned char)(mix >> 33);
        /* Short delay so successive MTIME reads land on different ticks. */
        for (k = 0; k < 8; k++) { }
    }
    return 0;
}

#endif /* !FWTPM_RNG_SCB_NONCE */
