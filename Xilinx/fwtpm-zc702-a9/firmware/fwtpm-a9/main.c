/* main.c
 *
 * wolfTPM firmware TPM (fwTPM) on the Zynq-7000 Cortex-A9 (ZC702). Registers the
 * NV / clock HALs, initializes the fwTPM engine, runs a small standalone
 * self-test (TPM2_Startup -> TPM2_GetRandom), then serves TPM2 commands over the
 * Cadence UART using the same framing as the STM32H5 and Mi-V ports (raw swtpm +
 * Microsoft-simulator "mssim"), so the wolfTPM swtpm client can drive it from a
 * host.
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

#include "user_settings.h"
#include <wolftpm/fwtpm/fwtpm.h>
#include <wolftpm/fwtpm/fwtpm_nv.h>
#include <wolftpm/fwtpm/fwtpm_command.h>

#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "zynq7000.h"
#include "zynq_uart.h"
#include "zynq_time.h"
#include "fwtpm_puf.h"

/* Port HAL initializers (this example). NV is volatile RAM by default; build
 * -DFWTPM_NV_QSPI for persistent NV + PUF helper data in the QSPI flash. */
#ifdef FWTPM_NV_QSPI
extern int FWTPM_NV_QSPI_Init(FWTPM_NV_HAL* hal);
#else
extern int zynq_nv_ram_init(FWTPM_NV_HAL* hal);
#endif
extern int FWTPM_Clock_ZYNQ_Init(FWTPM_CTX* ctx);

/* Static fwTPM context (large - keep off the stack). */
static FWTPM_CTX g_ctx;

/* mssim platform command codes (Microsoft TPM simulator protocol). */
#define MSSIM_SIGNAL_POWER_ON   1
#define MSSIM_SIGNAL_POWER_OFF  2
#define MSSIM_SEND_COMMAND      8
#define MSSIM_SIGNAL_RESET      17
#define MSSIM_SESSION_END       20
#define MSSIM_STOP              21

/* Once a frame has started, the rest of it must arrive within this window; on
 * expiry the parser resets rather than blocking forever on a partial frame. */
#define FWTPM_FRAME_TIMEOUT_MS   2000U

/* Minimal well-formed TPM_RC_FAILURE response (10-byte header) used to unblock
 * a raw-transport host after a malformed or stalled frame. */
static const uint8_t g_tpmRcFailure[10] = {
    0x80, 0x01, 0x00, 0x00, 0x00, 0x0A, 0x00, 0x00, 0x01, 0x01
};

/* ---- raw UART byte transport over the Cadence UART ---- */
static int UartRecv(uint8_t* buf, uint32_t sz)
{
    uint32_t i;
    for (i = 0; i < sz; i++) {
        while (zynq_uart_getc(ZYNQ_CONSOLE_UART_BASE, &buf[i]) == 0) {
            /* spin until a byte arrives (Global Timer is free-running HW) */
        }
    }
    return 0;
}

/* Bounded receive for the remainder of an in-progress frame: like UartRecv but
 * gives up with -1 if no byte arrives within timeoutMs (inter-byte deadline),
 * so a sender that stops mid-frame cannot wedge the command loop. */
static int UartRecvTO(uint8_t* buf, uint32_t sz, uint32_t timeoutMs)
{
    uint32_t i;
    uint64_t deadline;

    for (i = 0; i < sz; i++) {
        deadline = zynq_millis() + timeoutMs;
        while (zynq_uart_getc(ZYNQ_CONSOLE_UART_BASE, &buf[i]) == 0) {
            if (zynq_millis() >= deadline) {
                return -1;
            }
        }
    }
    return 0;
}

static int UartSend(const uint8_t* buf, uint32_t sz)
{
    zynq_uart_write(ZYNQ_CONSOLE_UART_BASE, buf, sz);
    return 0;
}

/* Discard up to maxBytes of pending input, stopping early once the stream is
 * idle for ~10 ms - resyncs the raw transport after a malformed frame without
 * blocking indefinitely on a bogus declared length. */
static void UartDrain(uint32_t maxBytes)
{
    uint8_t b;
    uint32_t got = 0U;
    uint64_t idleStart = zynq_millis();

    /* Cap the resync drain regardless of the (untrusted) declared frame length,
     * so a peer that keeps the line busy cannot force an unbounded drain. */
    if (maxBytes > FWTPM_MAX_COMMAND_SIZE) {
        maxBytes = FWTPM_MAX_COMMAND_SIZE;
    }
    while (got < maxBytes) {
        if (zynq_uart_getc(ZYNQ_CONSOLE_UART_BASE, &b) != 0) {
            got++;
            idleStart = zynq_millis();
        }
        else if ((zynq_millis() - idleStart) > 10U) {
            break;
        }
    }
}

static uint32_t LoadU32BE(const uint8_t* p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8)  |  (uint32_t)p[3];
}

