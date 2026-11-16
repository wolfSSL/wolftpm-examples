/* tpm_io_mpfs_mbox.c
 *
 * wolfTPM IO callback for Linux on the PolarFire SoC U54 harts 1-3. Each TIS
 * register access is forwarded to the fwTPM on hart 4 through the shared-
 * memory mailbox described in ../fwtpm_tis_mpfs.h, mapped from /dev/mem.
 * Linked ahead of libwolftpm.a, it replaces the library's example HAL, so the
 * stock wolfTPM examples run unchanged. Needs root for /dev/mem.
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

#ifdef HAVE_CONFIG_H
    #include <config.h>
#endif

#include <wolftpm/tpm2.h>
#include <wolftpm/fwtpm/fwtpm_tis.h>
#include <hal/tpm_io.h>
#include "../fwtpm_tis_mpfs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/mman.h>
#include <sys/file.h>

#ifndef WOLFTPM_ADV_IO
    #error "tpm_io_mpfs_mbox.c needs wolfTPM built with --enable-advio"
#endif

/* Covers the mailbox, console ring and TIS register block (16 KB of LIM) */
#ifndef FWTPM_MPFS_MAP_SIZE
    #define FWTPM_MPFS_MAP_SIZE 0x4000
#endif
/* A register access that starts a command (TPM_STS.go) returns only after
 * hart 4 has executed it, so allow for slow commands such as key generation */
#ifndef FWTPM_MPFS_TIMEOUT_MS
    #define FWTPM_MPFS_TIMEOUT_MS 120000
#endif

static volatile FWTPM_MPFS_MAILBOX* gMbox = NULL;
static volatile FWTPM_TIS_REGS* gRegs = NULL;
static int gMemFd = -1;
static UINT32 gNonce = 0;
static int gMboxFailed = 0;
static pthread_mutex_t gMboxLock = PTHREAD_MUTEX_INITIALIZER;

static int MpfsMbox_Connect(void)
{
    int rc;
    const char* env;
    unsigned long base = FWTPM_TIS_SHM_BASE;
    void* map;

    /* DDR transport builds place the mailbox elsewhere */
    env = getenv("FWTPM_MPFS_BASE");
    if (env != NULL) {
        base = strtoul(env, NULL, 0);
    }

    gMemFd = open("/dev/mem", O_RDWR | O_SYNC | O_CLOEXEC);
    if (gMemFd < 0) {
        fprintf(stderr, "fwTPM mailbox: cannot open /dev/mem (run as root)\n");
        return TPM_RC_FAILURE;
    }
    map = mmap(NULL, FWTPM_MPFS_MAP_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED,
        gMemFd, (off_t)base);
    if (map == MAP_FAILED) {
        fprintf(stderr, "fwTPM mailbox: cannot map 0x%lx\n", base);
        close(gMemFd);
        gMemFd = -1;
        return TPM_RC_FAILURE;
    }

    gMbox = (volatile FWTPM_MPFS_MAILBOX*)map;
    gRegs = (volatile FWTPM_TIS_REGS*)((BYTE*)map + FWTPM_TIS_REGS_OFFSET);
    if (gMbox->magic != FWTPM_MBOX_MAGIC ||
            gMbox->version != FWTPM_MBOX_VERSION ||
            gRegs->magic != FWTPM_TIS_MAGIC ||
            gRegs->version != FWTPM_TIS_VERSION) {
        fprintf(stderr, "fwTPM mailbox: no fwTPM server at 0x%lx (for a DDR "
            "transport build set FWTPM_MPFS_BASE)\n", base);
        munmap(map, FWTPM_MPFS_MAP_SIZE);
        close(gMemFd);
        gMemFd = -1;
        gMbox = NULL;
        gRegs = NULL;
        return TPM_RC_FAILURE;
    }

    /* A TPM command spans many register accesses, so one client process
     * owns the mailbox until it exits; others wait here */
    do {
        rc = flock(gMemFd, LOCK_EX);
    } while (rc != 0 && errno == EINTR);
    if (rc != 0) {
        fprintf(stderr, "fwTPM mailbox: cannot lock /dev/mem\n");
        munmap(map, FWTPM_MPFS_MAP_SIZE);
        close(gMemFd);
        gMemFd = -1;
        gMbox = NULL;
        gRegs = NULL;
        return TPM_RC_FAILURE;
    }

    gNonce = (UINT32)time(NULL) ^ ((UINT32)getpid() << 16);
    return TPM_RC_SUCCESS;
}

