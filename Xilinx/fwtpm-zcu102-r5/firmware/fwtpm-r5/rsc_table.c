/* rsc_table.c
 *
 * remoteproc resource table for the ZCU102 R5 fwTPM firmware. The Linux
 * remoteproc core finds this table by section name (.resource_table)
 * when loading the ELF, then uses it to set up the OpenAMP virtio
 * device (one rpmsg vdev with two vrings + one buffer carveout) and
 * an optional trace ring.
 *
 * Carveout addresses match the reserved-memory nodes in the PetaLinux
 * system-user.dtsi (rproc_0_reserved + vdev0vring0/1 + vdev0buffer).
 *
 * Copyright (C) 2006-2026 wolfSSL Inc.
 */

#include "include/xparameters.h"

#include <openamp/open_amp.h>

#include <stdint.h>

#define NUM_VRINGS              2
#define VRING_ALIGN             4096
#define NUM_RPMSG_BUFFS         256
#define VDEV_FEATURES           (1U << VIRTIO_RPMSG_F_NS)

#define NUM_RESOURCE_ENTRIES    2
#define TRACE_BUFFER_SIZE       0x8000

struct fwtpm_rsc_table {
    uint32_t version;
    uint32_t num;
    uint32_t reserved[2];
    uint32_t offset[NUM_RESOURCE_ENTRIES];

    /* rpmsg vdev */
    struct fw_rsc_vdev rpmsg_vdev;
    struct fw_rsc_vdev_vring rpmsg_vring0;
    struct fw_rsc_vdev_vring rpmsg_vring1;

    /* Trace buffer (optional debug ring) */
    struct fw_rsc_trace trace;
};

/* Exported -- bsp_stubs.c writes outbyte() output here so the same
 * memory backs both the rsc_table trace resource and stdout. */
char trace_buffer[TRACE_BUFFER_SIZE]
    __attribute__((section(".trace_buffer"), used, aligned(8)));

const struct fwtpm_rsc_table __resource_table
    __attribute__((section(".resource_table"))) = {
    /* version */ 1,
    /* num */     NUM_RESOURCE_ENTRIES,
    /* reserved */ {0, 0},
    /* offset */ {
        offsetof(struct fwtpm_rsc_table, rpmsg_vdev),
        offsetof(struct fwtpm_rsc_table, trace),
    },

    /* rpmsg vdev */
    {
        RSC_VDEV, VIRTIO_ID_RPMSG, 0, VDEV_FEATURES, 0, 0, 0,
        NUM_VRINGS, {0, 0},
    },
    /* vring 0 (TX): R5 -> Linux */
    {
        ZCU102_R5_VRING0_BASE, VRING_ALIGN, NUM_RPMSG_BUFFS, 1, 0,
    },
    /* vring 1 (RX): Linux -> R5 */
    {
        ZCU102_R5_VRING1_BASE, VRING_ALIGN, NUM_RPMSG_BUFFS, 2, 0,
    },

    /* Trace ring (visible from /sys/kernel/debug/remoteproc/.../trace0) */
    {
        RSC_TRACE, (uint32_t)trace_buffer, TRACE_BUFFER_SIZE, 0,
        "trace:fwtpm",
    },
};

void* get_resource_table(int rsc_id, unsigned int* len)
{
    (void)rsc_id;
    if (len != NULL) {
        *len = sizeof(__resource_table);
    }
    return (void*)&__resource_table;
}
