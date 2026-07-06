/* fwtpm_caps.c
 *
 * APU-side "hello world" for the ZCU102 R5 fwTPM: a simple
 * TPM2_GetCapability(TPM_CAP_TPM_PROPERTIES) that decodes and prints
 * the fixed TPM properties (family, spec level/revision, manufacturer,
 * vendor string, firmware version) -- the rpmsg-transport analogue of
 * wolfTPM's examples/wrap/caps.
 *
 * Opens the "wolftpm" rpmsg endpoint via /dev/rpmsg_ctrl0, issues
 * TPM2_Startup, then a single GetCapability for the PT_FIXED group and
 * walks the returned property list. The response is kept well under the
 * single-frame rpmsg payload (~496 bytes) by requesting a bounded count.
 *
 * Single source of truth: the PetaLinux recipe
 * (recipes-apps/fwtpm-caps/fwtpm-caps_0.1.0.bb) builds this exact file via
 * FILESEXTRAPATHS, so there is no separate copy to keep in sync.
 *
 * Build (PetaLinux SDK or aarch64 cross):
 *   aarch64-linux-gnu-gcc -O2 -Wall -o fwtpm_caps fwtpm_caps.c
 *
 * Run (as root, after `echo start > /sys/class/remoteproc/.../state`):
 *   ./fwtpm_caps
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

/* Capability selector echoed back in a GetCapability response. */
#define TPM_CAP_TPM_PROPERTIES  0x00000006u

/* TPM property tags (TCG TPM 2.0 Part 2, PT_FIXED group). */
#define PT_FAMILY_INDICATOR     0x00000100u
#define PT_LEVEL                0x00000101u
#define PT_REVISION             0x00000102u
#define PT_MANUFACTURER         0x00000105u
#define PT_VENDOR_STRING_1      0x00000106u
#define PT_VENDOR_STRING_2      0x00000107u
#define PT_VENDOR_STRING_3      0x00000108u
#define PT_VENDOR_STRING_4      0x00000109u
#define PT_VENDOR_TPM_TYPE      0x0000010Au
#define PT_FIRMWARE_VERSION_1   0x0000010Bu
#define PT_FIRMWARE_VERSION_2   0x0000010Cu

static const uint8_t cmd_startup[] = {
    0x80, 0x01,                   /* tag = TPM_ST_NO_SESSIONS */
    0x00, 0x00, 0x00, 0x0C,       /* size = 12 */
    0x00, 0x00, 0x01, 0x44,       /* CC = TPM_CC_Startup */
    0x00, 0x00,                   /* TPM_SU_CLEAR */
};

/* GetCapability(TPM_CAP_TPM_PROPERTIES, property=PT_FAMILY_INDICATOR,
 * count=0x20). 32 property/value pairs at 8 bytes each plus the 19-byte
 * preamble is ~275 bytes -- safely inside one rpmsg frame. */
static const uint8_t cmd_getcap_fixed[] = {
    0x80, 0x01,                   /* tag */
    0x00, 0x00, 0x00, 0x16,       /* size = 22 */
    0x00, 0x00, 0x01, 0x7A,       /* CC = TPM_CC_GetCapability */
    0x00, 0x00, 0x00, 0x06,       /* TPM_CAP_TPM_PROPERTIES */
    0x00, 0x00, 0x01, 0x00,       /* property = PT_FAMILY_INDICATOR */
    0x00, 0x00, 0x00, 0x20,       /* count = 32 */
};

static uint32_t be32(const uint8_t* p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8)  | (uint32_t)p[3];
}

/* Tear down an endpoint returned by open_endpoint(): destroy the rpmsg
 * endpoint first so repeated runs do not leak /dev/rpmsg<N> char devices,
 * then close the fd. Used on every exit path once the endpoint is open. */
static void close_endpoint(int fd)
{
    (void)ioctl(fd, RPMSG_DESTROY_EPT_IOCTL, 0);
    close(fd);
}

