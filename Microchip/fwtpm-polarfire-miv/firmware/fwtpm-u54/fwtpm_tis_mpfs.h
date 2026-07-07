/* fwtpm_tis_mpfs.h
 *
 * Shared header for PolarFire SoC fwTPM IPC (server + client).
 * Defines the mailbox, console ring buffer, shared memory layout,
 * CLINT IPI macros, and memory barrier helpers.
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

#ifndef FWTPM_TIS_MPFS_H
#define FWTPM_TIS_MPFS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------- */
/* Shared memory layout in L2 LIM (Loosely-Integrated Memory)           */
/*                                                                       */
/*   0x08000000  FWTPM_MPFS_MAILBOX (64 B  -- mailbox + boot debug)     */
/*   0x08000040  FWTPM_CONSOLE_RING (4 KB -- server stdout)             */
/*   0x08001040  FWTPM_TIS_REGS     (~8.2 KB -- regs + cmd/rsp FIFOs)   */
/*   0x08004000  End (16 KB total)                                      */
/*                                                                       */
/* LIM is L2 SRAM repurposed as scratchpad, reachable by every master   */
/* over the L2 bus. The mailbox must be non-cached for the               */
/* bare-metal hart 4 to observe Linux's writes (U54 L1d is               */
/* write-back, no CMO); the default uses a non-cached DDR window.        */
/*                                                                       */
/* Transport is build-selectable via FWTPM_XPORT (see Makefile):        */
/*   DDR_WCB       default -- 0xC0000000 non-cached write-combine DDR    */
/*   LIM_CACHED    cacheable L2 LIM (needs HSS that sets hart-4 PMA)     */
/*   DDR_NONCACHED non-cached DDR alias 0x1400000000 + PMP               */
/*   L1D_OFF       DEAD END -- csrw 0x7C1 halts hart 4 (see startup.S)   */
/*   IHC           Inter-Hart Comm (stub; bitstream-gated)              */
/* See the README "Transport options" table.                            */
/* -------------------------------------------------------------------- */

/* Debug breadcrumb base: ALWAYS L2 LIM. startup.S writes progress/trap
 * fields here before any PMP/cache setup, so it must need no setup and
 * stay la-reachable under medany. Linux reads it via /dev/mem. */
#define FWTPM_DBG_BASE         0x08000000UL

/* Live mailbox + console ring + TIS regs base. DDR_WCB (the default) and
 * DDR_NONCACHED relocate it out of cacheable LIM; L1D_OFF and IHC keep the
 * LIM base. The debug breadcrumbs (FWTPM_DBG_BASE) always stay in LIM. */
#if defined(FWTPM_XPORT_DDR_NONCACHED)
    #define FWTPM_TIS_SHM_BASE 0x1400000000UL /* non-cached 64-bit DDR alias */
#elif defined(FWTPM_XPORT_DDR_WCB)
    #define FWTPM_TIS_SHM_BASE 0xC0000000UL   /* non-cached 32-bit DDR (WCB) */
#else
    #define FWTPM_TIS_SHM_BASE 0x08000000UL   /* L2 LIM */
#endif
#define FWTPM_TIS_SHM_SIZE     0x4000UL      /* 16 KB total */

/* Transport IDs reported in mailbox.xport_id so the Linux client can
 * confirm which transport build it is talking to. */
#define FWTPM_XPORT_ID_LIM_CACHED     1
#define FWTPM_XPORT_ID_DDR_NONCACHED  2
#define FWTPM_XPORT_ID_L1D_OFF        3
#define FWTPM_XPORT_ID_IHC            4
#define FWTPM_XPORT_ID_DDR_WCB        5

#if defined(FWTPM_XPORT_DDR_NONCACHED)
    #define FWTPM_XPORT_ID  FWTPM_XPORT_ID_DDR_NONCACHED
#elif defined(FWTPM_XPORT_DDR_WCB)
    #define FWTPM_XPORT_ID  FWTPM_XPORT_ID_DDR_WCB
#elif defined(FWTPM_XPORT_L1D_OFF)
    #define FWTPM_XPORT_ID  FWTPM_XPORT_ID_L1D_OFF
#elif defined(FWTPM_XPORT_IHC)
    #define FWTPM_XPORT_ID  FWTPM_XPORT_ID_IHC
#else
    #define FWTPM_XPORT_ID  FWTPM_XPORT_ID_LIM_CACHED
#endif

/* Mailbox magic: "FWTI" in little-endian */
#define FWTPM_MBOX_MAGIC        0x46575449UL
#define FWTPM_MBOX_VERSION      1

/* Trap handler sentinel: written to mailbox.trap_marker by _trap_vector
 * in startup.S whenever an M-mode trap fires on hart 4. */
#define FWTPM_TRAP_MAGIC        0xDEADC0DEUL

/* Progress markers (mailbox.progress field) */
#define FWTPM_PROG_ENTRY        0x10        /* _start reached */
#define FWTPM_PROG_MAIN         0x100       /* main() entered */
#define FWTPM_PROG_UART         0x2         /* mpfs_uart_init done */
#define FWTPM_PROG_PRINTF       0x3         /* first printf done */
#define FWTPM_PROG_NV_CLOCK     0x4         /* NV + clock HAL registered */
#define FWTPM_PROG_TIS_HAL      0x5         /* TIS HAL set */
#define FWTPM_PROG_INIT_DONE    0x6         /* FWTPM_Init returned */
#define FWTPM_PROG_SERVERLOOP   0x7         /* entered FWTPM_TIS_ServerLoop */
#define FWTPM_PROG_LOOP_EXIT    0x8         /* server loop returned */
#define FWTPM_PROG_TRAPCAUGHT   0xBADC0DE0UL /* _trap_vector ran */

