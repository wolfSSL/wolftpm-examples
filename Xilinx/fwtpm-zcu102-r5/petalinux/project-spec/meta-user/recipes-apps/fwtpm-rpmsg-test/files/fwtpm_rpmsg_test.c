/* fwtpm_rpmsg_test.c
 *
 * APU-side smoke test for the ZCU102 R5 fwTPM. Opens an rpmsg endpoint
 * named "wolftpm" via /dev/rpmsg_ctrl0, then issues TPM2_Startup,
 * TPM2_SelfTest, TPM2_GetRandom(32), and TPM2_GetCapability(props_fixed)
 * commands and validates the responses.
 *
 * Build (PetaLinux SDK or aarch64 cross):
 *   aarch64-linux-gnu-gcc -O2 -Wall -o fwtpm_rpmsg_test fwtpm_rpmsg_test.c
 *
 * Run (as root, after `echo start > /sys/class/remoteproc/.../state`):
 *   ./fwtpm_rpmsg_test
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <linux/rpmsg.h>

#define RPMSG_CTRL "/dev/rpmsg_ctrl0"
#define EPT_NAME   "wolftpm"

/* TPM2 commands (big-endian wire format). */
static const uint8_t cmd_startup[] = {
    0x80, 0x01,                   /* tag = TPM_ST_NO_SESSIONS */
    0x00, 0x00, 0x00, 0x0C,       /* size = 12 */
    0x00, 0x00, 0x01, 0x44,       /* CC = TPM_CC_Startup */
    0x00, 0x00,                   /* TPM_SU_CLEAR */
};

static const uint8_t cmd_selftest[] = {
    0x80, 0x01,                   /* tag */
    0x00, 0x00, 0x00, 0x0B,       /* size = 11 */
    0x00, 0x00, 0x01, 0x43,       /* CC = TPM_CC_SelfTest */
    0x00,                         /* fullTest = NO */
};

static const uint8_t cmd_getrand[] = {
    0x80, 0x01,                   /* tag */
    0x00, 0x00, 0x00, 0x0C,       /* size = 12 */
    0x00, 0x00, 0x01, 0x7B,       /* CC = TPM_CC_GetRandom */
    0x00, 0x20,                   /* bytes requested = 32 */
};

static const uint8_t cmd_getcap[] = {
    0x80, 0x01,                   /* tag */
    0x00, 0x00, 0x00, 0x16,       /* size = 22 */
    0x00, 0x00, 0x01, 0x7A,       /* CC = TPM_CC_GetCapability */
    0x00, 0x00, 0x00, 0x06,       /* TPM_CAP_TPM_PROPERTIES */
    0x00, 0x00, 0x01, 0x05,       /* property = TPM_PT_MANUFACTURER */
    0x00, 0x00, 0x00, 0x01,       /* count = 1 */
};

/* Query TPM_PT_ML_PARAMETER_SETS (PT_FIXED + 49 = 0x131). Per TCG
 * TPM 2.0 v1.85 Part 2 Sec.8.13 TPMA_ML_PARAMETER_SET, the response
 * value is a bitmap: bits 0-2 = MLKEM-512/768/1024,
 * bits 3-5 = MLDSA-44/65/87. All six bits set (0x3F) means the
 * firmware advertises full ML-KEM + ML-DSA support. */
static const uint8_t cmd_getcap_ml[] = {
    0x80, 0x01,
    0x00, 0x00, 0x00, 0x16,       /* size = 22 */
    0x00, 0x00, 0x01, 0x7A,       /* CC = TPM_CC_GetCapability */
    0x00, 0x00, 0x00, 0x06,       /* TPM_CAP_TPM_PROPERTIES */
    0x00, 0x00, 0x01, 0x31,       /* property = TPM_PT_ML_PARAMETER_SETS */
    0x00, 0x00, 0x00, 0x01,       /* count = 1 */
};

