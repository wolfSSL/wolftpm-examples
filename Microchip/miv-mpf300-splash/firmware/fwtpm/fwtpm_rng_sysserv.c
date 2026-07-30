/* fwtpm_rng_sysserv.c
 *
 * wolfCrypt entropy seed hook for the Mi-V fwTPM. Seed material comes from the
 * PolarFire System Controller NRBG via the CoreSysServices_PF nonce service
 * (see miv_sysserv.c). It is wired to wolfCrypt through CUSTOM_RAND_GENERATE_SEED
 * (user_settings.h), so wolfCrypt's Hash-DRBG - not the raw service - produces
 * the TPM's random stream and reseeds from the NRBG on its own schedule.
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
#include <wolfssl/wolfcrypt/error-crypt.h>   /* RNG_FAILURE_E, BAD_FUNC_ARG */
#ifdef NO_INLINE
    #include <wolfssl/wolfcrypt/misc.h>   /* ForceZero */
#else
    #define WOLFSSL_MISC_INCLUDED
    #include <wolfcrypt/src/misc.c>       /* ForceZero (inline build) */
#endif

#include "miv_sysserv.h"

/* wolfCrypt seed hook (CUSTOM_RAND_GENERATE_SEED). Fills sz bytes of seed
 * material from the NRBG, 32 bytes per nonce service call. Returns 0 on success
 * or a wolfCrypt error code (the raw driver code is mapped so it does not
 * collide with the wolfCrypt error enum consumed by wc_InitRng). */
int miv_get_seed(unsigned char* output, unsigned int sz)
{
    unsigned char nonce[32];
    unsigned int n;
    int rc;

    if (output == NULL) {
        return BAD_FUNC_ARG;
    }

    while (sz > 0U) {
        rc = miv_sysserv_nonce(nonce);
        if (rc != 0) {
            ForceZero(nonce, sizeof(nonce));
            return RNG_FAILURE_E;   /* driver code rc mapped into wolfCrypt space */
        }
        n = (sz < sizeof(nonce)) ? sz : (unsigned int)sizeof(nonce);
        memcpy(output, nonce, n);
        output += n;
        sz -= n;
    }

    ForceZero(nonce, sizeof(nonce));
    return 0;
}
