/* xparameters.h
 *
 * Minimal hand-rolled xparameters for the stock ZCU102 in lock-step
 * Cortex-R5 mode. Used as a fallback when no Vitis BSP is staged
 * (Tier-1 compile-clean check). When the BSP is present, its own
 * xparameters.h takes precedence -- all XPAR_* defines below are
 * #ifndef-guarded so they are no-ops in Tier-2 builds.
 *
 * Source values: ZCU102 board files (ds925), Zynq UltraScale+ TRM
 * (ug1085), and the Xilinx ZCU102 default petalinux BSP.
 *
 * Copyright (C) 2006-2026 wolfSSL Inc.
 */

#ifndef FWTPM_ZCU102_XPARAMETERS_H
#define FWTPM_ZCU102_XPARAMETERS_H

/* CPU clock -- default ZCU102 R5 clock = 533.333 MHz. */
#ifndef XPAR_CPU_CORTEXR5_CORE_CLOCK_FREQ_HZ
#define XPAR_CPU_CORTEXR5_CORE_CLOCK_FREQ_HZ  533333000U
#endif

/* PSU UART (UART1 routed to USB-UART CP2108 on ZCU102). */
#ifndef XPAR_PSU_UART_1_BASEADDR
#define XPAR_PSU_UART_1_BASEADDR     0xFF010000U
#endif
#ifndef XPAR_PSU_UART_1_CLOCK_HZ
#define XPAR_PSU_UART_1_CLOCK_HZ     100000000U
#endif

/* QSPI controller (psu_qspi_0, dual-stacked MT25QU512). */
#ifndef XPAR_PSU_QSPI_0_BASEADDR
#define XPAR_PSU_QSPI_0_BASEADDR     0xFF0F0000U
#endif
#ifndef XPAR_XQSPIPSU_0_BASEADDR
#define XPAR_XQSPIPSU_0_BASEADDR     XPAR_PSU_QSPI_0_BASEADDR
#endif

/* IPI block 1 (RPU0 <-> APU0 channel on ZCU102 OpenAMP). */
#ifndef XPAR_PSU_IPI_1_BASE_ADDRESS
#define XPAR_PSU_IPI_1_BASE_ADDRESS  0xFF310000U
#endif
#ifndef XPAR_PSU_IPI_1_BIT_MASK
/* IPI channel-7 bit position in the trigger/ISR bitmap (PL3 channel,
 * shared with the OpenAMP "bare-metal-app" channel that the Linux
 * IPI mailbox driver listens on -- xlnx,ipi-id = 7). The R5 (channel
 * 1) writes (1 << 24) to its TRIG register to interrupt Linux on
 * channel 7. Setting 0x100 (bit 8) here previously targeted PMU
 * IPI 0 instead, so kick() from R5 never reached Linux and the
 * NS announce never propagated -- /dev/rpmsg0 stayed absent. */
#define XPAR_PSU_IPI_1_BIT_MASK      0x01000000U
#endif
#ifndef XPAR_XIPIPSU_0_BASEADDR
#define XPAR_XIPIPSU_0_BASEADDR      XPAR_PSU_IPI_1_BASE_ADDRESS
#endif
/* Number of IPI targets reachable from this RPU0 instance. ZynqMP
 * has 11 IPI channels but the XIpiPsu driver only iterates through
 * the per-instance target table; sized to the BSP default. */
#ifndef XPAR_XIPIPSU_0_IPI_TARGET_COUNT
#define XPAR_XIPIPSU_0_IPI_TARGET_COUNT 7U
#endif

/* GIC (RPU GIC400 PL390 view). */
#ifndef XPAR_SCUGIC_0_DIST_BASEADDR
#define XPAR_SCUGIC_0_DIST_BASEADDR  0xF9000000U
#endif
#ifndef XPAR_SCUGIC_0_CPU_BASEADDR
#define XPAR_SCUGIC_0_CPU_BASEADDR   0xF9001000U
#endif

