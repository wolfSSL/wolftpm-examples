/* fwtpm_nsc.c
 *
 * fwTPM Non-Secure Callable (NSC) entry points.
 * These functions are marked with cmse_nonsecure_entry and placed
 * in the .gnu.sgstubs section by the compiler when built with -mcmse.
 *
 * Copyright (C) 2006-2025 wolfSSL Inc.
 *
 * This file is part of wolfTPM.
 */

#include "user_settings.h"
#include "stm32h5xx_hal.h"

#if defined(__ARM_FEATURE_CMSE) && (__ARM_FEATURE_CMSE == 3U)
#include <arm_cmse.h>
#define FWTPM_NSC_ENTRY __attribute__((cmse_nonsecure_entry))
#else
#define FWTPM_NSC_ENTRY
#endif

#include <wolftpm/fwtpm/fwtpm.h>
#include <wolftpm/fwtpm/fwtpm_command.h>
#include "fwtpm_nsc.h"
#include <string.h>

/* Global secure fwTPM context pointer (set once by main.c at init,
 * before the non-secure world is released; never modified after). */
extern FWTPM_CTX* g_fwtpmCtx;

FWTPM_NSC_ENTRY
int FWTPM_NSC_ExecuteCommand(const uint8_t* cmdBuf, uint32_t cmdSz,
                              uint8_t* rspBuf, uint32_t* rspSz)
{
    FWTPM_CTX* ctx = g_fwtpmCtx;
    uint32_t localRspSz;
    int rspSzInt;
    int rc;

    if (ctx == NULL || cmdBuf == NULL || rspBuf == NULL || rspSz == NULL) {
        return -1;
    }

#if defined(__ARM_FEATURE_CMSE) && (__ARM_FEATURE_CMSE == 3U)
    /* Validate the rspSz pointer itself BEFORE any dereference.
     * A malicious non-secure caller could otherwise pass a pointer into
     * secure memory or device-register space and leak state through
     * timing/error-code side channels. */
    if (cmse_check_address_range(rspSz, sizeof(*rspSz),
            CMSE_NONSECURE | CMSE_MPU_READWRITE) == NULL) {
        return -3;
    }
#endif

    /* Snapshot *rspSz into a local — rspSz points at non-secure memory
     * which can be mutated concurrently (TrustZone double-fetch). Only
     * use localRspSz past this point; write *rspSz exactly once on exit. */
    localRspSz = *rspSz;

    if (cmdSz > FWTPM_MAX_COMMAND_SIZE || localRspSz == 0) {
        *rspSz = 0;
        return -2;
    }

#if defined(__ARM_FEATURE_CMSE) && (__ARM_FEATURE_CMSE == 3U)
    if (cmse_check_address_range((void*)cmdBuf, cmdSz,
            CMSE_NONSECURE | CMSE_MPU_READ) == NULL) {
        *rspSz = 0;
        return -3;
    }
    if (cmse_check_address_range(rspBuf, localRspSz,
            CMSE_NONSECURE | CMSE_MPU_READWRITE) == NULL) {
        *rspSz = 0;
        return -3;
    }
#endif

    /* Copy command into secure buffer */
    memcpy(ctx->cmdBuf, cmdBuf, cmdSz);

    /* Process the TPM command */
    rspSzInt = FWTPM_MAX_COMMAND_SIZE;
    rc = FWTPM_ProcessCommand(ctx, ctx->cmdBuf, (int)cmdSz,
        ctx->rspBuf, &rspSzInt, 0);

    if (rc != 0) {
        *rspSz = 0;
        return rc;
    }

    /* rc == 0 but no response — treat as error rather than silently
     * leaving *rspSz at its input value. */
    if (rspSzInt <= 0) {
        *rspSz = 0;
        return -5;
    }

    if ((uint32_t)rspSzInt > localRspSz) {
        *rspSz = 0;
        return -4; /* response too large for caller buffer */
    }

    memcpy(rspBuf, ctx->rspBuf, (size_t)rspSzInt);
    *rspSz = (uint32_t)rspSzInt;
    return 0;
}

FWTPM_NSC_ENTRY
int FWTPM_NSC_GetVersion(char* versionBuf, uint32_t versionBufSz)
{
    const char* version;
    size_t versionLen;

    if (versionBuf == NULL || versionBufSz == 0) {
        return -1;
    }

#if defined(__ARM_FEATURE_CMSE) && (__ARM_FEATURE_CMSE == 3U)
    if (cmse_check_address_range(versionBuf, versionBufSz,
            CMSE_NONSECURE | CMSE_MPU_READWRITE) == NULL) {
        return -2;
    }
#endif

    version = FWTPM_GetVersionString();
    if (version == NULL) {
        return -3;
    }

    versionLen = strlen(version) + 1;
    if (versionLen > versionBufSz) {
        return -4;
    }

    memcpy(versionBuf, version, versionLen);
    return 0;
}
