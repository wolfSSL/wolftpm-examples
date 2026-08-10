/* miv_board.h
 *
 * Board definition for the Mi-V RV32 soft core on the Microchip PolarFire
 * MPF300 Splash Kit. Peripheral base addresses and clocking match the
 * "RISC-V for PolarFire" Splash Kit Mi-V reference design (Libero 2025.1):
 * MIV_RV32 core plus CoreUARTapb, CoreGPIO and CoreTimer on an APB3 bus.
 *
 * If your Libero design uses different addresses or a different core clock,
 * override the values below (see fpga/README.md).
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

#ifndef MIV_BOARD_H
#define MIV_BOARD_H

#include <stdint.h>

/* Mi-V soft processor clock (Hz). Set to the design's CCC/PLL output. */
#ifndef MIV_SYS_CLK_FREQ
#define MIV_SYS_CLK_FREQ        80000000UL
#endif

/* APB peripheral base addresses (from the Splash Kit Mi-V reference design). */
#ifndef MIV_COREUARTAPB0_BASE
#define MIV_COREUARTAPB0_BASE   0x70000000UL
#endif
#ifndef MIV_COREGPIO_OUT_BASE
#define MIV_COREGPIO_OUT_BASE   0x70001000UL
#endif
#ifndef MIV_CORETIMER0_BASE
#define MIV_CORETIMER0_BASE     0x70002000UL
#endif
#ifndef MIV_CORESYSSERV_BASE
#define MIV_CORESYSSERV_BASE    0x70003000UL   /* CoreSysServices_PF (APB slot 3) */
#endif

/* Timekeeping source: CoreTimer0 (see miv_time.c). This design's MIV_RV32
 * implements neither the memory-mapped MTIME block (a load from 0x4400BFF8
 * faults) nor the mcycle/minstret performance counters (hardwired to 0), so
 * timing is derived from the CoreTimer free-running down-counter instead.
 *
 * The prescaler is set to /128 (not the /2 minimum) so the 32-bit down-counter
 * wraps only every 2^32 / (80 MHz/128) = ~1.9 hours. miv_ticks() extends it to
 * 64 bits by accumulating between calls and must be called at least once per
 * wrap; a ~1.9 h window is far longer than any single TPM command (RSA keygen /
 * ML-DSA-87 sign are minutes), so no wrap is ever dropped even while a long
 * command runs. 625 kHz still gives ms-accurate timing (1.6 us resolution). */
#define MIV_CORETIMER_PRESCALE          6U      /* PRESCALER_DIV_128 = 2^(N+1) */
#define MIV_CORETIMER_CLK_FREQ          (MIV_SYS_CLK_FREQ / 128U)

/* Console UART configuration. */
#ifndef MIV_CONSOLE_BAUD
#define MIV_CONSOLE_BAUD        115200UL
#endif

#endif /* MIV_BOARD_H */
