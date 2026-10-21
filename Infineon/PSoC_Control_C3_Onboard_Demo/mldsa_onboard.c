/* mldsa_onboard.c
 *
 * Runs the sign-and-verify act once and stops: the smallest complete
 * demonstration that a TPM-made post-quantum signature can be verified by
 * the MCU beside it, with no host taking part in the cryptography.
 *
 * demo_acts.c is the same act driven by a command byte alongside the others.
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
#include <string.h>

#include <wolftpm/tpm2.h>
#include <wolftpm/tpm2_wrap.h>

#include "demo_util.h"
#include "act_sign.h"

extern int TPM2_IoCb(TPM2_CTX* ctx, INT32 isRead, UINT32 addr, BYTE* buf,
    UINT16 size, void* userCtx);

static WOLFTPM2_DEV dev;

void main(void)
{
    int rc;

    demo_board_init();

    rc = wolfTPM2_Init(&dev, TPM2_IoCb, NULL);
    if (rc != TPM_RC_SUCCESS) {
        demo_fail("wolfTPM2_Init", rc);
        goto done;
    }
    demo_put("{\"event\":\"tpm.startup\",\"ok\":true}\r\n");

    (void)act_sign(&dev, 0, TPM_MLDSA_87);

    wolfTPM2_Cleanup(&dev);
done:
    while (1)
        ;
}
