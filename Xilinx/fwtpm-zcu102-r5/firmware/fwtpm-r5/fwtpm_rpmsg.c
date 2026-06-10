/* fwtpm_rpmsg.c
 *
 * OpenAMP RPMsg endpoint that bridges the Linux/APU TPM client to
 * FWTPM_ProcessCommand on the lock-step Cortex-R5. A single named
 * endpoint ("wolftpm") receives raw TPM2 command buffers and replies
 * with the response buffer.
 *
 * The libmetal + open-amp libraries do the heavy lifting for vring
 * management; this file only wires the receive callback into the
 * fwTPM dispatcher and provides the polling loop.
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

#include "user_settings.h"
#include "zcu102_r5.h"
#include "include/xparameters.h"

#include <wolftpm/fwtpm/fwtpm.h>
#include <wolftpm/fwtpm/fwtpm_command.h>

#include <metal/sys.h>
#include <metal/device.h>
#include <metal/io.h>
#include <metal/alloc.h>
#include <metal/log.h>
#include <openamp/open_amp.h>
#include <openamp/remoteproc.h>
#include <openamp/rpmsg.h>
#include <openamp/rpmsg_virtio.h>
#include <openamp/virtio.h>

#include <stdint.h>
#include <string.h>

/* Logging uses xil_printf() (declared by the BSP headers) -> outbyte()
 * -> remoteproc trace ring; hang-safe, unlike newlib printf(). */

#define RPMSG_EPT_NAME      "wolftpm"
/* g_rsp_buf is internal staging for FWTPM_ProcessCommand, which may
 * emit a few KiB for classical TPM responses (key blobs, capability
 * lists); it is NOT a claim about transport capacity. Sized with
 * headroom so the core cannot overflow it.
 *
 * TRANSPORT LIMIT: the OpenAMP rpmsg buffer pool is NUM_RPMSG_BUFFS
 * (256) descriptors of the default 512-byte frame (see rsc_table.c and
 * ZCU102_R5_BUF_SIZE in xparameters.h), so a single rpmsg_send carries
 * only ~496 bytes of payload. This transport sends one frame per
 * message -- it has no fragmentation/reassembly layer. A response that
 * does not fit one frame is rejected as a hard transport error (see
 * rpmsg_rx_cb) rather than silently truncated. PQC is gated off by
 * default for this reason (user_settings.h, FWTPM_ENABLE_PQC); enabling
 * end-to-end large payloads (e.g. ML-DSA) requires adding fragmentation
 * here and in the Linux client first. */
#define MAX_TPM_CMD_SIZE    4096
#define MAX_TPM_RSP_SIZE    4096

extern const struct remoteproc_ops zynqmp_r5_remoteproc_ops;     /* platform_info.c */
extern struct metal_io_region* zynqmp_r5_buf_io(void);     /* platform_info.c */
extern void* get_resource_table(int rsc_id, unsigned int* len);

static FWTPM_CTX*           g_fwtpm_ctx;
static struct rpmsg_endpoint g_ept;
static struct rpmsg_device*  g_rdev;
static struct remoteproc     g_rproc;
static struct rpmsg_virtio_device g_rvdev;
static struct virtio_device* g_vdev;

static uint8_t g_rsp_buf[MAX_TPM_RSP_SIZE];

/* ------------------------------------------------------------------ */
/* RPMsg receive callback                                             */
/* ------------------------------------------------------------------ */
/* Minimal TPM error responses: tag=TPM_ST_NO_SESSIONS, size=10. The
 * last 4 bytes are the response code. These let the client see a
 * structured failure rather than a transport timeout. */
static const uint8_t k_err_failure[] = {       /* TPM_RC_FAILURE 0x101 */
    0x80, 0x01, 0x00, 0x00, 0x00, 0x0A, 0x00, 0x00, 0x01, 0x01
};
static const uint8_t k_err_size[] = {          /* TPM_RC_SIZE 0x95 */
    0x80, 0x01, 0x00, 0x00, 0x00, 0x0A, 0x00, 0x00, 0x00, 0x95
};

