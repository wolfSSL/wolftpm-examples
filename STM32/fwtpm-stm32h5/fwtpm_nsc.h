/* fwtpm_nsc.h
 *
 * fwTPM Non-Secure Callable (NSC) interface.
 * Include this header from both secure and non-secure code.
 *
 * Copyright (C) 2006-2025 wolfSSL Inc.
 *
 * This file is part of wolfTPM.
 */

#ifndef FWTPM_NSC_H
#define FWTPM_NSC_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Execute a TPM 2.0 command.
 * cmdBuf/cmdSz: input command buffer and size
 * rspBuf: output response buffer (caller allocated)
 * rspSz: in/out - max size on input, actual size on output
 * Returns 0 on success, negative on error. */
int FWTPM_NSC_ExecuteCommand(const uint8_t* cmdBuf, uint32_t cmdSz,
                              uint8_t* rspBuf, uint32_t* rspSz);

/* Get fwTPM version string. */
const char* FWTPM_NSC_GetVersion(void);

#ifdef __cplusplus
}
#endif

#endif /* FWTPM_NSC_H */
