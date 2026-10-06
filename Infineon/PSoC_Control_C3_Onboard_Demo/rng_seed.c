/* rng_seed.c
 *
 * Seed wolfCrypt's DRBG from the TPM's random number generator.
 *
 * The acts so far never needed host entropy: every key is made inside the
 * TPM. SPDM is the first thing on this board that generates a key locally,
 * and wolfSPDM_Init fails with WOLFSPDM_E_CRYPTO_FAIL without a seed source.
 * The TPM on the other end of the bus is a certified RNG and is already
 * initialized by the time anything asks, so it is the obvious source.
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
#include <wolftpm/tpm2_wrap.h>

/* Set once the device is up. Seeding before that would re-enter the stack
 * that is still bringing the TPM into service. */
static WOLFTPM2_DEV* seed_dev = NULL;

void app_rand_set_dev(WOLFTPM2_DEV* dev)
{
    seed_dev = dev;
}

/* The TPM caps a single GetRandom at its digest size, so a larger request is
 * filled across several calls rather than silently short. */
int app_rand_seed(unsigned char* output, unsigned int sz)
{
    const word32 chunk = 32;
    word32 got = 0;
    int rc;

    if (output == NULL)
        return -1;
    if (seed_dev == NULL)
        return -1;

    while (got < (word32)sz) {
        word32 want = (word32)sz - got;
        if (want > chunk)
            want = chunk;
        rc = wolfTPM2_GetRandom(seed_dev, output + got, want);
        if (rc != TPM_RC_SUCCESS)
            return -1;
        got += want;
    }
    return 0;
}
