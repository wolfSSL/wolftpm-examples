/* fwtpm_pqc_test.c
 *
 * APU-side ML-KEM + ML-DSA round-trip test against the ZCU102 R5 fwTPM
 * server over OpenAMP RPMsg. Uses the wolfTPM2 wrapper API for command
 * marshaling and wires a custom TPM2_IoCb that pushes the command
 * buffer through /dev/rpmsg<N> and reads the response back.
 *
 * Build (PetaLinux SDK or aarch64 cross, with sibling wolfssl + wolftpm
 * checkouts):
 *   make fwtpm_pqc_test
 *
 * Run (as root, R5 firmware loaded via remoteproc):
 *   ./fwtpm_pqc_test          # both ML-KEM-768 encap and ML-DSA-65 sign
 *   ./fwtpm_pqc_test -kem     # ML-KEM-768 encap only
 *   ./fwtpm_pqc_test -dsa     # ML-DSA-65 sign only
 *
 * Copyright (C) 2006-2026 wolfSSL Inc.
 *
 * This file is part of wolfTPM.
 *
 * wolfTPM is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 */

/* Pull autoconf-generated build settings from the installed wolfSSL.
 * PetaLinux's wolfssl recipe installs <wolfssl/options.h>; same for
 * wolftpm. If you build with WOLFSSL_USER_SETTINGS, provide your own
 * user_settings.h on the include path instead of options.h. */
#include <wolfssl/options.h>

#include <wolftpm/tpm2.h>
#include <wolftpm/tpm2_wrap.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <linux/rpmsg.h>

#define RPMSG_CTRL "/dev/rpmsg_ctrl0"
#define EPT_NAME   "wolftpm"

static int g_rpmsg_fd = -1;

/* RPMsg endpoint open. Mirrors fwtpm_rpmsg_test.c::open_endpoint --
 * the R5 firmware advertises endpoint addr 1024 (RPMSG_RESERVED_ADDRESSES). */
static int rpmsg_open(void)
{
    int ctrl_fd;
    struct rpmsg_endpoint_info info;
    int rc;
    int ept_fd;
    char dev_path[64];
    int i;

    ctrl_fd = open(RPMSG_CTRL, O_RDWR);
    if (ctrl_fd < 0) {
        fprintf(stderr, "open %s: %s\n", RPMSG_CTRL, strerror(errno));
        return -1;
    }
    memset(&info, 0, sizeof(info));
    strncpy(info.name, EPT_NAME, sizeof(info.name) - 1);
    info.src = 0xFFFFFFFFu;
    info.dst = 1024u;
    rc = ioctl(ctrl_fd, RPMSG_CREATE_EPT_IOCTL, &info);
    if (rc < 0) {
        fprintf(stderr, "RPMSG_CREATE_EPT_IOCTL: %s\n", strerror(errno));
        close(ctrl_fd);
        return -1;
    }
    for (i = 0; i < 32; i++) {
        snprintf(dev_path, sizeof(dev_path), "/dev/rpmsg%d", i);
        ept_fd = open(dev_path, O_RDWR);
        if (ept_fd >= 0) {
            close(ctrl_fd);
            return ept_fd;
        }
    }
    fprintf(stderr, "no /dev/rpmsg<N> after CREATE_EPT_IOCTL\n");
    close(ctrl_fd);
    return -1;
}

/* TPM2 IO callback. wolfTPM2_Init() configures this; the wolfTPM core
 * marshals each command into ctx->cmdBuf, calls back here, then parses
 * the response from the same buffer. xferSz is the total in/out size
 * including the command/response header. */