/* Clear the shared data slot so command and response bytes do not linger */
static void MpfsMbox_ClearData(void)
{
    UINT16 i;

    for (i = 0; i < (UINT16)sizeof(gRegs->reg_data); i++) {
        gRegs->reg_data[i] = 0;
    }
}

static long MpfsMbox_ElapsedMs(const struct timespec* start)
{
    struct timespec now;

    clock_gettime(CLOCK_MONOTONIC, &now);
    return (long)(now.tv_sec - start->tv_sec) * 1000 +
        (now.tv_nsec - start->tv_nsec) / 1000000;
}

static int MpfsMbox_Access(INT32 isRead, UINT32 addr, BYTE* buf, UINT16 size)
{
    UINT16 i;
    UINT32 nonce;
    long elapsed;
    struct timespec start;
    struct timespec pause;

    gRegs->reg_addr = addr;
    gRegs->reg_len = size;
    gRegs->reg_is_write = (BYTE)(isRead ? 0 : 1);
    if (!isRead) {
        for (i = 0; i < size; i++) {
            gRegs->reg_data[i] = buf[i];
        }
    }
    gMbox->rsp_ready = 0;

    /* Nonzero, changing nonce; the server echoes it to prove it served this
     * request and not a stale one (see FWTPM_MPFS_MAILBOX) */
    gNonce += 2;
    nonce = gNonce | 1;
    if (nonce == 1) {
        nonce = 3;
    }

    /* The request must be visible before cmd_ready raises it */
    __sync_synchronize();
    gMbox->cmd_ready = nonce;
    __sync_synchronize();

    /* Register accesses complete in microseconds; only a command being
     * executed (TPM_STS.go) takes longer, so back off after 1 ms */
    pause.tv_sec = 0;
    pause.tv_nsec = 100000;
    clock_gettime(CLOCK_MONOTONIC, &start);
    while (gMbox->rsp_ready != 1) {
        elapsed = MpfsMbox_ElapsedMs(&start);
        if (elapsed > FWTPM_MPFS_TIMEOUT_MS) {
            fprintf(stderr, "fwTPM mailbox: no response from hart 4 "
                "(reg 0x%x)\n", (unsigned int)addr);
            /* Withdraw the request; hart 4 may still be reading reg_data, so
             * leave it, and send nothing more from this process */
            gMbox->cmd_ready = 0;
            __sync_synchronize();
            gMboxFailed = 1;
            return TPM_RC_FAILURE;
        }
        if (elapsed >= 1) {
            nanosleep(&pause, NULL);
        }
    }
    __sync_synchronize();

    if (gMbox->echo_nonce != nonce) {
        fprintf(stderr, "fwTPM mailbox: stale response (reg 0x%x)\n",
            (unsigned int)addr);
        gMbox->rsp_ready = 0;
        MpfsMbox_ClearData();
        return TPM_RC_FAILURE;
    }
    if (isRead) {
        for (i = 0; i < size; i++) {
            buf[i] = gRegs->reg_data[i];
        }
    }
    MpfsMbox_ClearData();
    gMbox->rsp_ready = 0;
    __sync_synchronize();

    return TPM_RC_SUCCESS;
}

int TPM2_IoCb(TPM2_CTX* ctx, INT32 isRead, UINT32 addr, BYTE* buf,
    UINT16 size, void* userCtx)
{
    int rc;

    (void)ctx;
    (void)userCtx;

    if (buf == NULL || size > (UINT16)sizeof(gRegs->reg_data)) {
        return BAD_FUNC_ARG;
    }

    /* One mailbox slot: threads serialize here, processes in Connect */
    if (pthread_mutex_lock(&gMboxLock) != 0) {
        return TPM_RC_FAILURE;
    }
    rc = TPM_RC_SUCCESS;
    if (gMboxFailed) {
        rc = TPM_RC_FAILURE;
    }
    else if (gMbox == NULL) {
        rc = MpfsMbox_Connect();
    }
    if (rc == TPM_RC_SUCCESS) {
        rc = MpfsMbox_Access(isRead, addr, buf, size);
    }
    (void)pthread_mutex_unlock(&gMboxLock);

    return rc;
}
