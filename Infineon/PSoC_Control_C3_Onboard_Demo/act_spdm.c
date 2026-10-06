/* act_spdm.c
 *
 * Negotiate a TCG SPDM session with the fitted TPM from the MCU, and run a
 * command inside it. The point of the act is what a logic analyser sees: the
 * same bus that carried plaintext for every other act now carries ciphertext.
 *
 * The responder key has to be pinned for the session to mean anything. A key
 * read off the part during the handshake proves only that the part is the one
 * answering, not that it is the one we meant to talk to, so the demo pins a
 * key captured once from this sample and says so. With none pinned the act
 * runs in discovery mode: it reads the key out and prints it, which is how
 * the pinned value below was obtained in the first place.
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

#include <wolftpm/tpm2_wrap.h>

#include "act_spdm.h"
#include "demo_util.h"
#include "i2c_trace.h"

/* The board supplies its own IO callback; wolfTPM only declares one
 * when its example HAL is compiled in, which it is not here. */
extern int TPM2_IoCb(TPM2_CTX* ctx, INT32 isRead, UINT32 addr,
    BYTE* buf, UINT16 size, void* userCtx);
extern void app_heap_usage(unsigned long* used, unsigned long* total);
extern void app_rand_set_dev(WOLFTPM2_DEV* dev);

static void report_heap(const char* where)
{
    unsigned long used = 0, total = 0;
    app_heap_usage(&used, &total);
    demo_put("{\"event\":\"heap\",\"at\":\"");
    demo_put(where);
    demo_put("\",\"used\":"); demo_put_u32((uint32_t)used);
    demo_put(",\"total\":"); demo_put_u32((uint32_t)total);
    demo_put("}\r\n");
}

#ifdef WOLFTPM_SPDM_TCG

/* P-384 X||Y of the responder, 96 raw bytes. Empty until captured from the
 * part with the discovery path below. */
#ifndef DEMO_SPDM_RSP_PUBKEY
static const byte spdm_rsp_pubkey[] = { 0 };
#define DEMO_SPDM_RSP_PUBKEY_SZ 0
#else
static const byte spdm_rsp_pubkey[] = { DEMO_SPDM_RSP_PUBKEY };
#define DEMO_SPDM_RSP_PUBKEY_SZ ((word32)sizeof(spdm_rsp_pubkey))
#endif

static void step(int n, const char* state, const char* detail)
{
    demo_put("{\"event\":\"spdm.step\",\"n\":");
    demo_put_u32((uint32_t)n);
    demo_put(",\"state\":\"");
    demo_put(state);
    if (detail != NULL) {
        demo_put("\",\"detail\":\"");
        demo_put(detail);
    }
    demo_put("\"}\r\n");
}

static void finish(WOLFTPM2_DEV* dev, int total, int passed)
{
    /* Every exit path comes through here, so this is the one place the bus
     * record is certain to be printed. */
    i2c_trace_dump();
    demo_put("{\"event\":\"spdm.results\",\"total\":");
    demo_put_u32((uint32_t)total);
    demo_put(",\"passed\":");
    demo_put_u32((uint32_t)passed);
    demo_put(",\"failed\":");
    demo_put_u32((uint32_t)(total - passed));
    demo_put("}\r\n");
    demo_put("{\"event\":\"spdm.end\",\"units\":");
    demo_put_u32((uint32_t)passed);
    demo_put("}\r\n");
    (void)dev;
}

static uint32_t mark_last;

/* Start the interval clock. Without this the first call of a run is timed
 * against zero and reports the whole uptime, and a later run reports against
 * whatever the previous one left behind. */
static void mark_begin(void)
{
    mark_last = demo_uptime_ms();
}

/* Each SPDM call, its result and how long it took. A call that never
 * returns leaves its predecessor as the last line printed, which is the
 * only way to tell a slow handshake from a stalled one. */
static void mark(const char* call, int rc)
{
    uint32_t now = demo_uptime_ms();

    demo_put("{\"event\":\"spdm.call\",\"name\":\"");
    demo_put(call);
    demo_put("\",\"rc\":");
    demo_put_u32((uint32_t)rc);
    demo_put(",\"ms\":");
    demo_put_u32(now - mark_last);
    demo_put("}\r\n");
    mark_last = now;
}

