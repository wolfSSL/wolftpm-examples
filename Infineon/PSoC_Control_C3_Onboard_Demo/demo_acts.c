/* demo_acts.c
 *
 * The demo's acts, run on the PSOC Control C3 and selected by a command byte
 * on the console. The host writes one character and reads back the events,
 * so a host-side demo server becomes a serial reader rather than something
 * that spawns programs and parses their output.
 *
 * Events are the same newline-delimited JSON the host-side examples emit, so
 * an existing UI consumes them unchanged.
 *
 *   i  identity        read the TPM's vendor and firmware
 *   p  measured boot   read the PCR bank
 *   x  extend PCR 16   extend the demo PCR, then re-read the bank
 *   r  reset PCR 16    reset the demo PCR, then re-read the bank
 *   l  sealed secret   seal to a PCR, unseal, extend, fail, reset
 *   e  endorsement     read the EK certificates out of NV
 *   d  spdm            encrypted channel to the TPM (SPDM builds only)
 *
 * The signing act lives in act_sign.c: 's' signs, 't' signs and tampers,
 * and the upper-case forms do the same with ML-DSA-65 instead of 87.
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
#include "act_sign.h"
#include "act_spdm.h"

extern void app_rand_set_dev(WOLFTPM2_DEV* dev);

extern int TPM2_IoCb(TPM2_CTX* ctx, INT32 isRead, UINT32 addr, BYTE* buf,
    UINT16 size, void* userCtx);

/* PCR 16 is the debug-use index, resettable from software, which is what
 * lets the sealed-secret act put the bank back afterwards. */
#ifndef DEMO_SEAL_PCR
#define DEMO_SEAL_PCR 16
#endif
#define DEMO_PCR_COUNT 24

static const char seal_secret[] = "OktoberTech2026";

static WOLFTPM2_DEV dev;
static int tpm_up;

static void act_identity(void)
{
    WOLFTPM2_CAPS caps;
    int rc;

    memset(&caps, 0, sizeof(caps));
    rc = wolfTPM2_GetCapabilities(&dev, &caps);
    if (rc != TPM_RC_SUCCESS) {
        demo_fail("GetCapabilities", rc);
        return;
    }
    demo_put("{\"event\":\"tpm.identity\",\"mfg\":\"");
    demo_put(caps.mfgStr);
    demo_put("\",\"vendor\":\"");
    demo_put(caps.vendorStr);
    demo_put("\",\"fw\":\"");
    demo_put_u32(caps.fwVerMajor);
    demo_put(".");
    demo_put_u32(caps.fwVerMinor);
    demo_put("\",\"on\":\"psoc_c3\"}\r\n");
}

/* The digest length is in/out: it must carry the buffer size in, or the read
 * fails with BUFFER_E before it ever reaches the TPM. */
static int read_pcr(int index, byte *digest, int *digestSz)
{
    *digestSz = TPM_SHA256_DIGEST_SIZE;
    return wolfTPM2_ReadPCR(&dev, index, TPM_ALG_SHA256, digest, digestSz);
}

static void act_pcr(void)
{
    byte digest[TPM_SHA256_DIGEST_SIZE];
    int digestSz, i, rc;

    demo_put("{\"event\":\"pcr.begin\",\"bank\":\"SHA256\"}\r\n");
    for (i = 0; i < DEMO_PCR_COUNT; i++) {
        digestSz = 0;
        memset(digest, 0, sizeof(digest));
        rc = read_pcr(i, digest, &digestSz);
        if (rc != TPM_RC_SUCCESS) {
            demo_fail("ReadPCR", rc);
            break;
        }
        demo_put("{\"event\":\"pcr.value\",\"index\":");
        demo_put_u32((uint32_t)i);
        demo_put(",\"digest\":\"");
        demo_put_hexbuf(digest, (uint32_t)digestSz);
        demo_put("\"}\r\n");
    }
    demo_put("{\"event\":\"pcr.end\"}\r\n");
}

/* Extend the demo PCR with a fixed value, which is what "the measurement
 * changed" means in the sealed-secret act. */
static int extend_pcr(int index)
{
    byte data[TPM_SHA256_DIGEST_SIZE];

    memset(data, 0x5A, sizeof(data));
    return wolfTPM2_ExtendPCR(&dev, index, TPM_ALG_SHA256, data,
            (int)sizeof(data));
}

/* The terminal event for the seal act. Every exit emits exactly one, or
 * the host reader blocks until its idle timeout and the page never leaves
 * the step it was on. */
static void seal_verdict(int pass)
{
    demo_put("{\"event\":\"seal.end\",\"pass\":");
    demo_put(pass ? "true" : "false");
    demo_put("}\r\n");
}

