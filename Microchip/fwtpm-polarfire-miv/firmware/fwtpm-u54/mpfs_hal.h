/* mpfs_hal.h
 *
 * Minimal MPFS250T HAL for bare-metal U54 fwTPM context.
 * UART driver, MTIME, and console ring buffer support.
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

#ifndef MPFS_HAL_H
#define MPFS_HAL_H

#include <stdint.h>
#include "fwtpm_tis_mpfs.h"

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------- */
/* Clock configuration (post-HSS PLL init)                              */
/* -------------------------------------------------------------------- */
#define MSS_APB_AHB_CLK    150000000UL
#define MSS_CPU_CLK         600000000UL

/* -------------------------------------------------------------------- */
/* UART registers (MMUART, M-mode LO addresses)                        */
/* -------------------------------------------------------------------- */
#define MSS_UART0_LO_BASE  0x20000000UL
#define MSS_UART1_LO_BASE  0x20100000UL
#define MSS_UART2_LO_BASE  0x20102000UL
#define MSS_UART3_LO_BASE  0x20104000UL
#define MSS_UART4_LO_BASE  0x20106000UL

/* fwTPM uses UART4 (hart 4's UART) if physically routed, else console ring */
#define FWTPM_UART_BASE     MSS_UART4_LO_BASE

/* PolarFire SoC MSS MMUART extended register map -- NOT a standard 16550:
 * TX holding (THR 0x100) and FIFO control (FCR 0x104) sit well above the
 * 16550 block, and the divisor latches are separate registers (DLR 0x80 /
 * DMR 0x84) rather than overlapping RBR/IER under DLAB. Offsets confirmed
 * against the wolfBoot mpfs250 driver (hal/mpfs250.h MMUART_* macros). */
#define MMUART_RBR(base) (*((volatile uint8_t*)((base)) + 0x00))
#define MMUART_IER(base) (*((volatile uint8_t*)((base)) + 0x04))
#define MMUART_IIR(base) (*((volatile uint8_t*)((base)) + 0x08))
#define MMUART_LCR(base) (*((volatile uint8_t*)((base)) + 0x0C))
#define MMUART_MCR(base) (*((volatile uint8_t*)((base)) + 0x10))
#define MMUART_LSR(base) (*((volatile uint8_t*)((base)) + 0x14))
#define MMUART_MM0(base) (*((volatile uint8_t*)((base)) + 0x30))
#define MMUART_MM1(base) (*((volatile uint8_t*)((base)) + 0x34))
#define MMUART_MM2(base) (*((volatile uint8_t*)((base)) + 0x38))
#define MMUART_DFR(base) (*((volatile uint8_t*)((base)) + 0x3C))
#define MMUART_DLR(base) (*((volatile uint8_t*)((base)) + 0x80))
#define MMUART_DMR(base) (*((volatile uint8_t*)((base)) + 0x84))
#define MMUART_THR(base) (*((volatile uint8_t*)((base)) + 0x100))
#define MMUART_FCR(base) (*((volatile uint8_t*)((base)) + 0x104))

/* Line Control Register */
#define MSS_UART_DATA_8_BITS    ((uint8_t)0x03)
#define DLAB_MASK                (1U << 7)

/* Line Status Register */
#define MSS_UART_DR              ((uint8_t)0x01)
#define MSS_UART_THRE            ((uint8_t)0x20)

/* -------------------------------------------------------------------- */
/* MTIME (memory-mapped CLINT timer)                                    */
/*                                                                       */
/* rdtime CSR is ILLEGAL in M-mode on PolarFire SoC. Read the CLINT     */
/* mtime register directly instead.                                     */
/* -------------------------------------------------------------------- */
#define CLINT_MTIME  (*(volatile uint64_t*)(CLINT_BASE + 0xBFF8))

static inline uint64_t mpfs_rdtime(void)
{
    return CLINT_MTIME;
}

/* MTIME frequency on PolarFire SoC: 1 MHz */
#define MTIME_FREQ  1000000UL

/* -------------------------------------------------------------------- */
/* Function prototypes                                                   */
/* -------------------------------------------------------------------- */

/* UART */
void mpfs_uart_init(uint32_t base, uint32_t baud_clk, uint32_t baud_rate);
void mpfs_uart_putc(uint32_t base, char c);
void mpfs_uart_puts(uint32_t base, const char *s);
int  mpfs_uart_getc(uint32_t base);

/* Console ring buffer (shared-memory stdout) */
void mpfs_console_init(FWTPM_CONSOLE_RING *ring);
void mpfs_console_putc(FWTPM_CONSOLE_RING *ring, char c);

/* RNG seed callbacks (CUSTOM_RAND_GENERATE_SEED). Selected by FWTPM_RNG:
 *   SCB_NONCE (default) -- mpfs_rng_seed_scb, System Controller HW TRNG
 *   JITTER              -- mpfs_rng_seed_cb,  MTIME jitter, DEVELOPMENT ONLY */
int mpfs_rng_seed_cb(unsigned char *output, unsigned int sz);
int mpfs_rng_seed_scb(unsigned char *output, unsigned int sz);

#ifdef __cplusplus
}
#endif

#endif /* MPFS_HAL_H */
