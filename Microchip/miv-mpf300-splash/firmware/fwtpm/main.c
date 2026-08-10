/* main.c
 *
 * wolfTPM firmware TPM (fwTPM) on the Mi-V RV32 soft core
 * (PolarFire MPF300 Splash Kit). Registers the NV / clock HALs, initializes
 * the fwTPM engine, runs a small standalone self-test (TPM2_Startup ->
 * TPM2_GetRandom), then serves TPM2 commands over the CoreUARTapb console
 * using the same framing as the STM32H5 port (raw swtpm + Microsoft-simulator
 * "mssim"), so the wolfTPM swtpm client can drive it from a host.
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

#include "miv_board.h"
#include "miv_uart.h"
#include "miv_time.h"
#include "miv_sysserv.h"

/* Port HAL initializers (this example) */
extern int FWTPM_NV_SNVM_Init(FWTPM_NV_HAL* hal);
extern int FWTPM_Clock_MIV_Init(FWTPM_CTX* ctx);
extern int miv_sysserv_nonce(unsigned char out[32]);

/* Optional sNVM persistence self-test (build -DMIV_SNVM_BOOTCNT_TEST, off by
 * default to avoid writing limited-endurance sNVM every boot). A boot counter
 * in a page outside the TPM NV region increments and persists across reloads
 * and power cycles, exercising the sNVM path independently of the TPM state. */
#ifdef MIV_SNVM_BOOTCNT_TEST
#define MIV_SNVM_BOOTCNT_PAGE   220U
/* A never-written plaintext sNVM page reads back as status 2 (rc -102); any
 * other negative rc is a genuine fault, not a blank page. */
#define MIV_SNVM_RC_BLANK       (-102)

static void FwTPM_SnvmBootCounter(void)
{
    uint8_t page[MIV_SNVM_PAGE_DATA];
    uint32_t cnt;
    int rc;

    memset(page, 0xFF, sizeof(page));  /* clean page for the blank-read case */
    rc = miv_snvm_read_page(MIV_SNVM_BOOTCNT_PAGE, page);
    if (rc == MIV_SNVM_RC_BLANK) {
        /* Never-written page: treat that (and only that) as the first boot. */
        cnt = 0;
        printf("sNVM persistence: boot counter = 0 (page blank, first boot)\r\n");
    }
    else if (rc != 0) {
        /* A genuine read fault: do NOT overwrite - resetting a good counter to a
         * fresh value would destroy state. Report and leave the page untouched. */
        printf("sNVM persistence: read FAILED rc=%d (leaving counter "
               "untouched)\r\n", rc);
        return;
    }
    else {
        cnt = (uint32_t)page[0] | ((uint32_t)page[1] << 8) |
              ((uint32_t)page[2] << 16) | ((uint32_t)page[3] << 24);
        if (cnt == 0xFFFFFFFFU) {
            cnt = 0;  /* erased page = first boot */
        }
        printf("sNVM persistence: boot counter = %u\r\n", (unsigned)cnt);
    }

    cnt++;
    page[0] = (uint8_t)(cnt & 0xFFU);
    page[1] = (uint8_t)((cnt >> 8) & 0xFFU);
    page[2] = (uint8_t)((cnt >> 16) & 0xFFU);
    page[3] = (uint8_t)((cnt >> 24) & 0xFFU);
    rc = miv_snvm_write_page(MIV_SNVM_BOOTCNT_PAGE, page);
    if (rc != 0) {
        printf("sNVM persistence: write FAILED rc=%d\r\n", rc);
    }
    else {
        printf("sNVM persistence: stored boot counter = %u "
               "(survives power cycle)\r\n", (unsigned)cnt);
    }
}
#endif /* MIV_SNVM_BOOTCNT_TEST */

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
#define MIV_FWTPM_FRAME_TIMEOUT_MS   2000U

/* Minimal well-formed TPM_RC_FAILURE response (10-byte header) used to unblock
 * a raw-transport host after a malformed or stalled frame. */
static const uint8_t g_tpmRcFailure[10] = {
    0x80, 0x01, 0x00, 0x00, 0x00, 0x0A, 0x00, 0x00, 0x01, 0x01
};

/* ---- raw UART byte transport over CoreUARTapb ---- */
static int UartRecv(uint8_t* buf, uint32_t sz)
{
    uint32_t i;
    for (i = 0; i < sz; i++) {
        while (miv_uart_getc(MIV_COREUARTAPB0_BASE, &buf[i]) == 0) {
            /* Service the software tick accumulator while blocking: miv_ticks()
             * must be called at least once per CoreTimer wrap (~1.9 h at 625 kHz
             * with the /128 prescaler) or idle time is dropped from the TPM
             * clock. This spin is the only thing running between commands. */
            (void)miv_ticks();
        }
    }
    return 0;
}

/* Bounded receive for the remainder of an in-progress frame: like UartRecv but
 * gives up with -1 if no byte arrives within timeoutMs (inter-byte deadline),
 * so a sender that stops mid-frame cannot wedge the command loop. Used only
 * after a frame has started; the idle wait for a new command still blocks. */
static int UartRecvTO(uint8_t* buf, uint32_t sz, uint32_t timeoutMs)
{
    uint32_t i;
    uint64_t deadline;

    for (i = 0; i < sz; i++) {
        deadline = miv_millis() + timeoutMs;
        while (miv_uart_getc(MIV_COREUARTAPB0_BASE, &buf[i]) == 0) {
            (void)miv_ticks();
            if (miv_millis() >= deadline) {
                return -1;
            }
        }
    }
    return 0;
}

