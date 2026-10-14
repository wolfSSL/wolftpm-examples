/* act_tls_sign.c
 *
 * Sign a host-supplied digest with an ML-DSA key held in the fitted TPM, so a
 * TLS 1.3 handshake running on the PC can be authenticated by this board.
 *
 * The host sends the CertificateVerify digest and gets a signature back; the
 * TLS stack, the certificate and the key exchange all stay on the PC. The only
 * thing delegated here is the one operation that needs the private key.
 *
 * The part signs Hash-ML-DSA with a SHA-256 pre-hash and nothing else. Asking
 * for SHA-384 or SHA-512 creates a key that then refuses to sign with
 * TPM_RC_SIZE on the digest, and SHA3 and SHAKE are refused outright at create
 * with TPM_RC_HASH. Measured on firmware 45.91, so the 32-byte digest below is
 * the only size worth accepting.
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

#include "demo_util.h"
#include "act_tls_sign.h"

/* TLS 1.3 pins the digest to the signature algorithm, and the code point this
 * demo uses names SHA-256. */
#define TLS_SIGN_DIGEST_SZ  32
#define TLS_SIGN_HEX_SZ     (TLS_SIGN_DIGEST_SZ * 2)

/* ML-DSA-87, matching the parameter set the signing act leads with. */
#define TLS_SIGN_PARAM_SET  TPM_MLDSA_87

/* A whole request should already be in flight when the command byte arrives,
 * so this only has to outlast scheduling noise on the host. */
#define TLS_SIGN_BYTE_MS    2000

/* Derived once and kept. It has to exist before the public key can be
 * reported, and a primary under the owner hierarchy is deterministic, so
 * holding the handle costs nothing and keeps the derivation out of the
 * handshake. */
static WOLFTPM2_KEY tlsKey;
static int tlsKeyReady;

/* Drop the cached primary. A handle lost to a TPM reset is already gone and
 * the unload simply fails, but a transport or command error leaves it loaded,
 * and re-deriving over the top of it would fill the transient slots. */
static void tls_key_discard(WOLFTPM2_DEV* dev)
{
    if (tlsKeyReady)
        (void)wolfTPM2_UnloadHandle(dev, &tlsKey.handle);
    tlsKeyReady = 0;
    memset(&tlsKey, 0, sizeof(tlsKey));
}

static int tls_key_load(WOLFTPM2_DEV* dev)
{
    TPMT_PUBLIC tpl;
    int rc;

    if (tlsKeyReady)
        return TPM_RC_SUCCESS;

    memset(&tlsKey, 0, sizeof(tlsKey));
    memset(&tpl, 0, sizeof(tpl));

    rc = wolfTPM2_GetKeyTemplate_HASH_MLDSA(&tpl,
            TPMA_OBJECT_sign | TPMA_OBJECT_fixedTPM |
            TPMA_OBJECT_fixedParent | TPMA_OBJECT_sensitiveDataOrigin |
            TPMA_OBJECT_userWithAuth | TPMA_OBJECT_noDA,
            (TPMI_MLDSA_PARAMETER_SET)TLS_SIGN_PARAM_SET, TPM_ALG_SHA256);
    if (rc != 0) {
        demo_fail("GetKeyTemplate_HASH_MLDSA", rc);
        return rc;
    }

    rc = wolfTPM2_CreatePrimaryKey(dev, &tlsKey, TPM_RH_OWNER, &tpl, NULL, 0);
    if (rc != TPM_RC_SUCCESS) {
        demo_fail("CreatePrimaryKey", rc);
        return rc;
    }

    tlsKeyReady = 1;
    return TPM_RC_SUCCESS;
}

static void tls_sign_end(int ok, const char* why)
{
    demo_put("{\"event\":\"boardsign.end\",\"ok\":");
    demo_put(ok ? "true" : "false");
    if (why != NULL) {
        demo_put(",\"why\":\"");
        demo_put(why);
        demo_put("\"");
    }
    demo_put("}\r\n");
}