static int rpmsg_rx_cb(struct rpmsg_endpoint* ept, void* data, size_t len,
                        uint32_t src, void* priv)
{
    int rc;
    int rspSize = (int)sizeof(g_rsp_buf);
    int maxPayload;

    (void)src;
    (void)priv;
    if (g_fwtpm_ctx == NULL) {
        return RPMSG_ERR_PERM;
    }
    if (len == 0U || len > MAX_TPM_CMD_SIZE) {
        /* Reply with a structured TPM_RC_SIZE rather than returning with
         * no response, so the Linux client sees a clean error instead of
         * blocking on a missing reply (looks like a transport hang). */
        return rpmsg_send(ept, k_err_size, sizeof(k_err_size));
    }

    rc = FWTPM_ProcessCommand(g_fwtpm_ctx, (const byte*)data, (int)len,
                               g_rsp_buf, &rspSize, /*locality*/0);
    if (rc != 0) {
        xil_printf("FWTPM_ProcessCommand: rc=%d\n", rc);
        return rpmsg_send(ept, k_err_failure, sizeof(k_err_failure));
    }

    /* This transport sends one rpmsg frame per response and has no
     * fragmentation layer. If the TPM response does not fit a single
     * frame, return a structured TPM_RC_SIZE error rather than letting
     * rpmsg_send truncate or fail silently. */
    maxPayload = rpmsg_virtio_get_buffer_size(ept->rdev);
    if (maxPayload > 0 && rspSize > maxPayload) {
        xil_printf("rpmsg: response %d exceeds frame payload %d\n",
               rspSize, maxPayload);
        return rpmsg_send(ept, k_err_size, sizeof(k_err_size));
    }

    return rpmsg_send(ept, g_rsp_buf, rspSize);
}

/* Linux-side endpoint deletion -- log only, fwTPM stays up. */
static void rpmsg_unbind_cb(struct rpmsg_endpoint* ept)
{
    (void)ept;
    xil_printf("rpmsg endpoint unbound (linux client closed)\n");
}

/* Master endpoint name match: bind every "wolftpm" channel announce. */
static int ns_bind_cb(struct rpmsg_device* rdev, const char* name,
                       uint32_t dest)
{
    (void)rdev;
    if (strcmp(name, RPMSG_EPT_NAME) != 0) {
        return RPMSG_ERR_PERM;
    }
    return rpmsg_create_ept(&g_ept, g_rdev, RPMSG_EPT_NAME, RPMSG_ADDR_ANY,
                             dest, rpmsg_rx_cb, rpmsg_unbind_cb);
}

/* ------------------------------------------------------------------ */
/* Init                                                               */
/* ------------------------------------------------------------------ */
extern void outbyte(char c);
static void rmsg_emit(const char *s) { while (*s) outbyte(*s++); }