static int UartSend(const uint8_t* buf, uint32_t sz)
{
    miv_uart_write(MIV_COREUARTAPB0_BASE, buf, sz);
    return 0;
}

/* Discard up to maxBytes of pending input, stopping early once the stream is
 * idle for ~10 ms - resyncs the raw transport after a malformed frame without
 * blocking indefinitely on a bogus declared length. */
static void UartDrain(uint32_t maxBytes)
{
    uint8_t b;
    uint32_t got = 0U;
    uint64_t idleStart = miv_millis();

    while (got < maxBytes) {
        if (miv_uart_getc(MIV_COREUARTAPB0_BASE, &b) != 0) {
            got++;
            idleStart = miv_millis();
        }
        else if ((miv_millis() - idleStart) > 10U) {
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
            if (UartRecvTO(ctx->cmdBuf + 4, 6, MIV_FWTPM_FRAME_TIMEOUT_MS) != 0) {
                /* Header stalled mid-frame: unblock a waiting host and reset. */
                UartSend(g_tpmRcFailure, sizeof(g_tpmRcFailure));
                continue;
            }
            cmdSize = LoadU32BE(ctx->cmdBuf + 2);
            if (cmdSize < 10 || cmdSize > FWTPM_MAX_COMMAND_SIZE) {
                /* Drain any announced payload (bounded, idle-terminated) so a
                 * malformed frame cannot leave the raw transport misaligned. */
                if (cmdSize > 10U) {
                    UartDrain(cmdSize - 10U);
                }
                UartSend(g_tpmRcFailure, sizeof(g_tpmRcFailure));
                continue;
            }
            remaining = cmdSize - 10;
            if (remaining > 0) {
                if (UartRecvTO(ctx->cmdBuf + 10, remaining,
                               MIV_FWTPM_FRAME_TIMEOUT_MS) != 0) {
                    /* Payload stalled mid-frame: unblock and reset. */
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
                /* No response produced: emit a well-formed TPM_RC_FAILURE so the
                 * host does not block in read_exact(). */
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
        if (UartRecvTO(&locality, 1, MIV_FWTPM_FRAME_TIMEOUT_MS) != 0) {
            /* Frame stalled: return an empty response so the host does not
             * block waiting on the mssim reply, then reset the parser. */
            StoreU32BE(rspHdr, 0);
            UartSend(rspHdr, 4);
            UartSendAck();
            continue;
        }
        if (UartRecvTO(hdr, 4, MIV_FWTPM_FRAME_TIMEOUT_MS) != 0) {
            StoreU32BE(rspHdr, 0);
            UartSend(rspHdr, 4);
            UartSendAck();
            continue;
        }
        cmdSize = LoadU32BE(hdr);
        if (cmdSize == 0 || cmdSize > FWTPM_MAX_COMMAND_SIZE) {
            /* Drain the announced payload (bounded) before replying, so an
             * oversize frame does not desync the transport. */
            if (cmdSize > 0U) {
                UartDrain(cmdSize);
            }
            StoreU32BE(rspHdr, 0);
            UartSend(rspHdr, 4);
            UartSendAck();
            continue;
        }
        if (UartRecvTO(ctx->cmdBuf, cmdSize, MIV_FWTPM_FRAME_TIMEOUT_MS) != 0) {
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
    unsigned char nonce[32];
    volatile unsigned char* vp;
    int nrc;
    int i;
    int rc;

    miv_uart_init(MIV_COREUARTAPB0_BASE, MIV_SYS_CLK_FREQ, MIV_CONSOLE_BAUD,
        (uint8_t)(MIV_UART_DATA_8_BITS | MIV_UART_NO_PARITY));
    miv_timer_init();

    printf("\r\n========================================================\r\n");
    printf("  wolfTPM fwTPM on PolarFire MPF300 Splash Kit\r\n");
    printf("  Mi-V RV32 soft core\r\n");
    printf("========================================================\r\n");

    memset(ctx, 0, sizeof(*ctx));
    memset(&nvHal, 0, sizeof(nvHal));

    rc = FWTPM_NV_SNVM_Init(&nvHal);
    if (rc == 0) {
        rc = FWTPM_NV_SetHAL(ctx, &nvHal);
    }
    if (rc == 0) {
        rc = FWTPM_Clock_MIV_Init(ctx);
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

    /* Entropy check: pull a raw nonce from the System Controller NRBG
     * (CoreSysServices_PF). This is the seed source behind the Hash-DRBG. */
    nrc = miv_sysserv_nonce(nonce);
    if (nrc == 0) {
        printf("Entropy: System Controller nonce OK: ");
        for (i = 0; i < 8; i++) {
            printf("%02x", nonce[i]);
        }
        printf("...\r\n");
    }
    else {
        printf("Entropy: nonce service FAILED rc=%d "
               "(check CoreSysServices_PF on APB 0x70003000)\r\n", nrc);
    }
    /* Scrub the raw nonce. ForceZero is not linkable in this translation unit
     * (it pulls in wolfTPM inline headers), so use a volatile store loop. */
    vp = nonce;
    for (i = 0; i < (int)sizeof(nonce); i++) {
        vp[i] = 0;
    }

#ifdef MIV_SNVM_BOOTCNT_TEST
    FwTPM_SnvmBootCounter();
#endif

    FwTPM_SelfTest(ctx);

    printf("Serving TPM2 over UART (swtpm/mssim). No more console output.\r\n");
    FwTPM_UartCommandLoop(ctx);

    FWTPM_Cleanup(ctx);
    for (;;) { }
    return 0;
}
