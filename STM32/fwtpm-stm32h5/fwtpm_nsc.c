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

/* Global secure fwTPM context pointer (set by main.c) */
extern volatile FWTPM_CTX* g_fwtpmCtx;

FWTPM_NSC_ENTRY
int FWTPM_NSC_ExecuteCommand(const uint8_t* cmdBuf, uint32_t cmdSz,
                              uint8_t* rspBuf, uint32_t* rspSz)
{
    FWTPM_CTX* ctx = (FWTPM_CTX*)g_fwtpmCtx;
    int rc;

    if (ctx == NULL || cmdBuf == NULL || rspBuf == NULL || rspSz == NULL) {
        return -1;
    }
    if (cmdSz > FWTPM_MAX_COMMAND_SIZE || *rspSz < FWTPM_MAX_COMMAND_SIZE) {
        return -2;
    }

#if defined(__ARM_FEATURE_CMSE) && (__ARM_FEATURE_CMSE == 3U)
    /* Validate that pointers are in non-secure memory */
    if (cmse_check_address_range((void*)cmdBuf, cmdSz,
            CMSE_NONSECURE | CMSE_MPU_READ) == NULL) {
        return -3;
    }
    if (cmse_check_address_range(rspBuf, *rspSz,
            CMSE_NONSECURE | CMSE_MPU_READWRITE) == NULL) {
        return -3;
    }
    if (cmse_check_address_range(rspSz, sizeof(*rspSz),
            CMSE_NONSECURE | CMSE_MPU_READWRITE) == NULL) {
        return -3;
    }
#endif

    /* Copy command into secure buffer */
    memcpy(ctx->cmdBuf, cmdBuf, cmdSz);

    /* Process the TPM command */
    {
        int rspSzInt = FWTPM_MAX_COMMAND_SIZE;
        rc = FWTPM_ProcessCommand(ctx, ctx->cmdBuf, (int)cmdSz,
            ctx->rspBuf, &rspSzInt, 0);

        if (rc == 0 && rspSzInt > 0) {
            if ((uint32_t)rspSzInt <= *rspSz) {
                memcpy(rspBuf, ctx->rspBuf, rspSzInt);
                *rspSz = (uint32_t)rspSzInt;
                return 0;
            }
            return -4; /* response too large */
        }
    }

    return rc; /* error */
}

FWTPM_NSC_ENTRY
const char* FWTPM_NSC_GetVersion(void)
{
    return FWTPM_GetVersionString();
}