static int rpmsg_io_cb(TPM2_CTX* ctx, const BYTE* txBuf, BYTE* rxBuf,
                       UINT16 xferSz, void* userCtx)
{
    ssize_t n;
    UINT32 rspSize;
    UINT32 cmdSize;

    (void)ctx;
    (void)userCtx;
    if (g_rpmsg_fd < 0 || txBuf == NULL || rxBuf == NULL) {
        return TPM_RC_FAILURE;
    }
    /* TPM2 command header: tag(2) | size(4) | code(4). Parse cmd size
     * from txBuf so we send exactly what the marshaller produced. */
    cmdSize = ((UINT32)txBuf[2] << 24) | ((UINT32)txBuf[3] << 16) |
              ((UINT32)txBuf[4] << 8)  | txBuf[5];
    if (cmdSize == 0 || cmdSize > xferSz) {
        fprintf(stderr, "rpmsg_io_cb: bad cmd size %u (xferSz %u)\n",
                cmdSize, xferSz);
        return TPM_RC_FAILURE;
    }
    n = write(g_rpmsg_fd, txBuf, cmdSize);
    if (n != (ssize_t)cmdSize) {
        fprintf(stderr, "rpmsg_io_cb: write %zd != %u: %s\n",
                n, cmdSize, strerror(errno));
        return TPM_RC_FAILURE;
    }
    n = read(g_rpmsg_fd, rxBuf, xferSz);
    if (n < 10) {
        fprintf(stderr, "rpmsg_io_cb: short read %zd\n", n);
        return TPM_RC_FAILURE;
    }
    rspSize = ((UINT32)rxBuf[2] << 24) | ((UINT32)rxBuf[3] << 16) |
              ((UINT32)rxBuf[4] << 8)  | rxBuf[5];
    if (rspSize != (UINT32)n) {
        fprintf(stderr, "rpmsg_io_cb: read %zd != header %u\n",
                n, rspSize);
        /* tolerate -- size field reflects truth, not transport */
    }
    return TPM_RC_SUCCESS;
}

static int run_mlkem(void)
{
    int rc;
    WOLFTPM2_DEV dev;
    WOLFTPM2_KEY key;
    TPMT_PUBLIC pub;
    /* MLKEM-1024 ct = 1568 B; size for max. */
    byte ct[1600];
    int ctSz = (int)sizeof(ct);
    byte secret1[64], secret2[64];
    int sec1Sz = (int)sizeof(secret1);
    int sec2Sz = (int)sizeof(secret2);

    memset(&dev, 0, sizeof(dev));
    memset(&key, 0, sizeof(key));
    memset(&pub, 0, sizeof(pub));

    printf("--- ML-KEM-768 encap/decap ---\n");
    rc = wolfTPM2_Init(&dev, rpmsg_io_cb, NULL);
    if (rc != TPM_RC_SUCCESS) {
        printf("wolfTPM2_Init: 0x%x\n", rc);
        return -1;
    }
    rc = wolfTPM2_GetKeyTemplate_MLKEM(&pub,
        TPMA_OBJECT_decrypt | TPMA_OBJECT_fixedTPM |
        TPMA_OBJECT_fixedParent | TPMA_OBJECT_sensitiveDataOrigin |
        TPMA_OBJECT_userWithAuth | TPMA_OBJECT_noDA, TPM_MLKEM_768);
    if (rc != TPM_RC_SUCCESS) goto exit_kem;

    rc = wolfTPM2_CreatePrimaryKey(&dev, &key, TPM_RH_OWNER, &pub, NULL, 0);
    if (rc != TPM_RC_SUCCESS) {
        printf("CreatePrimary(MLKEM-768): 0x%x\n", rc);
        goto exit_kem;
    }
    rc = wolfTPM2_Encapsulate(&dev, &key, ct, &ctSz, secret1, &sec1Sz);
    if (rc != TPM_RC_SUCCESS) {
        printf("Encapsulate: 0x%x\n", rc);
        goto exit_kem;
    }
    rc = wolfTPM2_Decapsulate(&dev, &key, ct, ctSz, secret2, &sec2Sz);
    if (rc != TPM_RC_SUCCESS) {
        printf("Decapsulate: 0x%x\n", rc);
        goto exit_kem;
    }
    if (sec1Sz != sec2Sz || memcmp(secret1, secret2, sec1Sz) != 0) {
        printf("ML-KEM secrets do NOT match\n");
        rc = -1;
        goto exit_kem;
    }
    printf("ML-KEM-768 OK: ct=%d B, secret=%d B match\n", ctSz, sec1Sz);

exit_kem:
    wolfTPM2_UnloadHandle(&dev, &key.handle);
    wolfTPM2_Cleanup(&dev);
    return (rc == TPM_RC_SUCCESS) ? 0 : -1;
}