/* Load the sealed blob and unseal it under a PCR policy session. The policy
 * is evaluated against the PCR bank as it stands now, so this succeeds only
 * while the measurement still matches the one sealed to. */
static int try_unseal(WOLFTPM2_KEY *primary, WOLFTPM2_KEYBLOB *seal,
    byte *pcrArray, byte *out, int *outSz)
{
    WOLFTPM2_SESSION policy;
    WOLFTPM2_KEY sealed;
    Unseal_In in;
    Unseal_Out unsealed;
    int rc;

    memset(&policy, 0, sizeof(policy));
    memset(&sealed, 0, sizeof(sealed));
    memset(&in, 0, sizeof(in));
    memset(&unsealed, 0, sizeof(unsealed));

    rc = wolfTPM2_LoadKey(&dev, seal, &primary->handle);
    if (rc != TPM_RC_SUCCESS)
        return rc;
    sealed.handle = seal->handle;

    /* Clear both auth slots first. Anything left in slot 1 from an earlier
     * command is still applied to this one, and the TPM rejects it as a bad
     * authorisation for a session the unseal never meant to use. */
    wolfTPM2_UnsetAuth(&dev, 0);
    wolfTPM2_UnsetAuth(&dev, 1);

    rc = wolfTPM2_StartSession(&dev, &policy, NULL, NULL,
            TPM_SE_POLICY, TPM_ALG_NULL);
    if (rc != TPM_RC_SUCCESS)
        goto unload;

    rc = wolfTPM2_PolicyPCR(&dev, policy.handle.hndl, TPM_ALG_SHA256,
            pcrArray, 1);
    if (rc == TPM_RC_SUCCESS)
        rc = wolfTPM2_SetAuthSession(&dev, 0, &policy,
                TPMA_SESSION_continueSession);
    if (rc != TPM_RC_SUCCESS)
        goto unload_session;

    /* The session HMAC covers the name of the object being authorised, so
     * the name has to be handed over as well as the session. Without this
     * the TPM rejects the authorisation with TPM_RC_BAD_AUTH even though
     * the policy itself is satisfied. */
    wolfTPM2_SetAuthHandleName(&dev, 0, &sealed.handle);

    in.itemHandle = sealed.handle.hndl;
    rc = TPM2_Unseal(&in, &unsealed);
    if (rc == TPM_RC_SUCCESS) {
        /* One byte of the caller's buffer belongs to the terminator, so
         * the payload can never fill it completely. */
        if (*outSz > 0)
            (*outSz)--;
        if ((int)unsealed.outData.size < *outSz)
            *outSz = (int)unsealed.outData.size;
        memcpy(out, unsealed.outData.buffer, (size_t)*outSz);
        out[*outSz] = 0;
    }

unload_session:
    wolfTPM2_UnloadHandle(&dev, &policy.handle);
unload:
    wolfTPM2_UnloadHandle(&dev, &sealed.handle);
    /* Put slot 0 back to the empty password session wolfTPM2_Init leaves
     * there. Clearing it outright is not enough: the next command needing
     * authorisation, PCR_Reset among them, then fails with
     * TPM_RC_AUTH_MISSING rather than succeeding. */
    wolfTPM2_SetAuthPassword(&dev, 0, NULL);
    wolfTPM2_UnsetAuth(&dev, 1);
    return rc;
}

static void act_pcr_extend(void)
{
    int rc = extend_pcr(DEMO_SEAL_PCR);

    if (rc != TPM_RC_SUCCESS)
        demo_fail("extend PCR", rc);
    act_pcr();
}

static void act_pcr_reset(void)
{
    int rc;

    /* PCR_Reset needs an authorisation, and the seal act's policy session
     * may have displaced the default one in slot 0. */
    wolfTPM2_SetAuthPassword(&dev, 0, NULL);
    wolfTPM2_UnsetAuth(&dev, 1);
    rc = wolfTPM2_ResetPCR(&dev, DEMO_SEAL_PCR);
    if (rc != TPM_RC_SUCCESS)
        demo_fail("reset PCR", rc);
    act_pcr();
}