static void StoreU32BE(uint8_t* p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static void UartSendAck(void)
{
    uint8_t ack[4] = { 0, 0, 0, 0 };
    UartSend(ack, 4);
}

/* ---- standalone self-test: drive a couple of TPM2 commands directly ---- */
static void FwTPM_SelfTest(FWTPM_CTX* ctx)
{
    /* TPM2_Startup(SU_CLEAR) */
    static const uint8_t cmdStartup[] = {
        0x80, 0x01, 0x00, 0x00, 0x00, 0x0C, 0x00, 0x00, 0x01, 0x44, 0x00, 0x00
    };
    /* TPM2_GetRandom(16) */
    static const uint8_t cmdGetRandom[] = {
        0x80, 0x01, 0x00, 0x00, 0x00, 0x0C, 0x00, 0x00, 0x01, 0x7B, 0x00, 0x10
    };
    int rspSize;
    uint32_t rc;
    int i;

    printf("Self-test:\r\n");

    rspSize = FWTPM_MAX_COMMAND_SIZE;
    FWTPM_ProcessCommand(ctx, cmdStartup, (int)sizeof(cmdStartup),
        ctx->rspBuf, &rspSize, 0);
    rc = (rspSize >= 10) ? LoadU32BE(ctx->rspBuf + 6) : 0xFFFFFFFFu;
    printf("  TPM2_Startup      rc=0x%08lX %s\r\n", (unsigned long)rc,
        (rc == 0) ? "OK" : "FAIL");

    rspSize = FWTPM_MAX_COMMAND_SIZE;
    FWTPM_ProcessCommand(ctx, cmdGetRandom, (int)sizeof(cmdGetRandom),
        ctx->rspBuf, &rspSize, 0);
    rc = (rspSize >= 10) ? LoadU32BE(ctx->rspBuf + 6) : 0xFFFFFFFFu;
    printf("  TPM2_GetRandom    rc=0x%08lX %s", (unsigned long)rc,
        (rc == 0) ? "OK bytes=" : "FAIL");
    /* response: hdr(10) + TPM2B size(2) + random bytes */
    if (rc == 0 && rspSize >= 12) {
        int n = (int)(((uint16_t)ctx->rspBuf[10] << 8) | ctx->rspBuf[11]);
        for (i = 0; i < n && (12 + i) < rspSize; i++) {
            printf("%02X", ctx->rspBuf[12 + i]);
        }
    }
    printf("\r\n");
}

/* ---- TPM2 command server over UART (raw swtpm + mssim framing) ---- */
static void FwTPM_UartCommandLoop(FWTPM_CTX* ctx)
{
    uint8_t hdr[4];
    uint16_t tag;
    uint32_t mssimCmd;
    uint8_t locality;
    uint32_t cmdSize, remaining;
    int rspSize;
    uint32_t rspSzOut;
    uint8_t rspHdr[4];

    for (;;) {
        if (UartRecv(hdr, 4) != 0) {
            continue;
        }

        /* Raw swtpm: TPM command begins with tag 0x8001 or 0x8002. */
        tag = ((uint16_t)hdr[0] << 8) | (uint16_t)hdr[1];
        if (tag == 0x8001 || tag == 0x8002) {
            memcpy(ctx->cmdBuf, hdr, 4);
            if (UartRecvTO(ctx->cmdBuf + 4, 6, FWTPM_FRAME_TIMEOUT_MS) != 0) {
                UartSend(g_tpmRcFailure, sizeof(g_tpmRcFailure));
                continue;
            }
            cmdSize = LoadU32BE(ctx->cmdBuf + 2);
            if (cmdSize < 10 || cmdSize > FWTPM_MAX_COMMAND_SIZE) {
                if (cmdSize > 10U) {
                    UartDrain(cmdSize - 10U);
                }
                UartSend(g_tpmRcFailure, sizeof(g_tpmRcFailure));
                continue;
            }
            remaining = cmdSize - 10;
            if (remaining > 0) {
                if (UartRecvTO(ctx->cmdBuf + 10, remaining,
                               FWTPM_FRAME_TIMEOUT_MS) != 0) {
                    UartSend(g_tpmRcFailure, sizeof(g_tpmRcFailure));
                    continue;
                }
            }
            rspSize = FWTPM_MAX_COMMAND_SIZE;
            FWTPM_ProcessCommand(ctx, ctx->cmdBuf, (int)cmdSize,
                ctx->rspBuf, &rspSize, 0);
            if (rspSize > 0) {
                UartSend(ctx->rspBuf, (uint32_t)rspSize);
            }
            else {
                UartSend(g_tpmRcFailure, sizeof(g_tpmRcFailure));
            }
            continue;
        }

        /* mssim: first 4 bytes are a big-endian platform command code. */
        mssimCmd = LoadU32BE(hdr);
        if (mssimCmd == MSSIM_SESSION_END) {
            continue;
        }
        if (mssimCmd == MSSIM_STOP) {
            UartSendAck();
            return;
        }
        if (mssimCmd == MSSIM_SIGNAL_POWER_ON) {
            ctx->powerOn = 1;
            UartSendAck();
            continue;
        }
        if (mssimCmd == MSSIM_SIGNAL_POWER_OFF) {
            ctx->powerOn = 0;
            ctx->wasStarted = 0;
            UartSendAck();
            continue;
        }
        if (mssimCmd == MSSIM_SIGNAL_RESET) {
            ctx->wasStarted = 0;
            UartSendAck();
            continue;
        }
        if (mssimCmd != MSSIM_SEND_COMMAND) {
            UartSendAck();
            continue;
        }

        /* SEND_COMMAND: locality(1) + cmdSize(4) + payload */
        if (UartRecvTO(&locality, 1, FWTPM_FRAME_TIMEOUT_MS) != 0) {
            StoreU32BE(rspHdr, 0);
            UartSend(rspHdr, 4);
            UartSendAck();
            continue;
        }
        if (UartRecvTO(hdr, 4, FWTPM_FRAME_TIMEOUT_MS) != 0) {
            StoreU32BE(rspHdr, 0);
            UartSend(rspHdr, 4);
            UartSendAck();
            continue;
        }
        cmdSize = LoadU32BE(hdr);
        if (cmdSize == 0 || cmdSize > FWTPM_MAX_COMMAND_SIZE) {
            if (cmdSize > 0U) {
                UartDrain(cmdSize);
            }
            StoreU32BE(rspHdr, 0);
            UartSend(rspHdr, 4);
            UartSendAck();
            continue;
        }
        if (UartRecvTO(ctx->cmdBuf, cmdSize, FWTPM_FRAME_TIMEOUT_MS) != 0) {
            StoreU32BE(rspHdr, 0);
            UartSend(rspHdr, 4);
            UartSendAck();
            continue;
        }

        rspSize = FWTPM_MAX_COMMAND_SIZE;
        FWTPM_ProcessCommand(ctx, ctx->cmdBuf, (int)cmdSize,
            ctx->rspBuf, &rspSize, (int)locality);

        rspSzOut = (rspSize > 0) ? (uint32_t)rspSize : 0;
        StoreU32BE(rspHdr, rspSzOut);
        UartSend(rspHdr, 4);
        if (rspSzOut != 0) {
            UartSend(ctx->rspBuf, rspSzOut);
        }
        UartSendAck();
    }
}

int main(void)
{
    FWTPM_CTX* ctx = &g_ctx;
    FWTPM_NV_HAL nvHal;
    int pufEnrolled = 0;
    int rc;

    zynq_uart_init(ZYNQ_CONSOLE_UART_BASE);

    printf("\r\n========================================================\r\n");
    printf("  wolfTPM fwTPM on AMD Zynq-7000 (ZC702)\r\n");
    printf("  Cortex-A9 bare-metal, served over UART\r\n");
    printf("========================================================\r\n");

#ifdef FWTPM_PUF_SELFTEST
    /* Synthetic SRAM PUF regression (proves the fuzzy-extractor math on this
     * silicon; does not touch the physical OCM). */
    FwTPM_Puf_SelfTest();
#endif

    memset(ctx, 0, sizeof(*ctx));
    memset(&nvHal, 0, sizeof(nvHal));

#ifdef FWTPM_NV_QSPI
    rc = FWTPM_NV_QSPI_Init(&nvHal);
#else
    rc = zynq_nv_ram_init(&nvHal);
#endif

    /* Derive the device-unique NV integrity key from the OCM SRAM PUF and wire
     * it into the NV HAL before it is registered. If the PUF is unavailable the
     * fwTPM still runs, but without a silicon-bound NV integrity key. */
    if (rc == 0) {
        int prc = FwTPM_Puf_Init(&pufEnrolled);
        if (prc == 0) {
            nvHal.get_integrity_key = FwTPM_Puf_GetIntegrityKey;
            FwTPM_Puf_PrintInfo(pufEnrolled);
        }
        else {
            printf("SRAM PUF init failed: %d "
                   "(NV integrity uses no device key)\r\n", prc);
        }
    }

    if (rc == 0) {
        rc = FWTPM_NV_SetHAL(ctx, &nvHal);
    }
    if (rc == 0) {
        rc = FWTPM_Clock_ZYNQ_Init(ctx);
    }
    if (rc == 0) {
        rc = FWTPM_Init(ctx);
    }
    if (rc != 0) {
        printf("fwTPM init failed: %d\r\n", rc);
        for (;;) { }
    }
    printf("fwTPM %s initialized (CTX %u bytes)\r\n",
        FWTPM_GetVersionString(), (unsigned int)sizeof(FWTPM_CTX));

    FwTPM_SelfTest(ctx);

    printf("Serving TPM2 over UART (swtpm/mssim). No more console output.\r\n");
    FwTPM_UartCommandLoop(ctx);

    FWTPM_Cleanup(ctx);
    for (;;) { }
    return 0;
}