/* RPU GIC interrupt ID for IPI_1 (RPU0 destination). Per ZynqMP TRM
 * (ug1085) table 13-1, IPI_1 fires GIC SPI 33 -> interrupt ID 65. */
#ifndef XPAR_PSU_IPI_1_INT_ID
#define XPAR_PSU_IPI_1_INT_ID        65U
#endif

/* ------------------------------------------------------------------ */
/* Carveouts -- must match PetaLinux system-user.dtsi reserved-memory */
/* ------------------------------------------------------------------ */
/* R5_0 firmware load region (matches rproc_0_reserved). */
#define ZCU102_R5_DDR_BASE     0x3ED00000U
#define ZCU102_R5_DDR_SIZE     0x00100000U   /* 1 MiB code + heap + stack */

/* OpenAMP shared carveouts -- MUST sit OUTSIDE rproc_0_reserved.
 * Earlier revisions placed these inside the firmware carveout, which
 * caused Linux's vring writes to land in R5 .text / .bss and wedged
 * both sides (rcu_sched stalls + SDHCI timeouts on the APU; silent
 * trace_buffer on the RPU). reserved-memory in system-user.dtsi MUST
 * mirror these addresses exactly. */
#define ZCU102_R5_VRING0_BASE  0x3EE00000U
#define ZCU102_R5_VRING0_SIZE  0x00004000U   /* 16 KiB */
#define ZCU102_R5_VRING1_BASE  0x3EE04000U
#define ZCU102_R5_VRING1_SIZE  0x00004000U   /* 16 KiB */
#define ZCU102_R5_BUF_BASE     0x3EE08000U
#define ZCU102_R5_BUF_SIZE     0x00040000U   /* 256 KiB rpmsg buffer pool */

/* QSPI NV partition (R5 owns; Linux must NOT mount).
 * MUST match the "fwtpm-nv" partition declared in
 * petalinux/.../recipes-bsp/device-tree/files/system-user.dtsi.
 * Last 64 KiB of dual-stacked MT25QU512 (128 MiB total). */
#define ZCU102_FWTPM_NV_QSPI_OFFSET  0x07FF0000U  /* last 64 KiB of 128 MiB */
#define ZCU102_FWTPM_NV_QSPI_SIZE    0x00010000U  /* 64 KiB */

/* Size of a single MT25QU512 die in the dual-stacked pair (64 MiB).
 * A flash address >= this value lives in the upper die and must be
 * addressed die-relative (addr - SINGLE) with CS_UPPER selected --
 * see qspi_select_addr() in fwtpm_nv_qspi.c. */
#define ZCU102_QSPI_SINGLE_FLASH_SIZE 0x04000000U  /* 64 MiB per die */

/* ------------------------------------------------------------------ */
/* TTC -- monotonic ms time base for the fwTPM clock HAL.             */
/* ------------------------------------------------------------------ */
/* The R5 PMU cycle counter wraps every ~8 s and only advances the
 * software high word when polled, so it loses time across the wfi idle
 * gaps in the rpmsg server loop. The clock HAL instead reads a
 * free-running ZynqMP TTC counter (see fwtpm_clock_zynqmp.c).
 *
 * VERIFY ON HARDWARE: the chosen TTC instance MUST NOT be used by
 * Linux -- mark it status="disabled" for the APU in system-user.dtsi,
 * or both sides will fight over the same counter. TTC3 is used here as
 * it is least likely to be claimed by the APU clocksource/clockevent.
 * ZCU102_TTC_CLK_HZ must match the TTC reference clock configured for
 * this design (PS LPD); 100 MHz is the common ZCU102 default. */
#ifndef XPAR_PSU_TTC_3_BASEADDR
#define XPAR_PSU_TTC_3_BASEADDR      0xFF140000U
#endif
#define ZCU102_TTC_BASEADDR          XPAR_PSU_TTC_3_BASEADDR
#ifndef ZCU102_TTC_CLK_HZ
#define ZCU102_TTC_CLK_HZ            100000000U
#endif

#endif /* FWTPM_ZCU102_XPARAMETERS_H */