static void act_seal(void)
{
    WOLFTPM2_KEY primary;
    WOLFTPM2_KEYBLOB seal;
    byte digest[TPM_SHA256_DIGEST_SIZE];
    byte out[sizeof(seal_secret) + 1];
    byte pcrArray[1];
    byte policy[TPM_SHA256_DIGEST_SIZE];
    WOLFTPM2_SESSION trial;
    TPMT_PUBLIC tpl;
    word32 policySz = 0;
    int digestSz = 0, outSz, rc, sealedOk = 0;

    memset(&trial, 0, sizeof(trial));

    memset(&primary, 0, sizeof(primary));
    memset(&seal, 0, sizeof(seal));

    demo_put("{\"event\":\"seal.begin\",\"pcr\":");
    demo_put_u32(DEMO_SEAL_PCR);
    demo_put(",\"secret\":\"");
    demo_put(seal_secret);
    demo_put("\"}\r\n");

    /* Whatever the previous act left in the auth slots is applied to the
     * first command of this one. */
    wolfTPM2_SetAuthPassword(&dev, 0, NULL);
    wolfTPM2_UnsetAuth(&dev, 1);

    /* Start from a known measurement so the act is repeatable. */
    rc = wolfTPM2_ResetPCR(&dev, DEMO_SEAL_PCR);
    if (rc == TPM_RC_SUCCESS)
        rc = read_pcr(DEMO_SEAL_PCR, digest, &digestSz);
    if (rc != TPM_RC_SUCCESS) {
        demo_fail("reset PCR", rc);
        seal_verdict(0);
        return;
    }
    demo_put("{\"event\":\"seal.reset\",\"pcr\":");
    demo_put_u32(DEMO_SEAL_PCR);
    demo_put(",\"digest\":\"");
    demo_put_hexbuf(digest, (uint32_t)digestSz);
    demo_put("\"}\r\n");

    /* An ECC storage root key, deliberately. Creating an RSA SRK on this
     * part is slow and has been seen to drop it into failure mode, from
     * which only removing power recovers it. */
    rc = wolfTPM2_GetKeyTemplate_ECC_SRK(&tpl);
    if (rc == TPM_RC_SUCCESS)
        rc = wolfTPM2_CreatePrimaryKey(&dev, &primary, TPM_RH_OWNER, &tpl,
                NULL, 0);
    if (rc != TPM_RC_SUCCESS) {
        demo_fail("CreatePrimaryKey", rc);
        seal_verdict(0);
        return;
    }

    pcrArray[0] = (byte)DEMO_SEAL_PCR;

    /* Work out the policy digest the sealed object must demand, using a
     * trial session: it evaluates the policy without authorising anything.
     * Letting CreateKeySeal_ex derive this from the PCR list alone is not
     * enough, because the object also has to be made policy-only. */
    rc = wolfTPM2_StartSession(&dev, &trial, NULL, NULL, TPM_SE_TRIAL,
            TPM_ALG_NULL);
    if (rc == TPM_RC_SUCCESS) {
        rc = wolfTPM2_PolicyPCR(&dev, trial.handle.hndl, TPM_ALG_SHA256,
                pcrArray, 1);
        if (rc == TPM_RC_SUCCESS) {
            policySz = (word32)sizeof(policy);
            rc = wolfTPM2_GetPolicyDigest(&dev, trial.handle.hndl, policy,
                    &policySz);
        }
        wolfTPM2_UnloadHandle(&dev, &trial.handle);
    }
    if (rc != TPM_RC_SUCCESS) {
        demo_fail("policy digest", rc);
        goto out_fail;
    }
    demo_put("{\"event\":\"seal.policy\",\"bytes\":");
    demo_put_u32(policySz);
    demo_put("}\r\n");

    rc = wolfTPM2_GetKeyTemplate_KeySeal(&tpl, TPM_ALG_SHA256);
    if (rc == TPM_RC_SUCCESS) {
        /* Policy-only: without clearing this the object can be opened with
         * its auth value and the PCR policy is not what gates it. */
        tpl.objectAttributes &= ~TPMA_OBJECT_userWithAuth;
        tpl.authPolicy.size = (UINT16)policySz;
        memcpy(tpl.authPolicy.buffer, policy, policySz);

        rc = wolfTPM2_CreateKeySeal_ex(&dev, &seal, &primary.handle, &tpl,
                NULL, 0, TPM_ALG_SHA256, pcrArray, 1,
                (const byte *)seal_secret, (int)sizeof(seal_secret) - 1);
    }
    if (rc != TPM_RC_SUCCESS) {
        demo_fail("CreateKeySeal", rc);
        goto out_fail;
    }
    demo_put("{\"event\":\"seal.create\",\"pub_bytes\":");
    demo_put_u32(seal.pub.size);
    demo_put(",\"priv_bytes\":");
    demo_put_u32(seal.priv.size);
    demo_put(",\"ok\":true}\r\n");

    /* Unseal while the measurement still matches. */
    outSz = (int)sizeof(out);
    memset(out, 0, sizeof(out));
    rc = try_unseal(&primary, &seal, pcrArray, out, &outSz);
    sealedOk = (rc == TPM_RC_SUCCESS);
    demo_put("{\"event\":\"seal.unseal\",\"stage\":\"before\",\"ok\":");
    demo_put(sealedOk ? "true" : "false");
    if (sealedOk) {
        demo_put(",\"secret\":\"");
        demo_put((const char *)out);
        demo_put("\"");
    }
    else {
        demo_put(",\"rc\":\"0x");
        demo_put_hex32((uint32_t)rc);
        demo_put("\"");
    }
    demo_put("}\r\n");

    /* Change the measurement, which must make the same unseal fail. */
    rc = extend_pcr(DEMO_SEAL_PCR);
    if (rc == TPM_RC_SUCCESS)
        rc = read_pcr(DEMO_SEAL_PCR, digest, &digestSz);
    if (rc == TPM_RC_SUCCESS) {
        demo_put("{\"event\":\"seal.extend\",\"pcr\":");
        demo_put_u32(DEMO_SEAL_PCR);
        demo_put(",\"digest\":\"");
        demo_put_hexbuf(digest, (uint32_t)digestSz);
        demo_put("\"}\r\n");
    }

    outSz = (int)sizeof(out);
    memset(out, 0, sizeof(out));
    rc = try_unseal(&primary, &seal, pcrArray, out, &outSz);
    demo_put("{\"event\":\"seal.unseal\",\"stage\":\"after\",\"ok\":");
    demo_put(rc == TPM_RC_SUCCESS ? "true" : "false");
    demo_put(",\"rc\":\"0x");
    demo_put_hex32((uint32_t)rc);
    demo_put("\"}\r\n");

    /* The act passes only if it told both halves of the story: the secret
     * came back while the measurement matched, and was refused once it did
     * not. A refusal on its own could just mean sealing never worked. */
    seal_verdict(sealedOk && rc != TPM_RC_SUCCESS);

    /* Put the measurement back, and say so if it does not go back: leaving
     * PCR 16 extended makes the next run of this act fail at its own reset
     * and makes the measured-boot tab disagree with this one. */
    rc = wolfTPM2_ResetPCR(&dev, DEMO_SEAL_PCR);
    if (rc != TPM_RC_SUCCESS)
        demo_fail("restore PCR", rc);
    goto out_primary;

out_fail:
    seal_verdict(0);
out_primary:
    wolfTPM2_UnloadHandle(&dev, &primary.handle);
}