static int run_mldsa(void)
{
    int rc;
    WOLFTPM2_DEV dev;
    WOLFTPM2_KEY key;
    TPMT_PUBLIC pub;
    TPM_HANDLE seq = 0;
    TPMT_TK_VERIFIED validation;
    byte msg[] = "wolfTPM ZCU102 R5 fwTPM Pure ML-DSA round-trip";
    int msgSz = (int)sizeof(msg) - 1;
    byte sig[5000];     /* MLDSA-87 sig = 4627 B; size for max. */
    int sigSz = (int)sizeof(sig);

    memset(&dev, 0, sizeof(dev));
    memset(&key, 0, sizeof(key));
    memset(&pub, 0, sizeof(pub));
    memset(&validation, 0, sizeof(validation));

    printf("--- ML-DSA-65 sign/verify ---\n");
    rc = wolfTPM2_Init(&dev, rpmsg_io_cb, NULL);
    if (rc != TPM_RC_SUCCESS) {
        printf("wolfTPM2_Init: 0x%x\n", rc);
        return -1;
    }
    rc = wolfTPM2_GetKeyTemplate_MLDSA(&pub,
        TPMA_OBJECT_sign | TPMA_OBJECT_fixedTPM |
        TPMA_OBJECT_fixedParent | TPMA_OBJECT_sensitiveDataOrigin |
        TPMA_OBJECT_userWithAuth | TPMA_OBJECT_noDA,
        TPM_MLDSA_65, 0 /* allowExternalMu */);
    if (rc != TPM_RC_SUCCESS) goto exit_dsa;

    rc = wolfTPM2_CreatePrimaryKey(&dev, &key, TPM_RH_OWNER, &pub, NULL, 0);
    if (rc != TPM_RC_SUCCESS) {
        printf("CreatePrimary(MLDSA-65): 0x%x\n", rc);
        goto exit_dsa;
    }
    /* Pure ML-DSA is one-shot: message via Complete buffer. */
    rc = wolfTPM2_SignSequenceStart(&dev, &key, NULL, 0, &seq);
    if (rc != TPM_RC_SUCCESS) {
        printf("SignSeqStart: 0x%x\n", rc);
        goto exit_dsa;
    }
    rc = wolfTPM2_SignSequenceComplete(&dev, seq, &key,
                                        msg, msgSz, sig, &sigSz);
    if (rc != TPM_RC_SUCCESS) {
        printf("SignSeqComplete: 0x%x\n", rc);
        goto exit_dsa;
    }
    rc = wolfTPM2_VerifySequenceStart(&dev, &key, NULL, 0, &seq);
    if (rc != TPM_RC_SUCCESS) {
        printf("VerifySeqStart: 0x%x\n", rc);
        goto exit_dsa;
    }
    rc = wolfTPM2_VerifySequenceUpdate(&dev, seq, msg, msgSz);
    if (rc != TPM_RC_SUCCESS) {
        printf("VerifySeqUpdate: 0x%x\n", rc);
        goto exit_dsa;
    }
    rc = wolfTPM2_VerifySequenceComplete(&dev, seq, &key,
                                          NULL, 0, sig, sigSz, &validation);
    if (rc != TPM_RC_SUCCESS) {
        printf("VerifySeqComplete: 0x%x\n", rc);
        goto exit_dsa;
    }
    if (validation.tag != TPM_ST_MESSAGE_VERIFIED) {
        printf("ML-DSA verify tag 0x%x != TPM_ST_MESSAGE_VERIFIED\n",
                validation.tag);
        rc = -1;
        goto exit_dsa;
    }
    printf("ML-DSA-65 OK: sig=%d B, MESSAGE_VERIFIED ticket returned\n",
            sigSz);

exit_dsa:
    wolfTPM2_UnloadHandle(&dev, &key.handle);
    wolfTPM2_Cleanup(&dev);
    return (rc == TPM_RC_SUCCESS) ? 0 : -1;
}

int main(int argc, char** argv)
{
    int run_kem = 1;
    int run_dsa = 1;
    int fails = 0;
    int i;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-kem") == 0) {
            run_kem = 1; run_dsa = 0;
        }
        else if (strcmp(argv[i], "-dsa") == 0) {
            run_kem = 0; run_dsa = 1;
        }
        else if (strcmp(argv[i], "-h") == 0 ||
                  strcmp(argv[i], "--help") == 0) {
            printf("Usage: %s [-kem | -dsa]\n", argv[0]);
            printf("  default: run both ML-KEM-768 and ML-DSA-65\n");
            return 0;
        }
    }

    g_rpmsg_fd = rpmsg_open();
    if (g_rpmsg_fd < 0) {
        return 1;
    }

    if (run_kem && run_mlkem() != 0) fails++;
    if (run_dsa && run_mldsa() != 0) fails++;

    close(g_rpmsg_fd);
    if (fails != 0) {
        printf("%d PQC test(s) failed\n", fails);
        return 1;
    }
    printf("all PQC tests passed\n");
    return 0;
}
