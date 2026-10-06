/* act_spdm.h
 *
 * The SPDM act: the MCU negotiates an encrypted channel to the fitted TPM.
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
#ifndef ACT_SPDM_H_INCLUDED
#define ACT_SPDM_H_INCLUDED

#include <wolftpm/tpm2_wrap.h>

/* Run the SPDM act, emitting the demo event grammar. Returns the number of
 * steps that passed. The device is left re-initialized in plain mode, so the
 * other acts are unaffected by having run this one. */
int act_spdm(WOLFTPM2_DEV* dev);

#endif /* ACT_SPDM_H_INCLUDED */