/* The endorsement certificates live in the TCG-reserved NV range. Each one
 * is streamed out as it is read rather than collected, because several
 * kilobytes of certificate will not all fit alongside everything else. */
/* The key behind one certificate, so the host can show it belongs to this
 * chip. Only P-256: each match costs a key derivation on the part. */
static void ek_report_pubkey(void)
{
    static WOLFTPM2_KEY ek;
    TPMT_PUBLIC publicTemplate;
    TPMS_ECC_POINT* pt;
    int rc;

    memset(&ek, 0, sizeof(ek));
    memset(&publicTemplate, 0, sizeof(publicTemplate));

    rc = wolfTPM2_GetKeyTemplate_EKIndex(TPM2_NV_ECC_EK_CERT,
            &publicTemplate);
    if (rc == TPM_RC_SUCCESS)
        rc = wolfTPM2_CreatePrimaryKey(&dev, &ek, TPM_RH_ENDORSEMENT,
                &publicTemplate, NULL, 0);
    if (rc != TPM_RC_SUCCESS) {
        demo_fail("CreatePrimary EK", rc);
        return;
    }

    pt = &ek.pub.publicArea.unique.ecc;
    demo_put("{\"event\":\"ek.pub\",\"index\":\"0x");
    demo_put_hex32(TPM2_NV_ECC_EK_CERT);
    demo_put("\",\"curve\":\"P-256\",\"x\":\"");
    demo_put_hexbuf(pt->x.buffer, pt->x.size);
    demo_put("\",\"y\":\"");
    demo_put_hexbuf(pt->y.buffer, pt->y.size);
    demo_put("\"}\r\n");

    wolfTPM2_UnloadHandle(&dev, &ek.handle);
}

