/* fwtpm_clock_mpfs.c
 *
 * Clock HAL for fwTPM on PolarFire SoC. Uses CLINT mtime via
 * mpfs_rdtime() (rdtime CSR is illegal in M-mode on this platform).
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

#include "user_settings.h"

#ifdef WOLFTPM_FWTPM

#include <wolftpm/fwtpm/fwtpm.h>
#include "mpfs_hal.h"

/* Return type and the (void*) signature must match FWTPM_CLOCK_HAL_S.get_ms
 * (UINT64 (*)(void*)) exactly. Returning UINT64 (not uint32_t) avoids the
 * incompatible-pointer UB and the ~49.7-day millisecond wrap. */
static UINT64 MpfsClockGetMs(void *ctx)
{
    (void)ctx;
    return (UINT64)(mpfs_rdtime() / (MTIME_FREQ / 1000));
}

int FWTPM_Clock_MPFS_Init(FWTPM_CTX *ctx)
{
    if (ctx == NULL)
        return -1;
    return FWTPM_Clock_SetHAL(ctx, MpfsClockGetMs, NULL);
}

#endif /* WOLFTPM_FWTPM */