static int open_endpoint(void)
{
    int ctrl_fd;
    struct rpmsg_endpoint_info info;
    char dev_path[64];
    unsigned char existed[32];
    int rc;
    int ept_fd;
    int attempt;
    int i;

    ctrl_fd = open(RPMSG_CTRL, O_RDWR);
    if (ctrl_fd < 0) {
        fprintf(stderr, "open %s: %s\n", RPMSG_CTRL, strerror(errno));
        return -1;
    }
    /* Snapshot which /dev/rpmsg<N> nodes already exist so that after CREATE we
     * pick out the node this endpoint adds, rather than grabbing a pre-existing
     * endpoint that belongs to someone else. */
    for (i = 0; i < 32; i++) {
        snprintf(dev_path, sizeof(dev_path), "/dev/rpmsg%d", i);
        existed[i] = (access(dev_path, F_OK) == 0) ? 1 : 0;
    }
    /* The R5 server's endpoint takes the first OpenAMP reserved address
     * (1024 / 0x400); hard-code dst for this single-endpoint server. */
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
    /* The /dev/rpmsg<N> char node is created asynchronously (udev), so it may
     * not exist immediately. Poll for the newly appeared node, backing off
     * briefly between rounds (~20 x 50 ms = 1 s). */
    for (attempt = 0; attempt < 20; attempt++) {
        for (i = 0; i < 32; i++) {
            if (existed[i]) {
                continue;
            }
            snprintf(dev_path, sizeof(dev_path), "/dev/rpmsg%d", i);
            ept_fd = open(dev_path, O_RDWR);
            if (ept_fd >= 0) {
                close(ctrl_fd);
                return ept_fd;
            }
        }
        usleep(50000);
    }
    fprintf(stderr, "no new /dev/rpmsg<N> appeared after CREATE_EPT_IOCTL\n");
    /* Do not try to reclaim here: RPMSG_DESTROY_EPT_IOCTL acts on an opened
     * endpoint fd, and on this path we never opened ours. A snapshot-diff sweep
     * that destroys every node absent from the pre-CREATE snapshot could tear
     * down an endpoint another process created concurrently, so we accept a
     * possible endpoint leak in this abnormal case rather than destroy a node
     * we cannot positively attribute to ourselves. */
    close(ctrl_fd);
    return -1;
}

/* Append a 32-bit property value as up to 4 printable ASCII bytes to
 * buf (NUL-terminated). Non-printable bytes stop the copy. */
static void append_ascii(char* buf, size_t cap, uint32_t val)
{
    uint8_t b[4];
    size_t len = strlen(buf);
    int i;

    b[0] = (uint8_t)(val >> 24);
    b[1] = (uint8_t)(val >> 16);
    b[2] = (uint8_t)(val >> 8);
    b[3] = (uint8_t)(val);
    for (i = 0; i < 4 && len + 1 < cap; i++) {
        if (b[i] < 0x20 || b[i] >= 0x7F) {
            break;   /* stop at first non-printable byte (incl. NUL) */
        }
        buf[len++] = (char)b[i];
    }
    buf[len] = '\0';
}

