/* platform_info.c
 *
 * ZCU102 OpenAMP platform-specific glue: IPI doorbell setup and
 * libmetal device registration for the rpmsg buffer / vring carveouts.
 *
 * The values mirror the stock Xilinx zynqmp_a53_r5_proc_common demo:
 * - IPI base 0xFF310000 (RPU0 channel)
 * - SPI 65 (GIC SPI 33)
 * - vring + buffer carveout in DDR (see xparameters.h)
 *
 * Copyright (C) 2006-2026 wolfSSL Inc.
 */

#include "include/xparameters.h"

#include <metal/sys.h>
#include <metal/device.h>
#include <metal/io.h>
#include <metal/alloc.h>
#include <metal/log.h>
#include <metal/utilities.h>
#include <openamp/remoteproc.h>
#include <openamp/rpmsg_virtio.h>

#include <xscugic.h>

#include <stdint.h>
#include <string.h>

/* Logging uses xil_printf() (declared by the BSP headers) -> outbyte()
 * -> remoteproc trace ring; hang-safe, unlike newlib printf(). */

/* ------------------------------------------------------------------ */
/* IPI base (write IPI_TRIG to ring the APU doorbell, read IPI_ISR    */
/* to ack incoming).                                                  */
/* ------------------------------------------------------------------ */
#define IPI_TRIG_OFFSET   0x0U
#define IPI_OBS_OFFSET    0x4U
#define IPI_ISR_OFFSET    0x10U
#define IPI_IMR_OFFSET    0x14U
#define IPI_IER_OFFSET    0x18U
#define IPI_IDR_OFFSET    0x1CU

#define IPI_BASE     XPAR_PSU_IPI_1_BASE_ADDRESS
#define IPI_BIT_MASK XPAR_PSU_IPI_1_BIT_MASK

/* page_shift = -1UL signals "one giant page" so physmap[0] is the only
 * entry the libmetal lookup ever indexes. We were previously setting
 * page_shift=12 + page_mask=-1, which is internally inconsistent: the
 * compiled metal_io_phys lookup in virtqueue_get_buffer_addr divides
 * the offset by page_shift (1<<12 = 4096) and indexes physmap[offset/4K]
 * -- but our physmap array has only one entry, so we read garbage past
 * index 0 and the lookup loops forever. The contiguous-region idiom in
 * libmetal is page_shift=-1 + page_mask=-1, which steers the lookup
 * down the physmap[0]+offset shortcut. */
#define CONTIG_PAGE_SHIFT  ((unsigned long)-1)
#define CONTIG_PAGE_MASK   ((metal_phys_addr_t)-1)

static struct metal_device g_vring_device = {
    .name = "vring",
    .num_regions = 1,
    .regions = {
        {
            .virt = (void*)ZCU102_R5_VRING0_BASE,
            .physmap = (metal_phys_addr_t[]){ZCU102_R5_VRING0_BASE},
            .size = ZCU102_R5_VRING0_SIZE + ZCU102_R5_VRING1_SIZE,
            .page_shift = CONTIG_PAGE_SHIFT,
            .page_mask = CONTIG_PAGE_MASK,
            .mem_flags = 0,
            .ops = {NULL},
        },
    },
    .node = {NULL},
    .irq_num = 0,
    .irq_info = NULL,
};

static struct metal_device g_buf_device = {
    .name = "vrbuf",
    .num_regions = 1,
    .regions = {
        {
            .virt = (void*)ZCU102_R5_BUF_BASE,
            .physmap = (metal_phys_addr_t[]){ZCU102_R5_BUF_BASE},
            .size = ZCU102_R5_BUF_SIZE,
            .page_shift = CONTIG_PAGE_SHIFT,
            .page_mask = CONTIG_PAGE_MASK,
            .mem_flags = 0,
            .ops = {NULL},
        },
    },
    .node = {NULL},
    .irq_num = 0,
    .irq_info = NULL,
};

/* ------------------------------------------------------------------ */
/* IPI helpers                                                        */
/* ------------------------------------------------------------------ */
static inline void ipi_write(uint32_t off, uint32_t v)
{
    *(volatile uint32_t*)(IPI_BASE + off) = v;
}

static inline uint32_t ipi_read(uint32_t off)
{
    return *(volatile uint32_t*)(IPI_BASE + off);
}

static int ipi_kick(struct remoteproc* rproc, uint32_t id)
{
    (void)rproc;
    (void)id;
    ipi_write(IPI_TRIG_OFFSET, IPI_BIT_MASK);
    return 0;
}

