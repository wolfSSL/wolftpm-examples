/* miv_sysserv.c
 *
 * PolarFire System Controller service driver for the Mi-V fabric, over the
 * CoreSysServices_PF APB mailbox. Provides the generic mailbox executor plus
 * the nonce (TRNG seed) and sNVM (persistent NV) services used by the fwTPM.
 *
 * Register map and service framing are from the CoreSysServices_PF handbook;
 * this is an independent implementation, not Microchip's driver sources.
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

#include <stdint.h>
#include <string.h>

#include <wolfssl/wolfcrypt/settings.h>
#ifdef NO_INLINE
    #include <wolfssl/wolfcrypt/misc.h>   /* ForceZero */
#else
    #define WOLFSSL_MISC_INCLUDED
    #include <wolfcrypt/src/misc.c>       /* ForceZero (inline build) */
#endif

#include "miv_board.h"
#include "miv_sysserv.h"

/* CoreSysServices_PF APB register offsets (from the core handbook). */
#define SS_REG_CMD          0x04U   /* command: [6:0] opcode, [15:7] mb_offset */
#define SS_REG_STAT         0x08U   /* service status returned by controller   */
#define SS_REG_REQ          0x0CU   /* [0] REQ start bit; other bits are status*/
#define SS_REG_MBX_WCNT     0x14U   /* number of 32-bit words to write         */
#define SS_REG_MBX_RCNT     0x18U   /* number of 32-bit words to read          */
#define SS_REG_MBX_WADDR    0x1CU   /* write offset within the mailbox         */
#define SS_REG_MBX_RADDR    0x20U   /* read offset within the mailbox          */
#define SS_REG_MBX_WDATA    0x28U   /* mailbox write-data FIFO                 */
#define SS_REG_MBX_RDATA    0x2CU   /* mailbox read-data FIFO                  */
#define SS_REG_USER         0x30U   /* [0] USER_BUSY, [1] USER_RDVLD           */

#define SS_REQ_START        0x01U
#define SS_USER_BUSY        0x01U
#define SS_USER_RDVLD       0x02U

/* Service opcodes. */
#define SS_CMD_NONCE        0x21U
#define SS_CMD_SNVM_WRITE   0x10U   /* non-authenticated plaintext write   */
#define SS_CMD_SNVM_WRITE_C 0x12U   /* authenticated-ciphertext write      */
#define SS_CMD_SNVM_READ    0x18U

#define SS_NONCE_LEN        32U

/* Bounded spin so a missing/holding controller can never hang the TPM. */
#define SS_SPIN_TIMEOUT     5000000U

#define SS_REG(off) \
    (*(volatile uint32_t*)(uintptr_t)(MIV_CORESYSSERV_BASE + (off)))

int miv_sysserv_exec(uint8_t opcode,
                     const uint8_t* wr, uint16_t wrSz,
                     uint8_t* rd, uint16_t rdSz,
                     uint16_t mbOffset, uint16_t rdWordOffset)
{
    const uint32_t* wrWords = (const uint32_t*)(const void*)wr;
    uint32_t* rdWords = (uint32_t*)(void*)rd;
    uint32_t spin;
    uint32_t idx;
    uint32_t status;

    /* Buffers are accessed as 32-bit words: require 4-byte alignment and
     * word-multiple sizes (and non-NULL when used), else reject rather than
     * fault on this rv32imc core or silently truncate. */
    if ((wrSz > 0U) &&
        ((wr == NULL) || (((uintptr_t)wr & 3U) != 0U) || ((wrSz & 3U) != 0U))) {
        return -4;
    }
    if ((rdSz > 0U) &&
        ((rd == NULL) || (((uintptr_t)rd & 3U) != 0U) || ((rdSz & 3U) != 0U))) {
        return -4;
    }

    /* Wait for any in-flight service to finish. */
    spin = SS_SPIN_TIMEOUT;
    while ((SS_REG(SS_REG_USER) & SS_USER_BUSY) != 0U) {
        if (--spin == 0U) {
            return -1;
        }
    }

    /* Opcode in [6:0], mailbox offset in [15:7]. */
    SS_REG(SS_REG_CMD) = (uint32_t)(((uint32_t)mbOffset << 7) |
                                    (opcode & 0x7FU));

    if (wrSz > 0U) {
        SS_REG(SS_REG_MBX_WCNT)  = (uint32_t)(wrSz / 4U);
        SS_REG(SS_REG_MBX_WADDR) = (uint32_t)mbOffset;
    }
    if (rdSz > 0U) {
        SS_REG(SS_REG_MBX_RCNT)  = (uint32_t)(rdSz / 4U);
        SS_REG(SS_REG_MBX_RADDR) = (uint32_t)(rdWordOffset + mbOffset);
    }

    /* Start the service. */
    SS_REG(SS_REG_REQ) = SS_REQ_START;

    /* Push input words to the mailbox (after REQ, matching the controller
     * handshake). */
    if (wrSz > 0U) {
        for (idx = 0U; idx < (uint32_t)(wrSz / 4U); idx++) {
            SS_REG(SS_REG_MBX_WDATA) = wrWords[idx];
        }
    }

    /* Drain the response mailbox, word by word, gated by USER_RDVLD. */
    if (rdSz > 0U) {
        for (idx = 0U; idx < (uint32_t)(rdSz / 4U); idx++) {
            spin = SS_SPIN_TIMEOUT;
            while ((SS_REG(SS_REG_USER) & SS_USER_RDVLD) == 0U) {
                if (--spin == 0U) {
                    return -2;
                }
            }
            rdWords[idx] = SS_REG(SS_REG_MBX_RDATA);
        }
    }

    /* Wait for completion, then read the controller status. */
    spin = SS_SPIN_TIMEOUT;
    while ((SS_REG(SS_REG_USER) & SS_USER_BUSY) != 0U) {
        if (--spin == 0U) {
            return -3;
        }
    }

    status = SS_REG(SS_REG_STAT) & 0xFFU;
    if (status != 0U) {
        return -(int)(100U + status);
    }
    return 0;
}