int main(void)
{
    int fd;
    uint8_t rsp[1024];
    ssize_t n;
    uint32_t rc, count, off, cap;
    uint32_t i;
    uint32_t family = 0, level = 0, revision = 0;
    uint32_t manuf = 0, vtype = 0, fw1 = 0, fw2 = 0;
    char mfg_ascii[8];
    char vendor[32];
    char fam[8];
    uint32_t prop, val;

    memset(mfg_ascii, 0, sizeof(mfg_ascii));
    memset(vendor, 0, sizeof(vendor));
    memset(fam, 0, sizeof(fam));

    fd = open_endpoint();
    if (fd < 0) {
        return 1;
    }

    /* TPM must be started before it answers capability queries. */
    n = write(fd, cmd_startup, sizeof(cmd_startup));
    if (n < 0) {
        fprintf(stderr, "Startup: write: %s\n", strerror(errno));
        close_endpoint(fd);
        return 1;
    }
    if (n != (ssize_t)sizeof(cmd_startup)) {
        fprintf(stderr, "Startup: short write %zd/%zu\n", n,
                sizeof(cmd_startup));
        close_endpoint(fd);
        return 1;
    }
    n = read(fd, rsp, sizeof(rsp));
    if (n < 0) {
        fprintf(stderr, "Startup: read: %s\n", strerror(errno));
        close_endpoint(fd);
        return 1;
    }
    if (n < 10) {
        fprintf(stderr, "Startup: short read %zd\n", n);
        close_endpoint(fd);
        return 1;
    }
    /* TPM_RC_INITIALIZE (0x100) means already started -- not an error. */
    rc = be32(rsp + 6);
    if (rc != 0 && rc != 0x00000100u) {
        fprintf(stderr, "Startup: rc=0x%08x\n", rc);
        close_endpoint(fd);
        return 1;
    }

    /* GetCapability(PT_FIXED). */
    n = write(fd, cmd_getcap_fixed, sizeof(cmd_getcap_fixed));
    if (n < 0) {
        fprintf(stderr, "GetCapability: write: %s\n", strerror(errno));
        close_endpoint(fd);
        return 1;
    }
    if (n != (ssize_t)sizeof(cmd_getcap_fixed)) {
        fprintf(stderr, "GetCapability: short write %zd/%zu\n", n,
                sizeof(cmd_getcap_fixed));
        close_endpoint(fd);
        return 1;
    }
    n = read(fd, rsp, sizeof(rsp));
    if (n < 0) {
        fprintf(stderr, "GetCapability: read: %s\n", strerror(errno));
        close_endpoint(fd);
        return 1;
    }
    if (n < 19) {
        fprintf(stderr, "GetCapability: short read %zd\n", n);
        close_endpoint(fd);
        return 1;
    }
    rc = be32(rsp + 6);
    if (rc != 0) {
        fprintf(stderr, "GetCapability: rc=0x%08x\n", rc);
        close_endpoint(fd);
        return 1;
    }

    /* Response: header(10) moreData(1) capability(4) count(4) then
     * count * (property(4) value(4)) pairs starting at offset 19. */
    cap   = be32(rsp + 11);
    count = be32(rsp + 15);
    /* Reject a malformed or truncated property list: wrong capability, an
     * empty list, or a count whose 8-byte pairs do not fit in the bytes
     * actually returned. A single-frame reply that dropped later properties
     * must fail here rather than print "OK" from the default zero fields.
     * n >= 19 is guaranteed by the short-read check above, so (n - 19) does
     * not underflow. */
    if (cap != TPM_CAP_TPM_PROPERTIES || count == 0 ||
        count > ((uint32_t)n - 19u) / 8u) {
        fprintf(stderr,
                "GetCapability: malformed response (cap=0x%08x count=%u "
                "len=%zd)\n", cap, count, n);
        close_endpoint(fd);
        return 1;
    }
    off = 19;
    for (i = 0; i < count; i++) {
        if (off + 8 > (uint32_t)n) {
            break;     /* truncated to one frame */
        }
        prop = be32(rsp + off);
        val  = be32(rsp + off + 4);
        off += 8;
        switch (prop) {
            case PT_FAMILY_INDICATOR:   family = val;   break;
            case PT_LEVEL:              level = val;     break;
            case PT_REVISION:           revision = val;  break;
            case PT_MANUFACTURER:       manuf = val;     break;
            case PT_VENDOR_STRING_1:
            case PT_VENDOR_STRING_2:
            case PT_VENDOR_STRING_3:
            case PT_VENDOR_STRING_4:
                append_ascii(vendor, sizeof(vendor), val);
                break;
            case PT_VENDOR_TPM_TYPE:    vtype = val;     break;
            case PT_FIRMWARE_VERSION_1: fw1 = val;       break;
            case PT_FIRMWARE_VERSION_2: fw2 = val;       break;
            default:                                     break;
        }
    }

    append_ascii(fam, sizeof(fam), family);   /* indicator is ASCII "2.0" */
    append_ascii(mfg_ascii, sizeof(mfg_ascii), manuf);

    printf("\n");
    printf("wolfTPM fwTPM capabilities (ZCU102 R5, via OpenAMP rpmsg)\n");
    printf("---------------------------------------------------------\n");
    printf("  Family:        %s\n", fam[0] ? fam : "(none)");
    printf("  Spec Level:    %u\n", level);
    printf("  Spec Revision: %u.%02u\n", revision / 100u, revision % 100u);
    printf("  Manufacturer:  0x%08x  \"%s\"\n", manuf, mfg_ascii);
    printf("  Vendor String: \"%s\"\n", vendor);
    printf("  Vendor Type:   0x%08x\n", vtype);
    printf("  Firmware Ver:  0x%08x 0x%08x\n", fw1, fw2);
    printf("---------------------------------------------------------\n");
    printf("GetCapability OK (rc=0x00000000)\n\n");

    close_endpoint(fd);
    return 0;
}
