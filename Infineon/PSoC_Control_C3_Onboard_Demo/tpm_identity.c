/* tpm_identity.c
 *
 * Read the TPM's identity from the PSOC Control C3 itself, with no host
 * involved. This is the first on-board milestone: it proves wolfTPM runs on
 * the MCU and reaches the part over I2C.
 *
 * It deliberately reports the TIS layer separately from TPM2_Startup. A part
 * that answers its registers but refuses commands is a very different problem
 * from a bus that does not work, and from the demo's point of view the two
 * otherwise look identical.
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
#include <stdint.h>
#include <string.h>

#include <wolftpm/tpm2.h>
#include <wolftpm/tpm2_wrap.h>

#include "hal/psoc_c3.h"

/* Where the TPM's interface-select strap lands on the evaluation kit. */
#ifndef PSOC_C3_TPM_SEL_PORT
#define PSOC_C3_TPM_SEL_PORT    7
#endif
#ifndef PSOC_C3_TPM_SEL_PIN
#define PSOC_C3_TPM_SEL_PIN     7
#endif

/* Supplied by tpm_io_i2c_c3.c in place of wolfTPM's built-in HAL. */
extern int TPM2_IoCb(TPM2_CTX* ctx, INT32 isRead, UINT32 addr, BYTE* buf,
    UINT16 size, void* userCtx);

extern void uart_init(void);
extern void uart_write(const char *buf, unsigned int sz);

static TPM2_CTX ctx;
static WOLFTPM2_DEV dev;
static WOLFTPM2_CAPS caps;

static void put(const char *s)
{
    unsigned int n = 0;
    while (s[n] != 0)
        n++;
    uart_write(s, n);
}

static void put_u32(uint32_t v)
{
    char tmp[12];
    char out[12];
    int t = 0, n = 0;

    if (v == 0) {
        out[n++] = '0';
    }
    else {
        while (v > 0) {
            tmp[t++] = (char)('0' + (v % 10));
            v /= 10;
        }
        while (t > 0)
            out[n++] = tmp[--t];
    }
    uart_write(out, (unsigned int)n);
}

static void put_hex32(uint32_t v)
{
    static const char d[] = "0123456789abcdef";
    char out[8];
    int i;

    for (i = 7; i >= 0; i--) {
        out[i] = d[v & 0xF];
        v >>= 4;
    }
    uart_write(out, 8);
}

/* The TPM latches its bus from this strap while its own reset is low, and
 * its reset trails the CPU's by about a millisecond. Driving the pin is
 * therefore the first thing main() does; when wolfBoot is in front of this
 * application its hal_init() has already done it, but a standalone image has
 * to do it itself and only just gets there in time. */
static void tpm_iface_select(void)
{
    psoc_c3_peri_init();
    psoc_c3_pin_setup(PSOC_C3_TPM_SEL_PORT, PSOC_C3_TPM_SEL_PIN, 0,
            GPIO_CFG_DM_STRONG);
    GPIO_PRT_OUT(PSOC_C3_TPM_SEL_PORT) &= ~(1UL << PSOC_C3_TPM_SEL_PIN);
}

void main(void)
{
    int rc;

    tpm_iface_select();
    uart_init();
    put("\r\nTPM identity, read on the PSOC Control C3\r\n");

    /* Bring up the TIS layer without issuing a startup, so a part in failure
     * mode can still be told apart from a dead bus. */
    rc = TPM2_Init_ex(&ctx, TPM2_IoCb, NULL, 0);
    put("  TPM2_Init_ex (no selftest): 0x"); put_hex32((uint32_t)rc);
    put(rc == TPM_RC_SUCCESS ? "  bus and TIS OK\r\n" : "  bus or TIS FAILED\r\n");

    memset(&dev, 0, sizeof(dev));
    rc = wolfTPM2_Init(&dev, TPM2_IoCb, NULL);
    put("  wolfTPM2_Init            : 0x"); put_hex32((uint32_t)rc);
    put(rc == TPM_RC_SUCCESS ? "  started\r\n"
                             : "  startup refused by the part\r\n");

    if (rc == TPM_RC_SUCCESS) {
        memset(&caps, 0, sizeof(caps));
        rc = wolfTPM2_GetCapabilities(&dev, &caps);
        put("  wolfTPM2_GetCapabilities : 0x"); put_hex32((uint32_t)rc);
        put("\r\n");
        if (rc == TPM_RC_SUCCESS) {
            put("  Mfg "); put(caps.mfgStr);
            put(" Vendor "); put(caps.vendorStr);
            put(" Fw "); put_u32(caps.fwVerMajor);
            put("."); put_u32(caps.fwVerMinor);
            put("\r\n");
        }
        wolfTPM2_Cleanup(&dev);
    }

    put("done\r\n");
    while (1)
        ;
}
