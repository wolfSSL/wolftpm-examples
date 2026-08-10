/* fwtpm_clock_miv.c
 *
 * fwTPM clock HAL for the Mi-V RV32 soft core: milliseconds since boot from
 * CoreTimer0 (this MIV_RV32 has no usable MTIME - see common/miv_time.c).
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

#ifdef WOLFTPM_FWTPM

#include <wolftpm/fwtpm/fwtpm.h>
#include "miv_time.h"

static UINT64 MivClockGetMs(void* ctx)
{
    (void)ctx;
    return (UINT64)miv_millis();
}

int FWTPM_Clock_MIV_Init(FWTPM_CTX* ctx)
{
    if (ctx == NULL) {
        return -1;
    }
    return FWTPM_Clock_SetHAL(ctx, MivClockGetMs, NULL);
}

#endif /* WOLFTPM_FWTPM */