int zynqmp_r5_rpmsg_init(FWTPM_CTX* ctx)
{
    int rc;
    void* rsc_table;
    unsigned int rsc_size;
    struct metal_init_params params = METAL_INIT_DEFAULTS;

    g_fwtpm_ctx = ctx;

    rmsg_emit("rpmsg: A metal_init\r\n");
    rc = metal_init(&params);
    rmsg_emit("rpmsg: B metal_init done\r\n");
    if (rc != 0) {
        xil_printf("metal_init failed: %d\n", rc);
        return rc;
    }

    /* Register the platform-specific remoteproc backend (IPI + carveouts). */
    rmsg_emit("rpmsg: C remoteproc_init\r\n");
    if (remoteproc_init(&g_rproc, &zynqmp_r5_remoteproc_ops, NULL) == NULL) {
        xil_printf("remoteproc_init failed\n");
        return -1;
    }
    rmsg_emit("rpmsg: D remoteproc_init done\r\n");

    /* Pull in the resource table baked into our ELF. The remoteproc
     * core fishes out the vdev/vring/buffer carveouts from this table. */
    rsc_table = get_resource_table(0, &rsc_size);

    /* Register the rsc_table region with remoteproc so its internal
     * get_io_with_va lookup can resolve the table virtual address.
     * Without this mmap, set_rsc_table returns -EINVAL. Mirrors the
     * canonical zynqmp_r5_a53 OpenAMP demo init sequence.
     *
     * Pass a non-NULL device-address pointer: zynqmp_r5_mmap rejects
     * da == NULL (see platform_info.c), so a NULL here silently fails
     * to register the region. The table is identity-mapped, so da = pa.
     * Fail init if the region was not registered. */
    {
        metal_phys_addr_t pa = (metal_phys_addr_t)(uintptr_t)rsc_table;
        metal_phys_addr_t da = pa;
        rmsg_emit("rpmsg: E0 mmap rsc_table\r\n");
        if (remoteproc_mmap(&g_rproc, &pa, &da, rsc_size,
                            NORM_NSHARED_NCACHE | PRIV_RW_USER_RW,
                            &g_rproc.rsc_io) == NULL) {
            xil_printf("rpmsg: rsc_table mmap failed\n");
            return -1;
        }
    }

    rmsg_emit("rpmsg: E set_rsc_table\r\n");
    rc = remoteproc_set_rsc_table(&g_rproc, rsc_table, rsc_size);
    rmsg_emit("rpmsg: F set_rsc_table done\r\n");
    if (rc != 0) {
        xil_printf("remoteproc_set_rsc_table: %d\n", rc);
        return rc;
    }

    rmsg_emit("rpmsg: G create_virtio\r\n");
    g_vdev = remoteproc_create_virtio(&g_rproc, /*vdev_index*/0,
                                       VIRTIO_DEV_DEVICE, NULL);
    rmsg_emit("rpmsg: H create_virtio done\r\n");
    if (g_vdev == NULL) {
        xil_printf("remoteproc_create_virtio failed\n");
        return -1;
    }

    rmsg_emit("rpmsg: I init_vdev\r\n");
    rc = rpmsg_init_vdev(&g_rvdev, g_vdev, ns_bind_cb,
                          zynqmp_r5_buf_io(), NULL);
    rmsg_emit("rpmsg: J init_vdev done\r\n");
    if (rc != 0) {
        xil_printf("rpmsg_init_vdev: %d\n", rc);
        return rc;
    }

    g_rdev = rpmsg_virtio_get_rpmsg_device(&g_rvdev);

    /* Proactively advertise the "wolftpm" endpoint so the Linux
     * client's RPMSG_CREATE_EPT_IOCTL can resolve a destination
     * address. Without this, the Linux client opens /dev/rpmsg<N>
     * with dst=ANY and the kernel can't route the first write. */
    rmsg_emit("rpmsg: K create_ept wolftpm\r\n");
    rc = rpmsg_create_ept(&g_ept, g_rdev, RPMSG_EPT_NAME,
                          RPMSG_ADDR_ANY, RPMSG_ADDR_ANY,
                          rpmsg_rx_cb, rpmsg_unbind_cb);
    rmsg_emit("rpmsg: L create_ept done\r\n");
    if (rc != 0) {
        xil_printf("rpmsg_create_ept: %d\r\n", rc);
        return rc;
    }
    return 0;
}

extern int platform_poll_ipi(void);
extern int platform_wait_ipi(void);

/* Legacy polled drain. Kept for debug builds that disable the GIC. */
int zynqmp_r5_rpmsg_poll(void)
{
    (void)platform_poll_ipi();
    return remoteproc_get_notification(&g_rproc, RSC_NOTIFY_ID_ANY);
}

/* IRQ-driven server loop. Blocks in WFI until an IPI ISR fires
 * (handled in platform_info.c::ipi_isr), then drains BOTH vrings via
 * RSC_NOTIFY_ID_ANY -- which walks every vring and pumps
 * virtqueue_notification(), delivering buffered avail entries up
 * through rpmsg into rpmsg_rx_cb. The ISR has already ACKed the IPI
 * source bit, so no platform_poll_ipi() call is needed here.
 *
 * Never returns under normal operation; only exits on a drain error. */
int zynqmp_r5_rpmsg_serve(void)
{
    int rc;
    for (;;) {
        platform_wait_ipi();
        rc = remoteproc_get_notification(&g_rproc, RSC_NOTIFY_ID_ANY);
        if (rc < 0) {
            return rc;
        }
    }
}
