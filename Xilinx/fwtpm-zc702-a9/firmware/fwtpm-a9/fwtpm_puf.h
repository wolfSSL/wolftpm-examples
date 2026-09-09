/* fwtpm_puf.h
 *
 * wolfCrypt SRAM PUF integration for the Zynq-7000 fwTPM: derives a silicon-
 * unique key from the power-on state of on-chip memory (OCM) and supplies it as
 * the fwTPM NV-journal integrity key (FWTPM_NV_HAL.get_integrity_key), so the
 * TPM's NV integrity root is regenerated from the device rather than stored.
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA
 */

#ifndef FWTPM_PUF_H
#define FWTPM_PUF_H

#include <wolftpm/fwtpm/fwtpm.h>

/* Read the OCM PUF source, enroll (first boot) or reconstruct (later boots,
 * using persisted helper data), and derive the device-unique integrity key.
 * On success *enrolled is set to 1 if this boot enrolled (no stored helper was
 * found) or 0 if it reconstructed from stored helper data. Returns 0 on
 * success, a wolfCrypt error otherwise. */
int FwTPM_Puf_Init(int* enrolled);

/* As FwTPM_Puf_Init, but with the re-provisioning decision as an argument:
 * forceEnroll nonzero ignores any stored helper data and enrolls this boot
 * (the enrollment fails unless the new helper data is durably persisted).
 * FwTPM_Puf_Init passes the FWTPM_PUF_FORCE_ENROLL build flag through. */
int FwTPM_Puf_InitEx(int forceEnroll, int* enrolled);

/* FWTPM_NV_HAL.get_integrity_key hook: returns the 32-byte PUF-derived key.
 * FwTPM_Puf_Init() must have run first. */
int FwTPM_Puf_GetIntegrityKey(void* halCtx, unsigned char* key,
    unsigned int* keySz);

/* Print the device identity fingerprint and PUF profile to the console. */
void FwTPM_Puf_PrintInfo(int enrolled);

/* Weak helper-data persistence seam. The default (RAM NV) build provides no-op
 * versions (load reports "not found", so every boot enrolls); the QSPI NV
 * backend overrides these to persist helper data across power cycles.
 *   load:  fill helper[0..helperSz) and *profileId; return 0 if found, else -1.
 *   store: persist helper + profileId; return 0 on success. */
int fwtpm_puf_helper_load(unsigned char* helper, unsigned int helperSz,
    unsigned int* profileId);
int fwtpm_puf_helper_store(const unsigned char* helper, unsigned int helperSz,
    unsigned int profileId);

#ifdef FWTPM_PUF_SELFTEST
/* Synthetic SRAM PUF regression (enroll -> clean reconstruct -> t-flip
 * reconstruct -> over-t must-fail -> bad-arg -> zeroize), mirroring the
 * wolfCrypt puf_test. Prints per-step results and returns 0 on PASS. Requires a
 * build with -DFWTPM_PUF_SELFTEST (which also enables WOLFSSL_PUF_TEST). */
int FwTPM_Puf_SelfTest(void);
#endif

#endif /* FWTPM_PUF_H */