static struct remoteproc* zynqmp_r5_init(struct remoteproc* rproc,
                                          const struct remoteproc_ops* ops,
                                          void* arg)
{
    (void)arg;
    (void)ops;
    if (rproc == NULL) {
        return NULL;
    }
    /* DO NOT memset(rproc) here -- the caller (remoteproc_init) has already
     * memset'd and called metal_list_init(&rproc->mems / &rproc->vdevs).
     * A second memset would clobber those list heads to NULL, after which
     * any metal_list_for_each walk dereferences NULL and the next
     * remoteproc_set_rsc_table hangs inside remoteproc_get_io_with_va. */

    /* Enable IPI from APU (clear any pending; unmask). */
    ipi_write(IPI_ISR_OFFSET, IPI_BIT_MASK);
    ipi_write(IPI_IER_OFFSET, IPI_BIT_MASK);

    /* Register libmetal devices for the vring + buffer carveouts. */
    metal_register_generic_device(&g_vring_device);
    metal_register_generic_device(&g_buf_device);
    return rproc;
}

static void zynqmp_r5_remove(struct remoteproc* rproc)
{
    (void)rproc;
    ipi_write(IPI_IDR_OFFSET, IPI_BIT_MASK);
}

/* Register a memory region with remoteproc so subsequent set_rsc_table /
 * get_io_with_va calls can find it. Mirrors the canonical
 * zynqmp_r5_a53_proc_mmap shipped with OpenAMP. */
static void* zynqmp_r5_mmap(struct remoteproc* rproc,
                             metal_phys_addr_t* pa, metal_phys_addr_t* da,
                             size_t size, unsigned int attribute,
                             struct metal_io_region** io)
{
    struct remoteproc_mem* mem;
    struct metal_io_region* tmpio;
    metal_phys_addr_t lpa, lda;

    if (pa == NULL || da == NULL || size == 0U) {
        return NULL;
    }
    lpa = *pa;
    lda = *da;
    if (lpa == METAL_BAD_PHYS && lda == METAL_BAD_PHYS) {
        return NULL;
    }
    if (lpa == METAL_BAD_PHYS) {
        lpa = lda;
    }
    if (lda == METAL_BAD_PHYS) {
        lda = lpa;
    }
    if (attribute == 0U) {
        attribute = NORM_SHARED_NCACHE | PRIV_RW_USER_RW;
    }

    mem = metal_allocate_memory(sizeof(*mem));
    if (mem == NULL) {
        return NULL;
    }
    tmpio = metal_allocate_memory(sizeof(*tmpio));
    if (tmpio == NULL) {
        metal_free_memory(mem);
        return NULL;
    }
    remoteproc_init_mem(mem, NULL, lpa, lda, size, tmpio);
    /* mem_flags=0 (not 'attribute'): libmetal's generic backend on
     * Xilinx routes a non-zero flags through Xil_MemMap ->
     * Xil_SetMPURegion, which would consume a fresh MPU region and
     * (if Mpu_Config wasn't synced with the hardware) silently
     * overwrite region 0/1 -- exactly what mpu_setup.c just
     * configured. With flags=0 Xil_MemMap returns the physical
     * address as-is. We've already mapped all DDR as Normal-NC in
     * region 0, so there is no per-rsc-table mapping work to do. */
    metal_io_init(tmpio, (void*)(uintptr_t)lpa, &mem->pa, size,
                   sizeof(metal_phys_addr_t) << 3, 0U, NULL);
    (void)attribute;
    remoteproc_add_mem(rproc, mem);
    *pa = lpa;
    *da = lda;
    if (io != NULL) {
        *io = tmpio;
    }
    return metal_io_phys_to_virt(tmpio, mem->pa);
}

static int zynqmp_r5_notify(struct remoteproc* rproc, uint32_t id)
{
    return ipi_kick(rproc, id);
}

const struct remoteproc_ops zynqmp_r5_remoteproc_ops = {
    .init    = zynqmp_r5_init,
    .remove  = zynqmp_r5_remove,
    .mmap    = zynqmp_r5_mmap,
    .notify  = zynqmp_r5_notify,
};

/* Open the buffer-pool device and return its first IO region. The
 * rpmsg vdev uses this region to map shared payload buffers between
 * R5 and APU. */
struct metal_io_region* zynqmp_r5_buf_io(void)
{
    static struct metal_io_region* s_io;
    static struct metal_device*    s_dev;
    int rc;

    if (s_io != NULL) {
        return s_io;
    }
    rc = metal_device_open("generic", "vrbuf", &s_dev);
    if (rc != 0 || s_dev == NULL) {
        return NULL;
    }
    s_io = metal_device_io_region(s_dev, 0);
    return s_io;
}

