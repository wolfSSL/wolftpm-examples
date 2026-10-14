/* act_sign.h
 *
 * The sign-and-verify act: the TPM signs with a Hash-ML-DSA key and the MCU
 * verifies the result itself.
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
#ifndef ACT_SIGN_H_INCLUDED
#define ACT_SIGN_H_INCLUDED

#include <wolftpm/tpm2_wrap.h>

/* Run one signing act, emitting the demo event grammar. With tamper
 * non-zero a single bit of the signature is flipped before verification, so
 * the correct outcome is a rejection. paramSet is a TPM_MLDSA_* parameter
 * set. Returns 0 when the outcome matched what was asked for. */
int act_sign(WOLFTPM2_DEV* dev, int tamper, int paramSet);

#endif /* ACT_SIGN_H_INCLUDED */