static int open_endpoint(void)
{
    int ctrl_fd = open(RPMSG_CTRL, O_RDWR);
    struct rpmsg_endpoint_info info;
    int rc;
    int ept_fd;
    char dev_path[64];
    int i;

    /* In-kernel rpmsg_chrdev only auto-probes channels named "rpmsg-raw",
     * so our "wolftpm" channel does not get a /dev/rpmsg<N> on its own
     * (the device shows up in /sys/bus/rpmsg/devices but has no driver
     * bound). Create a chrdev endpoint explicitly via the controller.
     *
     * RPMSG_CREATE_EPT_IOCTL creates a NEW eptdev whose chinfo.dst is
     * what the caller passes. Passing 0xFFFFFFFF leaves dst unresolved
     * and the first write returns "invalid addr (dst 0xffffffff)".
     * The R5 firmware's rpmsg_create_ept(src=RPMSG_ADDR_ANY,...) gets
     * the first reserved address (RPMSG_RESERVED_ADDRESSES = 1024 in
     * OpenAMP), and that is what NS_CREATE announces to Linux -- you
     * can verify in /sys/bus/rpmsg/devices/virtio0.wolftpm.-1.1024.
     * Hard-coding 1024 here works for a single-endpoint server; for
     * multi-endpoint setups parse the channel info from sysfs. */
    if (ctrl_fd < 0) {
        fprintf(stderr, "open %s: %s\n", RPMSG_CTRL, strerror(errno));
        return -1;
    }
    memset(&info, 0, sizeof(info));
    strncpy(info.name, EPT_NAME, sizeof(info.name) - 1);
    info.src = 0xFFFFFFFFu;
    info.dst = 1024u;                       /* R5 endpoint addr (0x400) */
    rc = ioctl(ctrl_fd, RPMSG_CREATE_EPT_IOCTL, &info);
    if (rc < 0) {
        fprintf(stderr, "RPMSG_CREATE_EPT_IOCTL: %s\n", strerror(errno));
        close(ctrl_fd);
        return -1;
    }
    /* udev creates /dev/rpmsg<N> for the new eptdev. Pick the first one
     * that opens (the only one for this single-channel server). */
    for (i = 0; i < 32; i++) {
        snprintf(dev_path, sizeof(dev_path), "/dev/rpmsg%d", i);
        ept_fd = open(dev_path, O_RDWR);
        if (ept_fd >= 0) {
            close(ctrl_fd);
            return ept_fd;
        }
    }
    fprintf(stderr, "no /dev/rpmsg<N> appeared after CREATE_EPT_IOCTL\n");
    close(ctrl_fd);
    return -1;
}

static int do_cmd(int fd, const char* name,
                   const uint8_t* cmd, size_t clen,
                   uint8_t* rsp, size_t rsp_cap)
{
    ssize_t n;
    uint16_t tag;
    uint32_t size, rc;

    n = write(fd, cmd, clen);
    if (n != (ssize_t)clen) {
        fprintf(stderr, "%s: write: %s\n", name, strerror(errno));
        return -1;
    }
    n = read(fd, rsp, rsp_cap);
    if (n < 10) {
        fprintf(stderr, "%s: short read %zd\n", name, n);
        return -1;
    }
    tag  = ((uint16_t)rsp[0] << 8) | rsp[1];
    size = ((uint32_t)rsp[2] << 24) | ((uint32_t)rsp[3] << 16) |
           ((uint32_t)rsp[4] << 8)  | rsp[5];
    rc   = ((uint32_t)rsp[6] << 24) | ((uint32_t)rsp[7] << 16) |
           ((uint32_t)rsp[8] << 8)  | rsp[9];
    printf("%-15s tag=0x%04x size=%u rc=0x%08x  %s\n",
            name, tag, size, rc, (rc == 0) ? "OK" : "FAIL");
    return (rc == 0) ? 0 : -1;
}

/* Run TPM2_GetCapability(PT_ML_PARAMETER_SETS) and decode the
 * returned bitmap. Returns 0 if at least one ML parameter set is
 * advertised, -1 on transport / RC / parse failure. */