/* Read the responder key out of the part and print it, so it can be pinned.
 * This establishes no trust on its own; it is the first half of a
 * trust-on-first-use step a human completes by building it in. */
static int discover(WOLFTPM2_DEV* dev)
{
    byte pubKey[128];
    word32 pubKeySz = (word32)sizeof(pubKey);
    int rc;

    step(1, "running", "reading the responder key");
    /* From here on every bus transfer is recorded. The TPM is already up, so
     * what the ring buffer holds is the SPDM exchange and nothing else. */
    i2c_trace_reset();
    mark_begin();
    /* The TCG binding header is added by the send path only for a context
     * already in TCG mode; a fresh context is WOLFSPDM_MODE_AUTO and sends
     * the SPDM message bare. The TPM then sizes a command from bytes that
     * are not there, sets Expect, and waits for the rest forever. */
    rc = wolfSPDM_SetMode(dev->spdmCtx->spdmCtx, WOLFSPDM_MODE_TCG);
    if (rc != 0) {
        demo_fail("SetMode", rc);
        step(1, "fail", NULL);
        return rc;
    }
    /* The connect paths wire the SPDM transport to TIS before sending
     * anything; a bare GetVersion here would otherwise fail with
     * WOLFSPDM_E_IO_FAIL because the context has no way to reach the bus. */
    rc = wolfTPM2_SPDM_SetTisIO(dev->spdmCtx);
    if (rc != 0 && rc != NOT_COMPILED_IN) {
        demo_fail("SetTisIO", rc);
        step(1, "fail", NULL);
        return rc;
    }
    mark("SetTisIO", rc);
    rc = wolfTPM2_SpdmGetVersion(dev);
    mark("GetVersion", rc);
    if (rc == TPM_RC_SUCCESS) {
        rc = wolfSPDM_TCG_GetCapabilities(dev->spdmCtx->spdmCtx,
                WOLFSPDM_TCG_CAPS_FLAGS_DEFAULT);
        mark("GetCapabilities", rc);
    }
    if (rc == TPM_RC_SUCCESS) {
        rc = wolfSPDM_TCG_NegotiateAlgorithms(dev->spdmCtx->spdmCtx);
        mark("NegotiateAlgorithms", rc);
    }
    if (rc == TPM_RC_SUCCESS) {
        rc = wolfTPM2_SpdmGetPubKey(dev, pubKey, &pubKeySz);
        mark("GetPubKey", rc);
    }
    if (rc != TPM_RC_SUCCESS) {
        demo_fail("SpdmGetPubKey", rc);
        step(1, "fail", NULL);
        return rc;
    }
    step(1, "pass", "key read");
    demo_put("{\"event\":\"spdm.pubkey\",\"bytes\":");
    demo_put_u32(pubKeySz);
    demo_put(",\"hex\":\"");
    demo_put_hexbuf(pubKey, pubKeySz);
    demo_put("\"}\r\n");
    demo_put("{\"event\":\"log\",\"message\":\"no responder key pinned - "
        "build with DEMO_SPDM_RSP_PUBKEY set to the bytes above\"}\r\n");
    return rc;
}