int miv_sysserv_nonce(unsigned char out[32])
{
    uint32_t words[SS_NONCE_LEN / 4U];
    int rc;

    rc = miv_sysserv_exec(SS_CMD_NONCE, NULL, 0U,
                          (uint8_t*)words, SS_NONCE_LEN, 0U, 0U);
    if (rc == 0) {
        memcpy(out, words, SS_NONCE_LEN);
    }
    /* words holds raw entropy; scrub with ForceZero so the store is not elided. */
    ForceZero(words, sizeof(words));
    return rc;
}

int miv_snvm_write_page(uint8_t page, const uint8_t* data)
{
    /* Frame: [0]=SNVMADDR, [1..3]=reserved, [4..255]=252 data bytes. */
    uint32_t frame[256U / 4U];

    if (data == NULL || page >= MIV_SNVM_PAGE_COUNT) {
        return -1;
    }
    memset(frame, 0, sizeof(frame));
    ((uint8_t*)frame)[0] = page;
    memcpy((uint8_t*)frame + 4, data, MIV_SNVM_PAGE_DATA);

    return miv_sysserv_exec(SS_CMD_SNVM_WRITE, (const uint8_t*)frame, 256U,
                            NULL, 0U, 0U, 0U);
}

int miv_snvm_read_page(uint8_t page, uint8_t* data)
{
    /* Request frame (16 B = 4 words): [0]=SNVMADDR, [1..3]=reserved,
     * [4..15]=user key (0 in plaintext mode). The controller writes its response
     * right after the request, so rdWordOffset=4 starts the read at the response
     * ([0..3]=admin header, [4..255]=252 data), hence the +4 on the copy below.
     * The two offsets skip different things: the request frame, then the admin
     * header - not the same header twice. */
    uint32_t frame[16U / 4U];
    uint32_t resp[256U / 4U];
    int rc;

    if (data == NULL || page >= MIV_SNVM_PAGE_COUNT) {
        return -1;
    }
    memset(frame, 0, sizeof(frame));
    ((uint8_t*)frame)[0] = page;

    rc = miv_sysserv_exec(SS_CMD_SNVM_READ, (const uint8_t*)frame, 16U,
                          (uint8_t*)resp, 256U, 0U, 4U);
    if (rc == 0) {
        memcpy(data, (uint8_t*)resp + 4, MIV_SNVM_PAGE_DATA);
    }
    return rc;
}

int miv_snvm_write_page_auth(uint8_t page, const uint8_t* data,
                             const uint8_t* usk)
{
    /* Frame (252): [0]=SNVMADDR, [1..3]=reserved, [4..239]=236 data bytes,
     * [240..251]=12-byte user secret key. */
    uint32_t frame[252U / 4U];

    if (data == NULL || usk == NULL || page >= MIV_SNVM_PAGE_COUNT) {
        return -1;
    }
    memset(frame, 0, sizeof(frame));
    ((uint8_t*)frame)[0] = page;
    memcpy((uint8_t*)frame + 4, data, MIV_SNVM_PAGE_DATA_AUTH);
    memcpy((uint8_t*)frame + 4 + MIV_SNVM_PAGE_DATA_AUTH, usk, MIV_SNVM_USK_LEN);

    return miv_sysserv_exec(SS_CMD_SNVM_WRITE_C, (const uint8_t*)frame, 252U,
                            NULL, 0U, 0U, 0U);
}

int miv_snvm_read_page_auth(uint8_t page, uint8_t* data, const uint8_t* usk)
{
    /* Request frame (16): [0]=SNVMADDR, [1..3]=reserved, [4..15]=user secret
     * key. Response (240): [0..3]=admin header, [4..239]=236 data bytes. */
    uint32_t frame[16U / 4U];
    uint32_t resp[240U / 4U];
    int rc;

    if (data == NULL || usk == NULL || page >= MIV_SNVM_PAGE_COUNT) {
        return -1;
    }
    memset(frame, 0, sizeof(frame));
    ((uint8_t*)frame)[0] = page;
    memcpy((uint8_t*)frame + 4, usk, MIV_SNVM_USK_LEN);

    rc = miv_sysserv_exec(SS_CMD_SNVM_READ, (const uint8_t*)frame, 16U,
                          (uint8_t*)resp, 240U, 0U, 4U);
    if (rc == 0) {
        memcpy(data, (uint8_t*)resp + 4, MIV_SNVM_PAGE_DATA_AUTH);
    }
    return rc;
}