static int do_cmd_getcap_ml(int fd, uint8_t* rsp, size_t rsp_cap)
{
    ssize_t n;
    uint32_t rc, count;
    uint32_t prop, val;

    n = write(fd, cmd_getcap_ml, sizeof(cmd_getcap_ml));
    if (n != (ssize_t)sizeof(cmd_getcap_ml)) {
        fprintf(stderr, "GetCap(ML): write: %s\n", strerror(errno));
        return -1;
    }
    n = read(fd, rsp, rsp_cap);
    /* TPMS_CAPABILITY_DATA response layout:
     *   header(10) | moreData(1) | cap(4) | count(4) |
     *   property(4) | value(4) = 27 bytes minimum for count >= 1. */
    if (n < 27) {
        fprintf(stderr, "GetCap(ML): short read %zd\n", n);
        return -1;
    }
    rc = ((uint32_t)rsp[6] << 24) | ((uint32_t)rsp[7] << 16) |
         ((uint32_t)rsp[8] << 8)  | rsp[9];
    if (rc != 0) {
        printf("%-15s rc=0x%08x  FAIL (firmware lacks WOLFTPM_V185?)\n",
               "GetCap(ML)", rc);
        return -1;
    }
    /* Skip moreData(1) + cap(4). count is at offset 15. */
    count = ((uint32_t)rsp[15] << 24) | ((uint32_t)rsp[16] << 16) |
            ((uint32_t)rsp[17] << 8)  | rsp[18];
    if (count < 1) {
        printf("%-15s count=0  FAIL (no ML property returned)\n",
               "GetCap(ML)");
        return -1;
    }
    prop = ((uint32_t)rsp[19] << 24) | ((uint32_t)rsp[20] << 16) |
           ((uint32_t)rsp[21] << 8)  | rsp[22];
    val  = ((uint32_t)rsp[23] << 24) | ((uint32_t)rsp[24] << 16) |
           ((uint32_t)rsp[25] << 8)  | rsp[26];
    printf("%-15s prop=0x%08x value=0x%08x  %s\n", "GetCap(ML)",
            prop, val,
            (val & 0x3FU) != 0U ? "OK" : "FAIL");
    if ((val & 0x07U) != 0U) {
        printf("                 ML-KEM: %s%s%s\n",
                (val & 0x01U) ? "512 "  : "",
                (val & 0x02U) ? "768 "  : "",
                (val & 0x04U) ? "1024 " : "");
    }
    if ((val & 0x38U) != 0U) {
        printf("                 ML-DSA: %s%s%s\n",
                (val & 0x08U) ? "44 " : "",
                (val & 0x10U) ? "65 " : "",
                (val & 0x20U) ? "87 " : "");
    }
    return ((val & 0x3FU) != 0U) ? 0 : -1;
}

int main(int argc, char** argv)
{
    int fd;
    uint8_t rsp[4096];
    int fails = 0;
    int run_pqc = 0;
    int total = 4;
    int i;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--pqc") == 0) {
            run_pqc = 1;
            total++;
        }
        else if (strcmp(argv[i], "-h") == 0 ||
                  strcmp(argv[i], "--help") == 0) {
            printf("Usage: %s [--pqc]\n", argv[0]);
            printf("  --pqc   Also probe TPM_PT_ML_PARAMETER_SETS to verify\n");
            printf("          firmware advertises ML-KEM / ML-DSA support.\n");
            return 0;
        }
    }

    fd = open_endpoint();
    if (fd < 0) {
        return 1;
    }

    if (do_cmd(fd, "Startup",         cmd_startup,  sizeof(cmd_startup),
                rsp, sizeof(rsp)) != 0) fails++;
    if (do_cmd(fd, "SelfTest",        cmd_selftest, sizeof(cmd_selftest),
                rsp, sizeof(rsp)) != 0) fails++;
    if (do_cmd(fd, "GetRandom(32)",   cmd_getrand,  sizeof(cmd_getrand),
                rsp, sizeof(rsp)) != 0) fails++;
    if (do_cmd(fd, "GetCapability",   cmd_getcap,   sizeof(cmd_getcap),
                rsp, sizeof(rsp)) != 0) fails++;

    if (run_pqc) {
        if (do_cmd_getcap_ml(fd, rsp, sizeof(rsp)) != 0) fails++;
    }

    close(fd);
    if (fails != 0) {
        printf("%d test(s) failed\n", fails);
        return 1;
    }
    printf("all %d tests passed\n", total);
    return 0;
}