/* Called from rpmsg_poll to drain pending IPI events. */
int platform_poll_ipi(void)
{
    uint32_t isr = ipi_read(IPI_ISR_OFFSET);
    if ((isr & IPI_BIT_MASK) != 0U) {
        ipi_write(IPI_ISR_OFFSET, IPI_BIT_MASK);
        return 1;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* IRQ-driven IPI                                                     */
/* ------------------------------------------------------------------ */
/* GIC instance and IPI pending flag. The flag is set from ISR context
 * and consumed by the rpmsg server's wait loop. volatile is enough --
 * R5 has a single core in lock-step, and the wait loop reads it with
 * IRQs masked between WFI wake and clear. */
static XScuGic     g_gic;
static volatile int g_ipi_pending;
static int          g_gic_ready;

/* ISR: ACK the IPI source bit, set pending flag. Keep work minimal --
 * the heavy lifting (vring drain) happens in thread context. */
static void ipi_isr(void* arg)
{
    uint32_t isr;
    (void)arg;
    isr = ipi_read(IPI_ISR_OFFSET);
    if ((isr & IPI_BIT_MASK) != 0U) {
        ipi_write(IPI_ISR_OFFSET, IPI_BIT_MASK);
        g_ipi_pending = 1;
    }
}

/* GIC + IPI interrupt setup. Call once after mpu_init() and before the
 * rpmsg server loop. Leaves IRQs enabled at the CPSR level on return. */
int gic_init(void)
{
    XScuGic_Config* cfg;
    int rc;

    cfg = XScuGic_LookupConfig(XPAR_SCUGIC_0_DIST_BASEADDR);
    if (cfg == NULL) {
        xil_printf("XScuGic_LookupConfig failed\n");
        return -1;
    }
    rc = XScuGic_CfgInitialize(&g_gic, cfg, cfg->CpuBaseAddress);
    if (rc != XST_SUCCESS) {
        xil_printf("XScuGic_CfgInitialize: %d\n", rc);
        return rc;
    }

    /* Level-triggered, priority 0xA0. */
    XScuGic_SetPriorityTriggerType(&g_gic, XPAR_PSU_IPI_1_INT_ID,
                                    0xA0U, 0x1U);
    rc = XScuGic_Connect(&g_gic, XPAR_PSU_IPI_1_INT_ID,
                          (Xil_InterruptHandler)ipi_isr, NULL);
    if (rc != XST_SUCCESS) {
        xil_printf("XScuGic_Connect: %d\n", rc);
        return rc;
    }
    XScuGic_Enable(&g_gic, XPAR_PSU_IPI_1_INT_ID);

    /* Unmask the IPI source (also done in remoteproc init -- harmless
     * to re-enable here so the ISR works even before remoteproc_init). */
    ipi_write(IPI_ISR_OFFSET, IPI_BIT_MASK);
    ipi_write(IPI_IER_OFFSET, IPI_BIT_MASK);

    g_gic_ready = 1;

    /* Enable IRQs in CPSR (clear I bit). FIQ stays masked. */
    __asm__ volatile("cpsie i" ::: "memory");
    return 0;
}

/* Called from boot.S _irq trampoline. Dispatches to the GIC handler
 * which in turn calls our registered ipi_isr. Wrapped here so boot.S
 * does not need to know about &g_gic. */
void _irq_dispatch(void)
{
    if (g_gic_ready) {
        XScuGic_InterruptHandler(&g_gic);
    }
}

/* Block until an IPI ISR posts. Returns 1 when an IPI was handled.
 *
 * The flag check and the wfi sleep must be atomic with respect to
 * ipi_isr. If we tested g_ipi_pending with IRQs enabled and the IPI
 * fired between the test and the wfi, the ISR would set the flag and
 * consume the interrupt, leaving wfi to sleep with nothing left to
 * wake it -- the server would stall until the next IPI. We therefore
 * mask IRQs around the flag test: a wfi executed with IRQs masked
 * still wakes on a pending interrupt (it just does not take the
 * exception), so an IPI posted during the window cannot be lost. We
 * briefly unmask after each wfi so the pending ipi_isr can run and
 * set the flag, then re-mask to re-test. */
int platform_wait_ipi(void)
{
    __asm__ volatile("cpsid i" ::: "memory");       /* mask IRQ */
    while (g_ipi_pending == 0) {
        __asm__ volatile("wfi" ::: "memory");       /* wakes on pending IRQ even if masked */
        __asm__ volatile("cpsie i" ::: "memory");   /* let the pending ipi_isr run */
        __asm__ volatile("cpsid i" ::: "memory");   /* re-mask, re-test the flag */
    }
    g_ipi_pending = 0;
    __asm__ volatile("cpsie i" ::: "memory");        /* restore IRQ enable */
    return 1;
}
