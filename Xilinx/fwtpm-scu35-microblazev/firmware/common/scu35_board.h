/* scu35_board.h
 *
 * Board definition for a MicroBlaze V (RISC-V rv32imc) soft core on the AMD
 * Spartan UltraScale+ SCU35 Evaluation Kit (xcsu35p). Addresses and the core
 * clock match the AMD "SCU35 Zephyr RTOS IO" reference design used as the
 * hardware platform (AXI UARTLite / AXI Timer / AXI QuadSPI on the MicroBlaze V
 * AXI bus, local BRAM at the reset vector). If your bitstream uses different
 * addresses, a different clock, or more local memory, override the values below.
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

#ifndef SCU35_BOARD_H
#define SCU35_BOARD_H

#include <stdint.h>

/* MicroBlaze V AXI/core clock (Hz). The reference design runs the AXI domain at
 * 225 MHz (axi_uartlite/axi_timer C_S_AXI_ACLK_FREQ_HZ). */
#ifndef SCU35_SYS_CLK_FREQ
#define SCU35_SYS_CLK_FREQ      225000000UL
#endif

/* AXI peripheral base addresses (from the reference design). */
#ifndef SCU35_UARTLITE0_BASE
#define SCU35_UARTLITE0_BASE    0x40600000UL   /* header, not the USB-UART */
#endif
#ifndef SCU35_UARTLITE1_BASE
#define SCU35_UARTLITE1_BASE    0x40700000UL
#endif
#ifndef SCU35_TIMER0_BASE
#define SCU35_TIMER0_BASE       0x41C00000UL
#endif
#ifndef SCU35_QSPI0_BASE
#define SCU35_QSPI0_BASE        0x44A00000UL   /* AXI QuadSPI (NV flash) */
#endif

/* Console UART is a fixed-baud AXI UARTLite (configured in the bitstream). On
 * the SCU35 reference design the USB-UART is wired to UARTLite 1 (0x40700000)
 * - it is the design's "serial1" console; UARTLite 0 goes to a header. */
#ifndef SCU35_CONSOLE_UART_BASE
#define SCU35_CONSOLE_UART_BASE SCU35_UARTLITE1_BASE
#endif
#ifndef SCU35_CONSOLE_BAUD
#define SCU35_CONSOLE_BAUD      115200UL
#endif

/* Local BRAM at the reset vector. The stock reference design provides 192 KB
 * (0x00000000..0x0002FFFF), sized for its Zephyr image; that also holds the
 * minimal ECC-only fwTPM (FWTPM_TINY_ECC). Only the full RSA+ECC fwTPM needs a
 * larger-memory bitstream (see fpga/README.md). */
#ifndef SCU35_RAM_BASE
#define SCU35_RAM_BASE          0x00000000UL
#endif

#endif /* SCU35_BOARD_H */
