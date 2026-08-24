/* zynq7000.h
 *
 * Board definition for the AMD/Xilinx Zynq-7000 (ZC702 / ZedBoard / MicroZed)
 * Cortex-A9 processing system (PS). Peripheral base addresses and the fixed PS
 * clocks below match the stock Zynq-7000 memory map; the prebuilt FSBL does the
 * ps7_init (DDR, MIO/pinmux, PLLs, UART) before this firmware is loaded over
 * JTAG, so the values here describe the running PS rather than program it.
 *
 * If a board routes the console to a different UART or clocks the PS
 * differently, override the values below.
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

#ifndef ZYNQ7000_H
#define ZYNQ7000_H

#include <stdint.h>

/* --- PS peripheral base addresses (Zynq-7000 TRM UG585) --- */

/* Cadence (Zynq) UART controllers. The ZC702 USB-UART is wired to UART1. */
#ifndef ZYNQ_UART0_BASE
#define ZYNQ_UART0_BASE         0xE0000000UL
#endif
#ifndef ZYNQ_UART1_BASE
#define ZYNQ_UART1_BASE         0xE0001000UL
#endif

/* Console UART used by the drivers and newlib retarget. */
#ifndef ZYNQ_CONSOLE_UART_BASE
#define ZYNQ_CONSOLE_UART_BASE  ZYNQ_UART1_BASE
#endif
#ifndef ZYNQ_CONSOLE_BAUD
#define ZYNQ_CONSOLE_BAUD       115200UL
#endif

/* Cortex-A9 MPCore private/global peripherals (SCU region at 0xF8F00000). The
 * 64-bit Global Timer is the free-running time base (the A9 has no ARMv7
 * generic timer / CNTPCT). It is clocked at CPU_3x2x = 1/2 the CPU clock:
 * 333.333 MHz on the ZC702 (667 MHz A9). Override for other CPU frequencies. */
#ifndef ZYNQ_GLOBAL_TIMER_BASE
#define ZYNQ_GLOBAL_TIMER_BASE  0xF8F00200UL
#endif
#ifndef ZYNQ_GLOBAL_TIMER_FREQ
#define ZYNQ_GLOBAL_TIMER_FREQ  333333333UL
#endif

/* System Level Control Registers (clocks, resets, OCM_CFG remap). */
#ifndef ZYNQ_SLCR_BASE
#define ZYNQ_SLCR_BASE          0xF8000000UL
#endif

/* Quad-SPI controller (linear/IO mode) for persistent NV (opt-in). */
#ifndef ZYNQ_QSPI_BASE
#define ZYNQ_QSPI_BASE          0xE000D000UL
#endif

/* On-Chip Memory (256 KB). After the SLCR OCM_CFG remap all four banks sit
 * high at 0xFFFC0000..0xFFFFFFFF. A carve-out near the top serves as the
 * physical SRAM PUF source (uninitialized at cold power-on). */
#ifndef ZYNQ_OCM_HIGH_BASE
#define ZYNQ_OCM_HIGH_BASE      0xFFFC0000UL
#endif
#ifndef ZYNQ_OCM_SIZE
#define ZYNQ_OCM_SIZE           0x00040000UL   /* 256 KB */
#endif

/* DDR base and the load/link address for this firmware. The FSBL brings up
 * DDR; we link into it well above the FSBL's low-DDR usage. */
#ifndef ZYNQ_DDR_BASE
#define ZYNQ_DDR_BASE           0x00000000UL
#endif
#ifndef ZYNQ_LOAD_ADDR
#define ZYNQ_LOAD_ADDR          0x04000000UL
#endif

#endif /* ZYNQ7000_H */
