/* main.c
 *
 * wolfTPM firmware TPM (fwTPM) on a MicroBlaze V (RISC-V rv32imc) soft core
 * (AMD Spartan UltraScale+ SCU35). Registers the NV / clock HALs, initializes
 * the fwTPM engine, runs a small standalone self-test (TPM2_Startup ->
 * TPM2_GetRandom), then serves TPM2 commands over the AXI UARTLite console using
 * the same framing as the STM32H5, Mi-V and Zynq-7000 ports (raw swtpm +
 * Microsoft-simulator "mssim"), so the wolfTPM swtpm client can drive it.
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

#include "scu35_board.h"
#include "mbv_uart.h"
#include "mbv_time.h"

/* Port HAL initializers (this example). */
extern int mbv_nv_ram_init(FWTPM_NV_HAL* hal);
extern int FWTPM_Clock_MBV_Init(FWTPM_CTX* ctx);

/* Static fwTPM context (large - keep off the stack). */
static FWTPM_CTX g_ctx;

/* mssim platform command codes (Microsoft TPM simulator protocol). */
#define MSSIM_SIGNAL_POWER_ON   1
#define MSSIM_SIGNAL_POWER_OFF  2
#define MSSIM_SEND_COMMAND      8
#define MSSIM_SIGNAL_RESET      17
#define MSSIM_SESSION_END       20
#define MSSIM_STOP              21

#define FWTPM_FRAME_TIMEOUT_MS   2000U

static const uint8_t g_tpmRcFailure[10] = {
    0x80, 0x01, 0x00, 0x00, 0x00, 0x0A, 0x00, 0x00, 0x01, 0x01
};

/* ---- raw UART byte transport over AXI UARTLite ---- */
static int UartRecv(uint8_t* buf, uint32_t sz)
{
    uint32_t i;
    for (i = 0; i < sz; i++) {
        while (mbv_uart_getc(SCU35_CONSOLE_UART_BASE, &buf[i]) == 0) {
            /* Service the software tick accumulator while blocking so idle time
             * is not dropped from the TPM clock (32-bit counter wraps ~19 s). */
            (void)mbv_ticks();
        }
    }
    return 0;
}

static int UartRecvTO(uint8_t* buf, uint32_t sz, uint32_t timeoutMs)
{
    uint32_t i;
    /* Frame-level deadline: timeoutMs bounds the whole frame, not each byte, so
     * a large declared length cannot stretch the wait to sz * timeoutMs. */
    uint64_t deadline = mbv_millis() + timeoutMs;

    for (i = 0; i < sz; i++) {
        while (mbv_uart_getc(SCU35_CONSOLE_UART_BASE, &buf[i]) == 0) {
            (void)mbv_ticks();
            if (mbv_millis() >= deadline) {
                return -1;
            }
        }
    }
    return 0;
}

static int UartSend(const uint8_t* buf, uint32_t sz)
{
    mbv_uart_write(SCU35_CONSOLE_UART_BASE, buf, sz);
    return 0;
}

static void UartDrain(uint32_t maxBytes)
{
    uint8_t b;
    uint32_t got = 0U;
    uint64_t idleStart = mbv_millis();

    while (got < maxBytes) {
        if (mbv_uart_getc(SCU35_CONSOLE_UART_BASE, &b) != 0) {
            got++;
            idleStart = mbv_millis();
        }
        else if ((mbv_millis() - idleStart) > 10U) {
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

/* ---- standalone self-test ---- */
static void FwTPM_SelfTest(FWTPM_CTX* ctx)
{
    static const uint8_t cmdStartup[] = {
        0x80, 0x01, 0x00, 0x00, 0x00, 0x0C, 0x00, 0x00, 0x01, 0x44, 0x00, 0x00
    };
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
    int rc;

    mbv_uart_init(SCU35_CONSOLE_UART_BASE);

    printf("\r\n========================================================\r\n");
    printf("  wolfTPM fwTPM on AMD Spartan UltraScale+ SCU35\r\n");
    printf("  MicroBlaze V (RISC-V rv32imc) soft core, over UART\r\n");
    printf("========================================================\r\n");

    memset(ctx, 0, sizeof(*ctx));
    memset(&nvHal, 0, sizeof(nvHal));

    rc = mbv_nv_ram_init(&nvHal);
    if (rc == 0) {
        rc = FWTPM_NV_SetHAL(ctx, &nvHal);
    }
    if (rc == 0) {
        rc = FWTPM_Clock_MBV_Init(ctx);
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