static int hex_nibble(uint8_t c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

/* Read to the end of the request line. Without this a malformed digest
 * leaves its remaining characters in the FIFO, and the dispatcher reads the
 * likes of 'd', 'e' or 's' as act commands. Bounded, because a request is
 * one line and a stalled link must not hold the act here. */
static void tls_sign_drain(void)
{
    uint8_t c;
    int n;

    for (n = 0; n < TLS_SIGN_HEX_SZ + 2; n++) {
        if (demo_getc_timed(&c, TLS_SIGN_BYTE_MS) != 0)
            return;
        if (c == '\n' || c == '\r')
            return;
    }
}

void act_tls_pub(WOLFTPM2_DEV* dev)
{
    if (tls_key_load(dev) != TPM_RC_SUCCESS) {
        tls_sign_end(0, "key");
        return;
    }

    demo_put("{\"event\":\"boardsign.pub\",\"alg\":\"HASH_MLDSA\",\"mldsa\":");
    /* The parameter set as 65 or 87, matching every other act. The TPM enum
     * is not that number and reporting it raw would read as a third set. */
    demo_put_u32((uint32_t)(TLS_SIGN_PARAM_SET == TPM_MLDSA_65 ? 65 : 87));
    demo_put(",\"prehash\":\"SHA256\",\"bytes\":");
    demo_put_u32(tlsKey.pub.publicArea.unique.mldsa.size);
    demo_put(",\"hex\":\"");
    demo_put_hexbuf(tlsKey.pub.publicArea.unique.mldsa.buffer,
            tlsKey.pub.publicArea.unique.mldsa.size);
    demo_put("\"}\r\n");
    tls_sign_end(1, NULL);
}

void act_tls_sign(WOLFTPM2_DEV* dev)
{
    byte digest[TLS_SIGN_DIGEST_SZ];
    static byte sig[5000];
    int sigSz = (int)sizeof(sig);
    int i, hi, lo, rc;
    uint8_t c;

    /* Read the digest before anything else, so a malformed request is drained
     * rather than left for the dispatcher to read as command bytes. */
    for (i = 0; i < TLS_SIGN_HEX_SZ; i++) {
        if (demo_getc_timed(&c, TLS_SIGN_BYTE_MS) != 0) {
            tls_sign_end(0, "timeout");
            return;
        }
        if ((i & 1) == 0) {
            hi = hex_nibble(c);
            if (hi < 0) {
                tls_sign_drain();
                tls_sign_end(0, "hex");
                return;
            }
        }
        else {
            lo = hex_nibble(c);
            if (lo < 0) {
                tls_sign_drain();
                tls_sign_end(0, "hex");
                return;
            }
            digest[i / 2] = (byte)((hi << 4) | lo);
        }
    }
    /* Trailing newline, so the host and the board agree where the request
     * ends even if one of them is restarted mid-stream. */
    if (demo_getc_timed(&c, TLS_SIGN_BYTE_MS) != 0 ||
            (c != '\n' && c != '\r')) {
        tls_sign_drain();
        tls_sign_end(0, "framing");
        return;
    }

    if (tls_key_load(dev) != TPM_RC_SUCCESS) {
        tls_sign_end(0, "key");
        return;
    }

    rc = wolfTPM2_SignDigest(dev, &tlsKey, digest, TLS_SIGN_DIGEST_SZ,
            NULL, 0, sig, &sigSz);
    if (rc != TPM_RC_SUCCESS) {
        demo_fail("SignDigest", rc);
        tls_key_discard(dev);
        tls_sign_end(0, "sign");
        return;
    }

    demo_put("{\"event\":\"boardsign.sig\",\"bytes\":");
    demo_put_u32((uint32_t)sigSz);
    demo_put(",\"hex\":\"");
    demo_put_hexbuf(sig, (uint32_t)sigSz);
    demo_put("\"}\r\n");
    tls_sign_end(1, NULL);
}
