/* act_sign.c
 *
 * The demo's central beat, run entirely on the PSOC Control C3: the TPM
 * creates a Hash-ML-DSA key and signs a SHA-256 digest, and the MCU verifies
 * that signature itself with wolfCrypt. Only the public key leaves the TPM.
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

#include <wolfssl/wolfcrypt/sha256.h>
#include <wolfssl/wolfcrypt/wc_mldsa.h>

#include "act_sign.h"
#include "demo_util.h"
#include "i2c_trace.h"

/* The message the host-side example signs, kept identical so the two
 * produce comparable output. */
static const char demo_msg[] =
    "wolfSSL + Infineon OktoberTech 2026 post-quantum TPM demo";

static WOLFTPM2_KEY tpmKey;
static TPMT_PUBLIC tpl;
static MlDsaKey pubKey;
static byte sig[5000];
static byte digest[WC_SHA256_DIGEST_SIZE];

/* An odd count so the median is a sample rather than an average of two.
 * Three runs of ML-DSA-87 is a few seconds, which the page can wait for;
 * one run varies by more than a factor of two on this part. */
#define SIGN_SAMPLE_COUNT 3

typedef struct SignSample {
    uint32_t totalMs;
    uint32_t transferMs;
} SignSample;

/* Sign the same digest with a classical key on the same silicon, so the
 * size and speed comparison the demo draws is between two keys from one
 * part rather than against a quoted figure. */
static void compare_classical(WOLFTPM2_DEV* dev, TPM_ECC_CURVE curve,
    const char* name, const byte* digest, int digestSz)
{
    static WOLFTPM2_KEY eccKey;
    static byte eccSig[256];
    TPMT_PUBLIC ecctpl;
    uint32_t t0, keyMs, signMs, transferMs, tpmMs;
    int sigSz = (int)sizeof(eccSig);
    int rc;

    memset(&eccKey, 0, sizeof(eccKey));
    memset(&ecctpl, 0, sizeof(ecctpl));
    memset(eccSig, 0, sizeof(eccSig));

    rc = wolfTPM2_GetKeyTemplate_ECC(&ecctpl,
            TPMA_OBJECT_sensitiveDataOrigin | TPMA_OBJECT_userWithAuth |
            TPMA_OBJECT_sign | TPMA_OBJECT_fixedTPM |
            TPMA_OBJECT_fixedParent | TPMA_OBJECT_noDA,
            curve, TPM_ALG_ECDSA);
    if (rc != 0)
        return;
    ecctpl.nameAlg = TPM_ALG_SHA256;

    t0 = demo_cycles();
    rc = wolfTPM2_CreatePrimaryKey(dev, &eccKey, TPM_RH_OWNER, &ecctpl,
            NULL, 0);
    keyMs = demo_ms_since(t0);
    if (rc != TPM_RC_SUCCESS)
        return;

    t0 = demo_cycles();
    tpm_i2c_measure_begin();
    rc = wolfTPM2_SignHash(dev, &eccKey, digest, digestSz, eccSig, &sigSz);
    transferMs = tpm_i2c_measure_end_ms();
    signMs = demo_ms_since(t0);
    tpmMs = (signMs > transferMs) ? signMs - transferMs : 0;
    if (rc == TPM_RC_SUCCESS) {
        demo_put("{\"event\":\"compare.classical\",\"alg\":\"");
        demo_put(name);
        demo_put("\",\"sig_bytes\":"); demo_put_u32((uint32_t)sigSz);
        demo_put(",\"keygen_ms\":"); demo_put_u32(keyMs);
        demo_put(",\"sign_ms\":"); demo_put_u32(signMs);
        demo_put(",\"tpm_sign_ms\":"); demo_put_u32(tpmMs);
        demo_put(",\"transfer_ms\":"); demo_put_u32(transferMs);
        demo_put("}\r\n");

        demo_put("{\"event\":\"compare.bytes\",\"alg\":\"");
        demo_put(name);
        demo_put("\",\"bytes\":"); demo_put_u32((uint32_t)sigSz);
        demo_put(",\"b64\":\"");
        demo_put_b64(eccSig, (uint32_t)sigSz);
        demo_put("\"}\r\n");
    }
    wolfTPM2_UnloadHandle(dev, &eccKey.handle);
}