/* -------------------------------------------------------------------- */
/* Mailbox: out-of-band signaling + boot debug for hart 4               */
/* Field offsets are referenced by absolute literals in startup.S       */
/* (FWTPM_OFF_PROGRESS, FWTPM_OFF_TRAP_*) -- keep layout in sync.        */
/* -------------------------------------------------------------------- */
typedef struct FWTPM_MPFS_MAILBOX {
    volatile uint32_t magic;          /* 0x00 FWTPM_MBOX_MAGIC */
    volatile uint32_t version;        /* 0x04 FWTPM_MBOX_VERSION */
    volatile uint32_t cmd_ready;      /* 0x08 Client sets nonce, server clears */
    volatile uint32_t rsp_ready;      /* 0x0C Server sets 1, client clears */
    volatile uint32_t server_alive;   /* 0x10 Server sets 1 after FWTPM_Init */
    volatile uint32_t progress;       /* 0x14 Boot progress (FWTPM_PROG_*) */
    volatile uint32_t rc;             /* 0x18 init status, then poll/liveness counter (see fwtpm_tis_mpfs.c) */
    volatile uint32_t echo_nonce;     /* 0x1C Server echoes client cmd nonce */
    volatile uint32_t trap_marker;    /* 0x20 FWTPM_TRAP_MAGIC if trapped */
    volatile uint32_t xport_id;       /* 0x24 FWTPM_XPORT_ID_* of this build */
    volatile uint64_t trap_mcause;    /* 0x28 csr mcause */
    volatile uint64_t trap_mepc;      /* 0x30 csr mepc */
    volatile uint64_t trap_mtval;     /* 0x38 csr mtval */
} FWTPM_MPFS_MAILBOX;                 /* 0x40 = 64 bytes */

/* cmd_ready protocol: the client writes a nonzero, changing nonce (not a
 * literal 1) to request service; the server copies it into echo_nonce
 * before clearing cmd_ready. A matching echo_nonce proves hart 4 saw the
 * new write, not a stale cached line. */

/* -------------------------------------------------------------------- */
/* Console ring buffer: fwTPM printf output readable from Linux          */
/*                                                                       */
/* Single producer (server), single consumer (Linux), no locking. The    */
/* server writes data[write_pos % size]; positions are 64-bit monotonic   */
/* counters (no practical wrap). Overwrite-oldest: when full the producer  */
/* advances read_pos and bumps overflow_cnt, so the newest output is      */
/* always kept (nothing drains this diagnostic ring). A reader takes the   */
/* last min(write_pos, size) bytes ending at write_pos % size. The 32-byte */
/* header keeps the shared-memory layout unchanged.                       */
/* -------------------------------------------------------------------- */
#define FWTPM_CONSOLE_RING_SIZE  4064  /* 4KB - 32 bytes header */

typedef struct FWTPM_CONSOLE_RING {
    volatile uint64_t write_pos;     /* Server's write position */
    volatile uint64_t read_pos;      /* Oldest retained byte (producer advances on overflow) */
    volatile uint32_t size;          /* Ring data size (FWTPM_CONSOLE_RING_SIZE) */
    volatile uint32_t overflow_cnt;  /* Dropped bytes counter */
    volatile uint32_t reserved[2];   /* Pad header to 32 bytes */
    volatile char     data[FWTPM_CONSOLE_RING_SIZE];
} FWTPM_CONSOLE_RING;

/* -------------------------------------------------------------------- */
/* Offsets from SHM base                                                */
/* -------------------------------------------------------------------- */
#define FWTPM_MBOX_OFFSET       0
#define FWTPM_CONSOLE_OFFSET    sizeof(FWTPM_MPFS_MAILBOX)       /* 64 */
#define FWTPM_TIS_REGS_OFFSET   (FWTPM_CONSOLE_OFFSET + \
                                 sizeof(FWTPM_CONSOLE_RING))     /* 64+4096=4160 */

/* -------------------------------------------------------------------- */
/* CLINT (Core Local Interruptor) -- IPI doorbells                       */
/* -------------------------------------------------------------------- */
#define CLINT_BASE              0x02000000UL

/* MSIP register: one per hart (0-4), 4 bytes each.
 * Write 1 to trigger M-mode software interrupt on that hart.
 * Write 0 to clear. */
#define CLINT_MSIP(hart) \
    (*(volatile uint32_t*)(CLINT_BASE + (uint32_t)(hart) * 4))

/* -------------------------------------------------------------------- */
/* Memory barriers (RISC-V fence instructions)                          */
/* -------------------------------------------------------------------- */
#define mpfs_fence_w()   __asm__ volatile("fence w,w"   ::: "memory")
#define mpfs_fence_r()   __asm__ volatile("fence r,r"   ::: "memory")
#define mpfs_fence_rw()  __asm__ volatile("fence rw,rw" ::: "memory")

#ifdef __cplusplus
}
#endif

#endif /* FWTPM_TIS_MPFS_H */
