/* act_tls_sign.h
 *
 * Sign a host-supplied digest with an ML-DSA key in the fitted TPM, for a TLS
 * 1.3 handshake running on the host. See act_tls_sign.c.
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
#ifndef ACT_TLS_SIGN_H
#define ACT_TLS_SIGN_H

#include <wolftpm/tpm2_wrap.h>

/* Report the public key of the TPM-held TLS identity, deriving it if needed. */
void act_tls_pub(WOLFTPM2_DEV* dev);

/* Read a hex digest and a newline from the console, sign it, return the
 * signature as hex. */
void act_tls_sign(WOLFTPM2_DEV* dev);

#endif /* ACT_TLS_SIGN_H */