static void act_ek_certs(void)
{
    static byte cert[1600];
    static TPML_HANDLE handles;
    word32 nvIndex;
    int found = 0, i, rc;

    demo_put("{\"event\":\"ek.begin\"}\r\n");

    /* Before the certificates, so the host already has the key when it
     * builds each row and can say on the row itself whether it matches. */
    ek_report_pubkey();

    /* Enumerate what is actually in the TCG NV space rather than guessing
     * indices: which certificates a part ships with varies, and a fixed
     * list finds nothing on a part that uses different ones. */
    memset(&handles, 0, sizeof(handles));
    rc = wolfTPM2_GetHandles(TPM_20_TCG_NV_SPACE, &handles);
    if (rc < 0) {
        demo_fail("GetHandles", rc);
        demo_put("{\"event\":\"ek.end\",\"found\":0}\r\n");
        return;
    }

    for (i = 0; i < (int)handles.count; i++) {
        WOLFTPM2_NV nv;
        word32 sz = (word32)sizeof(cert);
        TPMS_NV_PUBLIC nvPublic;

        nvIndex = handles.handle[i];
        memset(&nv, 0, sizeof(nv));
        memset(&nvPublic, 0, sizeof(nvPublic));
        nv.handle.hndl = nvIndex;

        /* Ask how big it is before reading, so a certificate larger than
         * the buffer is skipped rather than truncated into the stream. */
        rc = wolfTPM2_NVReadPublic(&dev, nvIndex, &nvPublic);
        if (rc != TPM_RC_SUCCESS)
            continue;
        if (nvPublic.dataSize == 0 || nvPublic.dataSize > sizeof(cert))
            continue;
        sz = nvPublic.dataSize;

        rc = wolfTPM2_NVReadAuth(&dev, &nv, nvIndex, cert, &sz, 0);
        if (rc != TPM_RC_SUCCESS)
            continue;

        found++;
        demo_put("{\"event\":\"ek.cert\",\"index\":\"0x");
        demo_put_hex32(nvIndex);
        demo_put("\",\"bytes\":");
        demo_put_u32(sz);
        demo_put(",\"b64\":\"");
        demo_put_b64(cert, sz);
        demo_put("\"}\r\n");
    }
    demo_put("{\"event\":\"ek.end\",\"found\":");
    demo_put_u32((uint32_t)found);
    demo_put("}\r\n");
}

/* Every act drives the same device, and one that never came up reports
 * failures from inside wolfTPM that describe nothing a visitor can act on.
 * Retry the init at the point of use instead: a part still in reset when
 * the board powered on comes back on the next press. */
static int tpm_ready(void)
{
    int rc;

    if (tpm_up)
        return 1;

    rc = wolfTPM2_Init(&dev, TPM2_IoCb, NULL);
    app_rand_set_dev(&dev);
    tpm_up = (rc == TPM_RC_SUCCESS);
    if (!tpm_up)
        demo_fail("wolfTPM2_Init", rc);
    demo_put("{\"event\":\"ready\",\"tpm\":");
    demo_put(tpm_up ? "true" : "false");
    demo_put("}\r\n");
    return tpm_up;
}

void main(void)
{
    int rc;

    demo_board_init();
    demo_put("{\"event\":\"boot\",\"host\":\"psoc_c3\"}\r\n");

    rc = wolfTPM2_Init(&dev, TPM2_IoCb, NULL);
    app_rand_set_dev(&dev);
    tpm_up = (rc == TPM_RC_SUCCESS);
    if (!tpm_up) {
        demo_fail("wolfTPM2_Init", rc);
        demo_put("{\"event\":\"ready\",\"tpm\":false}\r\n");
    }
    else {
        demo_put("{\"event\":\"ready\",\"tpm\":true}\r\n");
    }

    /* One character selects an act. Anything else is ignored, so line noise
     * from a host opening the port cannot start a run. */
    for (;;) {
        switch (demo_getc()) {
            case 'i': if (tpm_ready()) act_identity(); break;
            case 'p': if (tpm_ready()) act_pcr();      break;
            case 'l': if (tpm_ready()) act_seal();     break;
            case 'e': if (tpm_ready()) act_ek_certs(); break;
#ifdef WOLFTPM_SPDM_TCG
            case 'd': if (tpm_ready()) (void)act_spdm(&dev); break;
#endif
            case 'x': if (tpm_ready()) act_pcr_extend(); break;
            case 'r': if (tpm_ready()) act_pcr_reset();  break;
            /* Upper case selects ML-DSA-65, lower case the 87 the demo
             * leads with, so an act stays one byte with no mode state. */
            case 's': if (tpm_ready()) act_sign(&dev, 0, TPM_MLDSA_87); break;
            case 'S': if (tpm_ready()) act_sign(&dev, 0, TPM_MLDSA_65); break;
            case 't': if (tpm_ready()) act_sign(&dev, 1, TPM_MLDSA_87);
                break;  /* tampered */
            case 'T': if (tpm_ready()) act_sign(&dev, 1, TPM_MLDSA_65); break;
            default:  break;
        }
    }
}