int act_spdm(WOLFTPM2_DEV* dev)
{
    WOLFTPM2_CAPS caps;
    int rc, passed = 0, total = 5;

    /* Discovery reads the responder key and stops; it negotiates no session
     * and must not be reported as one. The host keys its verdict off this. */
    demo_put("{\"event\":\"spdm.begin\",\"mode\":\"");
    demo_put(DEMO_SPDM_RSP_PUBKEY_SZ == 0 ? "discovery" : "tcg-asym");
    demo_put("\",\"on\":\"psoc_c3\"}\r\n");

    /* The session is set up at init, so the device is torn down and brought
     * back with the responder key and TCG mode in place. */
    wolfTPM2_Cleanup(dev);

    if (DEMO_SPDM_RSP_PUBKEY_SZ == 0) {
        report_heap("before init");
        rc = wolfTPM2_Init(dev, TPM2_IoCb, NULL);
        app_rand_set_dev(dev);
        if (rc != TPM_RC_SUCCESS)
            demo_fail("Init for discovery", rc);
        report_heap("after init");
        if (rc == TPM_RC_SUCCESS) {
            rc = wolfTPM2_SpdmInit(dev);
            /* A failed allocation and a failed init both surface as
             * MEMORY_E; the heap figure separates the two. */
            report_heap("after SpdmInit");
            if (rc != TPM_RC_SUCCESS)
                demo_fail("SpdmInit", rc);
        }
        if (rc == TPM_RC_SUCCESS)
            rc = discover(dev);
        finish(dev, 1, rc == TPM_RC_SUCCESS ? 1 : 0);
        goto restore;
    }

    i2c_trace_reset();
    step(1, "running", "init with the pinned responder key");
    rc = wolfTPM2_InitWithSpdmKey_ex(dev, TPM2_IoCb, NULL,
            spdm_rsp_pubkey, DEMO_SPDM_RSP_PUBKEY_SZ, WOLFSPDM_MODE_TCG);
    app_rand_set_dev(dev);
    if (rc != TPM_RC_SUCCESS) {
        demo_fail("InitWithSpdmKey", rc);
        step(1, "fail", NULL);
        finish(dev, total, passed);
        goto restore;
    }
    step(1, "pass", "responder key pinned");
    passed++;

    /* Init with a pinned key runs the whole handshake itself, so the context
     * and the session already exist. Running them again on a live session
     * sends a record the responder cannot decrypt - its sequence number has
     * moved on - and it answers SPDM ERROR DecryptError. */
    step(2, "running", "SPDM context");
    if (!wolfTPM2_SpdmIsConnected(dev)) {
        rc = wolfTPM2_SpdmInit(dev);
        if (rc != TPM_RC_SUCCESS) {
            demo_fail("SpdmInit", rc);
            step(2, "fail", NULL);
            finish(dev, total, passed);
            goto restore;
        }
    }
    step(2, "pass", NULL);
    passed++;

    step(3, "running", "TCG asymmetric handshake");
    if (!wolfTPM2_SpdmIsConnected(dev)) {
        rc = wolfTPM2_SpdmConnectInfineonTcg(dev, NULL, 0, NULL, 0);
        if (rc != TPM_RC_SUCCESS) {
            demo_fail("SpdmConnectInfineonTcg", rc);
            step(3, "fail", NULL);
            finish(dev, total, passed);
            goto restore;
        }
    }
    step(3, "pass", "AES-256-GCM");
    passed++;
    demo_put("{\"event\":\"spdm.session\",\"id\":\"0x");
    demo_put_hex32((uint32_t)wolfTPM2_SpdmGetSessionId(dev));
    demo_put("\",\"cipher\":\"AES-256-GCM\"}\r\n");

    /* A command inside the session. Everything it puts on the bus is
     * ciphertext, which is the whole claim of the act. */
    step(4, "running", "a TPM command inside the session");
    memset(&caps, 0, sizeof(caps));
    rc = wolfTPM2_GetCapabilities(dev, &caps);
    if (rc != TPM_RC_SUCCESS) {
        demo_fail("GetCapabilities in session", rc);
        step(4, "fail", NULL);
    }
    else {
        step(4, "pass", "encrypted GetCapabilities");
        passed++;
        demo_put("{\"event\":\"spdm.encrypted\",\"mfg\":\"");
        demo_put(caps.mfgStr);
        demo_put("\",\"fw\":\"");
        demo_put_u32(caps.fwVerMajor);
        demo_put(".");
        demo_put_u32(caps.fwVerMinor);
        demo_put("\"}\r\n");
    }

    step(5, "running", "close the session");
    rc = wolfTPM2_SpdmDisconnect(dev);
    if (rc != TPM_RC_SUCCESS) {
        demo_fail("SpdmDisconnect", rc);
        step(5, "fail", NULL);
    }
    else {
        step(5, "pass", NULL);
        passed++;
    }
    wolfTPM2_SpdmCleanup(dev);
    finish(dev, total, passed);

restore:
    /* Back to the plain device the other acts expect. */
    wolfTPM2_Cleanup(dev);
    rc = wolfTPM2_Init(dev, TPM2_IoCb, NULL);
    app_rand_set_dev(dev);
    if (rc != TPM_RC_SUCCESS)
        demo_fail("re-init after SPDM", rc);
    return passed;
}

#endif /* WOLFTPM_SPDM_TCG */
