/* zcu102_r5.h
 *
 * Public interface for the ZCU102 R5 fwTPM HAL helpers.
 *
 * Copyright (C) 2006-2026 wolfSSL Inc.
 */

#ifndef ZCU102_R5_H
#define ZCU102_R5_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* fwtpm_clock_zynqmp.c -- FWTPM_CLOCK_HAL get_ms() and RNG seed cb */
struct FWTPM_CTX;
int      zynqmp_r5_clock_init(struct FWTPM_CTX* ctx);
uint64_t zynqmp_r5_get_ms(void* halCtx);
int      zynqmp_r5_rng_seed_cb(unsigned char* output, unsigned int sz);

/* fwtpm_nv_qspi.c -- FWTPM_NV_HAL backed by XQspiPsu */
struct FWTPM_NV_HAL_S;
int  zynqmp_r5_nv_qspi_init(struct FWTPM_NV_HAL_S* hal);
int  zynqmp_r5_nv_ram_init(struct FWTPM_NV_HAL_S* hal);  /* V1 fallback (volatile) */

/* fwtpm_rpmsg.c -- OpenAMP rpmsg endpoint -> FWTPM_ProcessCommand */
int  zynqmp_r5_rpmsg_init(struct FWTPM_CTX* ctx);
int  zynqmp_r5_rpmsg_poll(void);   /* polling form (legacy) */
int  zynqmp_r5_rpmsg_serve(void);  /* IRQ-driven server loop */

/* platform_info.c -- IPI / GIC */
int  gic_init(void);                /* XScuGic + IPI ISR setup */
int  platform_poll_ipi(void);       /* legacy: drain via polling */
int  platform_wait_ipi(void);       /* WFI until IPI ISR fires */

/* pmu_eemi.c -- EEMI client to PMUFW (PM_REQUEST_NODE / PM_RELEASE_NODE).
 * Used to bring up devices Linux DTS marks status="disabled" so the
 * R5 owns them outright (e.g. NODE_QSPI=0x2D for fwtpm_nv_qspi.c). */
#define PMU_NODE_QSPI    0x2DU
int  pmu_eemi_init(void);
int  pmu_request_node(unsigned int node_id);
int  pmu_release_node(unsigned int node_id);

/* Resource table accessor (rsc_table.c) */
void* get_resource_table(int rsc_id, unsigned int* len);

#ifdef __cplusplus
}
#endif

#endif /* ZCU102_R5_H */