int act_sign(WOLFTPM2_DEV* dev, int tamper, int paramSet)
{
    wc_Sha256 sha;
    SignSample samples[SIGN_SAMPLE_COUNT];
    SignSample sample;
    uint32_t t0, signMs, transferMs, tpmMs, verifyMs, keyMs;
    int i, j, sigSz, verifyRes = 0, rc, pass = 0;
    int wcLevel;

    /* The host picks the parameter set; anything it does not name falls
     * back to the one the demo leads with. */
    switch (paramSet) {
        case TPM_MLDSA_65:
            wcLevel = WC_ML_DSA_65;
            break;
        default:
            paramSet = TPM_MLDSA_87;
            wcLevel = WC_ML_DSA_87;
            break;
    }

    demo_put("{\"event\":\"run.start\",\"host\":\"psoc_c3\",\"alg\":"
        "\"HASH_MLDSA\",\"param_set\":");
    demo_put_u32((uint32_t)(paramSet == TPM_MLDSA_65 ? 65 : 87));
    demo_put(",\"tamper\":");
    demo_put(tamper ? "true" : "false");
    demo_put(",\"i2c_khz\":");
    demo_put_u32((uint32_t)(PSOC_C3_I2C_HZ / 1000));
    demo_put("}\r\n");

    /* Hash-ML-DSA signs a digest the caller computes, so the hashing happens
     * here and only the digest goes to the TPM. */
    rc = wc_InitSha256(&sha);
    if (rc == 0)
        rc = wc_Sha256Update(&sha, (const byte *)demo_msg,
                (word32)(sizeof(demo_msg) - 1));
    if (rc == 0)
        rc = wc_Sha256Final(&sha, digest);
    if (rc != 0) {
        demo_fail("sha256", rc);
        demo_put("{\"event\":\"run.end\"}\r\n");
        return -1;
    }
    demo_put("{\"event\":\"sign.digest.input\",\"alg\":\"SHA256\","
        "\"message\":\"");
    demo_put(demo_msg);
    demo_put("\",\"hex\":\"");
    demo_put_hexbuf(digest, (uint32_t)sizeof(digest));
    demo_put("\"}\r\n");

    t0 = demo_cycles();
    rc = wolfTPM2_GetKeyTemplate_HASH_MLDSA(&tpl,
            TPMA_OBJECT_sign | TPMA_OBJECT_fixedTPM |
            TPMA_OBJECT_fixedParent | TPMA_OBJECT_sensitiveDataOrigin |
            TPMA_OBJECT_userWithAuth | TPMA_OBJECT_noDA,
            (TPMI_MLDSA_PARAMETER_SET)paramSet, TPM_ALG_SHA256);
    if (rc != 0) {
        demo_fail("GetKeyTemplate_HASH_MLDSA", rc);
        demo_put("{\"event\":\"run.end\"}\r\n");
        return -1;
    }
    rc = wolfTPM2_CreatePrimaryKey(dev, &tpmKey, TPM_RH_OWNER, &tpl, NULL, 0);
    keyMs = demo_ms_since(t0);
    if (rc != TPM_RC_SUCCESS) {
        demo_fail("CreatePrimaryKey", rc);
        demo_put("{\"event\":\"run.end\"}\r\n");
        return -1;
    }
    demo_put("{\"event\":\"key.create\",\"alg\":\"HASH_MLDSA\",\"pub_bytes\":");
    demo_put_u32(tpmKey.pub.publicArea.unique.mldsa.size);
    demo_put(",\"ms\":"); demo_put_u32(keyMs); demo_put("}\r\n");

    /* Sign several times and report the median. A single run of this part
     * varies by more than a factor of two, so one number read off the page
     * is not the one anyone would measure next. */
    for (i = 0; i < SIGN_SAMPLE_COUNT; i++) {
        sigSz = (int)sizeof(sig);
        t0 = demo_cycles();
        tpm_i2c_measure_begin();
        rc = wolfTPM2_SignDigest(dev, &tpmKey, digest, (int)sizeof(digest),
                NULL, 0, sig, &sigSz);
        samples[i].transferMs = tpm_i2c_measure_end_ms();
        samples[i].totalMs = demo_ms_since(t0);
        if (rc != TPM_RC_SUCCESS) {
            demo_fail("SignDigest", rc);
            goto unload;
        }
    }
    for (i = 1; i < SIGN_SAMPLE_COUNT; i++) {
        sample = samples[i];
        j = i;
        while ((j > 0) && (samples[j - 1].totalMs > sample.totalMs)) {
            samples[j] = samples[j - 1];
            j--;
        }
        samples[j] = sample;
    }
    sample = samples[SIGN_SAMPLE_COUNT / 2];
    signMs = sample.totalMs;
    transferMs = sample.transferMs;
    tpmMs = (signMs > transferMs) ? signMs - transferMs : 0;
    demo_put("{\"event\":\"sign.digest\",\"alg\":\"HASH_MLDSA\","
        "\"param_set\":");
    demo_put_u32((uint32_t)(paramSet == TPM_MLDSA_65 ? 65 : 87));
    demo_put(",\"sig_bytes\":");
    demo_put_u32((uint32_t)sigSz);
    demo_put(",\"ms\":"); demo_put_u32(signMs);
    demo_put(",\"tpm_sign_ms\":"); demo_put_u32(tpmMs);
    demo_put(",\"transfer_ms\":"); demo_put_u32(transferMs);
    demo_put(",\"samples\":"); demo_put_u32(SIGN_SAMPLE_COUNT);
    demo_put(",\"min_ms\":"); demo_put_u32(samples[0].totalMs);
    demo_put(",\"max_ms\":");
    demo_put_u32(samples[SIGN_SAMPLE_COUNT - 1].totalMs);
    demo_put("}\r\n");

    demo_put("{\"event\":\"sign.bytes\",\"alg\":\"HASH_MLDSA\",\"bytes\":");
    demo_put_u32((uint32_t)sigSz);
    demo_put(",\"b64\":\"");
    demo_put_b64(sig, (uint32_t)sigSz);
    demo_put("\"}\r\n");

    if (tamper) {
        sig[sigSz / 2] ^= 0x01;
        demo_put("{\"event\":\"tamper\",\"offset\":");
        demo_put_u32((uint32_t)(sigSz / 2));
        demo_put("}\r\n");
    }

    rc = wc_MlDsaKey_Init(&pubKey, NULL, INVALID_DEVID);
    if (rc == 0)
        rc = wc_MlDsaKey_SetParams(&pubKey, wcLevel);
    if (rc == 0)
        rc = wc_MlDsaKey_ImportPubRaw(&pubKey,
                tpmKey.pub.publicArea.unique.mldsa.buffer,
                tpmKey.pub.publicArea.unique.mldsa.size);
    if (rc != 0) {
        demo_fail("import public key", rc);
        wc_MlDsaKey_Free(&pubKey);
        goto unload;
    }

    t0 = demo_cycles();
    rc = wc_MlDsaKey_VerifyCtxHash(&pubKey, sig, (word32)sigSz, NULL, 0,
            digest, (word32)sizeof(digest), WC_HASH_TYPE_SHA256, &verifyRes);
    verifyMs = demo_ms_since(t0);
    /* A rejected signature surfaces as SIG_VERIFY_E rather than res == 0.
     * Any other error means verification never ran, so it is not a tamper
     * rejection and the act fails. */
    if (rc == SIG_VERIFY_E) {
        rc = 0;
        verifyRes = 0;
    }
    else if (rc != 0) {
        demo_fail("verify signature", rc);
        verifyRes = 0;
    }

    demo_put("{\"event\":\"verify.host\",\"backend\":\"wolfcrypt\",\"on\":"
        "\"psoc_c3\",\"result\":\"");
    demo_put(verifyRes ? "valid" : "invalid");
    demo_put("\",\"ms\":"); demo_put_u32(verifyMs); demo_put("}\r\n");

    /* Success means the outcome matched the intent: an untampered signature
     * must verify and a tampered one must not. */
    pass = (rc == 0) && ((verifyRes != 0) == (tamper == 0));
    demo_put("{\"event\":\"result\",\"pass\":");
    demo_put(pass ? "true" : "false");
    demo_put("}\r\n");

    wc_MlDsaKey_Free(&pubKey);

    /* Both curves against the same digest, so the panel can show the
     * post-quantum signature beside two classical ones. */
    compare_classical(dev, TPM_ECC_NIST_P256, "ECDSA_P256", digest,
            (int)sizeof(digest));
    compare_classical(dev, TPM_ECC_NIST_P384, "ECDSA_P384", digest,
            (int)sizeof(digest));

unload:
    wolfTPM2_UnloadHandle(dev, &tpmKey.handle);
    demo_put("{\"event\":\"run.end\"}\r\n");
    return pass ? 0 : -1;
}
