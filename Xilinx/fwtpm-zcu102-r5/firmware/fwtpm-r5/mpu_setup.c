/* mpu_setup.c
 *
 * Minimal ARMv7-R MPU programmer for the fwTPM R5 firmware loaded by
 * Linux remoteproc. Programs the smallest workable region map so that
 * libmetal's LDREX/STREX-based atomics work on the rproc vring memory.
 *
 * Two cache policies, selected at build time:
 *
 *   Default (FWTPM_DCACHE=1, I-cache + D-cache):
 *     Region 0: 0x00000000 + 2 GiB, Normal write-back cacheable, RW.
 *     Region 1: 0xF0000000 + 256 MiB, Device, RW (GIC/IPI/UART/QSPI/...).
 *     Region 2: __shared_nc_start + 64 KiB, Normal non-cacheable -- the
 *               grouped rsc_table + trace_buffer window (lscript-dcache.ld).
 *     Region 3: 0x3EE00000 + 512 KiB, Normal non-cacheable -- the vring
 *               and rpmsg buffer pool (APU-shared virtio rings).
 *     SCTLR.I=1, SCTLR.C=1. Code and R5-private data are cached (MemUse
 *     seeding / FWTPM_Init ~3 s), while the two NC regions keep the trace
 *     ring and virtio handshake coherent. Hardware-validated.
 *
 *   FWTPM_DCACHE=0 (I-cache only, conservative fallback):
 *     Regions 0/1 only. SCTLR.I=1, SCTLR.C=0. Instruction fetch is cached
 *     (fast SHA-3 for MemUse entropy); ALL data accesses bypass the cache,
 *     so the APU-shared trace_buffer, vrings and resource table stay
 *     coherent without flushes, at the cost of ~30 s seeding.
 *
 * Notes
 *   - Bypasses Xilinx Mpu_Config bookkeeping (which lives in a
 *     .bootdata section our custom linker script does not allocate).
 *     Programs CP15 directly via the same MCR encodings used by
 *     libxilstandalone's xil_mpu_r5.c.
 *   - Called from main() before the first emit() so any fault is
 *     observable as silence in trace0 vs. a "MPU ready" breadcrumb.
 *
 * Copyright (C) 2006-2026 wolfSSL Inc.
 */

#include <stdint.h>

/* CP15 MCR/MRC pseudo-asm. xpseudo_asm.h depends on a generated BSP
 * include path; we open-code the ops to keep this file Tier-1 buildable. */
#define MCR(coproc, op1, val, crn, crm, op2) \
    __asm__ volatile("mcr " #coproc ", " #op1 ", %0, " #crn ", " #crm ", " #op2 \
                     :: "r" (val))

#define MRC(coproc, op1, valvar, crn, crm, op2) \
    __asm__ volatile("mrc " #coproc ", " #op1 ", %0, " #crn ", " #crm ", " #op2 \
                     : "=r" (valvar))

#define DSB() __asm__ volatile("dsb" ::: "memory")
#define ISB() __asm__ volatile("isb" ::: "memory")

/* Region size encodings (value placed in SIZE_EN, pre-shift). */
#define REGION_ENC_64KB    0x0FU  /* log2(64 KiB) - 1 */
#define REGION_ENC_512KB   0x12U  /* log2(512 KiB) - 1 */
#define REGION_ENC_256MB   0x1BU  /* log2(256 MiB) - 1 */
#define REGION_ENC_2GB     0x1EU  /* log2(2 GiB) - 1 */
#define REGION_EN_BIT      0x1U

/* Attributes (TEX/C/B/S + access permissions), matching xreg_cortexr5.h. */
#define ATTR_NORM_NSHARED_NCACHE    0x0008U  /* Normal, Outer/Inner non-cache */
#define ATTR_NORM_NSHARED_WB_WA     0x000BU  /* Normal, write-back write-alloc */
#define ATTR_DEVICE_NONSHARED       0x0010U  /* Device, non-shared            */
#define ATTR_AP_PRIV_RW_USER_RW     (0x3U << 8) /* full access, all modes      */

/* SCTLR M (MPU), C (D-cache) and I (I-cache) bits. */
#define SCTLR_M_BIT                 0x1U
#define SCTLR_C_BIT                 (0x1U << 2)
#define SCTLR_I_BIT                 (0x1U << 12)

#ifdef FWTPM_DCACHE
/* Grouped APU-shared non-cacheable window (rsc_table + trace_buffer),
 * placed at the carveout base and 64 KiB-sized by lscript-dcache.ld. */
extern char __shared_nc_start[];
#define SHARED_NC_SIZE     0x10000U   /* must match _SHARED_NC_SIZE in the .ld */
/* Vring + rpmsg buffer pool (APU-shared virtio rings), outside the
 * firmware carveout. Matches ZCU102_R5_VRING0_BASE in xparameters.h;
 * 512 KiB covers vring0/1 + the 256 KiB buffer pool. */
#define VRING_NC_BASE      0x3EE00000U
#define VRING_NC_SIZE_ENC  REGION_ENC_512KB
#endif

extern void outbyte(char c);
static void emit_str(const char *s) { while (*s) outbyte(*s++); }

static void mpu_program_region(uint32_t reg_num,
                               uint32_t base,
                               uint32_t size_enc,
                               uint32_t attrib)
{
    /* Select region. */
    MCR(p15, 0, reg_num, c6, c2, 0);
    ISB();
    /* Base address (must be aligned to size). */
    MCR(p15, 0, base, c6, c1, 0);
    /* Access control attributes (TEX/C/B/S/AP). */
    MCR(p15, 0, attrib, c6, c1, 4);
    /* Size + enable. */
    {
        uint32_t size_en = (size_enc << 1) | REGION_EN_BIT;
        MCR(p15, 0, size_en, c6, c1, 2);
    }
    DSB();
    ISB();
}

static void mpu_disable_all_regions(void)
{
    uint32_t n;
    for (n = 0; n < 16U; n++) {
        uint32_t size_en;
        MCR(p15, 0, n, c6, c2, 0);
        ISB();
        MRC(p15, 0, size_en, c6, c1, 2);
        size_en &= ~REGION_EN_BIT;
        MCR(p15, 0, size_en, c6, c1, 2);
        DSB();
        ISB();
    }
}

/* Invalidate the entire I-cache and branch predictor before enabling the
 * I-cache. remoteproc stop/start can preserve CP15/cache state, so a
 * prior run's cached instructions (or stale predictions) must be flushed
 * before the freshly loaded ELF runs. */
static void mpu_icache_invalidate_all(void)
{
    /* ICIALLU: invalidate all instruction caches to PoU. */
    MCR(p15, 0, 0U, c7, c5, 0);
    /* BPIALL: invalidate all branch predictors. */
    MCR(p15, 0, 0U, c7, c5, 6);
    DSB();
    ISB();
}

#ifdef FWTPM_DCACHE
/* Invalidate (not clean) the entire L1 D-cache by set/way before
 * enabling the D-cache, so stale lines a prior cached run may have left
 * cannot shadow the freshly written DDR. Discarding is safe: boot.S
 * wrote .data/.bss to DDR while the cache was off. Geometry is read from
 * CCSIDR (Cortex-R5 D-cache is 4-way, 32-byte lines). */
static void mpu_dcache_invalidate_all(void)
{
    uint32_t ccsidr, ways, sets, way, set, line_log2, way_shift, val;

    /* CSSELR = 0: select L1 data cache, then read its CCSIDR. */
    MCR(p15, 2, 0U, c0, c0, 0);
    ISB();
    MRC(p15, 1, ccsidr, c0, c0, 0);

    line_log2 = (ccsidr & 0x7U) + 4U;                 /* log2(line bytes) */
    ways      = ((ccsidr >> 3) & 0x3FFU) + 1U;        /* associativity    */
    sets      = ((ccsidr >> 13) & 0x7FFFU) + 1U;      /* number of sets   */
    /* Way field is left-justified: shift = 32 - ceil(log2(ways)). */
    way_shift = (uint32_t)__builtin_clz(ways - 1U);

    for (way = 0; way < ways; way++) {
        for (set = 0; set < sets; set++) {
            val = (way << way_shift) | (set << line_log2);
            /* DCISW: invalidate data cache line by set/way. */
            MCR(p15, 0, val, c7, c6, 2);
        }
    }
    DSB();
    ISB();
}
#endif /* FWTPM_DCACHE */

void mpu_init(void)
{
    uint32_t sctlr;

    /* Disable MPU and caches FIRST, before any memory access that could
     * be filtered by stale MPU state. On a cold reset MPU is off and any
     * DDR write works; but ZynqMP remoteproc stop/start may preserve
     * CP15 state, so a previous run's region map or caches could still
     * be active and block trace_buffer writes. Run the SCTLR clear
     * before the first outbyte() to make trace output observable on
     * every boot regardless of prior state. */
    MRC(p15, 0, sctlr, c1, c0, 0);
    sctlr &= ~(SCTLR_M_BIT | SCTLR_C_BIT | SCTLR_I_BIT);
    DSB();
    MCR(p15, 0, sctlr, c1, c0, 0);
    ISB();

    emit_str("mpu: disable\r\n");
    emit_str("mpu: clear regions\r\n");
    mpu_disable_all_regions();

    emit_str("mpu: region 0 (2G Normal-WB)\r\n");
    mpu_program_region(0U, 0x00000000U, REGION_ENC_2GB,
                       ATTR_NORM_NSHARED_WB_WA | ATTR_AP_PRIV_RW_USER_RW);

    emit_str("mpu: region 1 (256M Device @0xF0000000)\r\n");
    mpu_program_region(1U, 0xF0000000U, REGION_ENC_256MB,
                       ATTR_DEVICE_NONSHARED | ATTR_AP_PRIV_RW_USER_RW);

#ifdef FWTPM_DCACHE
    /* Region 2: APU-shared rsc_table + trace_buffer window -- keep NC so
     * the trace ring and virtio handshake stay coherent with caches on. */
    emit_str("mpu: region 2 (64K Normal-NC shared)\r\n");
    mpu_program_region(2U, (uint32_t)(uintptr_t)__shared_nc_start,
                       REGION_ENC_64KB,
                       ATTR_NORM_NSHARED_NCACHE | ATTR_AP_PRIV_RW_USER_RW);

    /* Region 3: vring + rpmsg buffer pool -- NC for the same reason. */
    emit_str("mpu: region 3 (512K Normal-NC vrings)\r\n");
    mpu_program_region(3U, VRING_NC_BASE, VRING_NC_SIZE_ENC,
                       ATTR_NORM_NSHARED_NCACHE | ATTR_AP_PRIV_RW_USER_RW);
#endif

    /* Flush stale cache/branch-predictor state before enabling. */
    mpu_icache_invalidate_all();
#ifdef FWTPM_DCACHE
    mpu_dcache_invalidate_all();
#endif

    emit_str("mpu: enable\r\n");
    /* Enable MPU + I-cache (+ D-cache under FWTPM_DCACHE). Without
     * FWTPM_DCACHE the D-cache stays off and all data bypasses the cache,
     * keeping APU-shared memory coherent without flushes. With it, only
     * regions 2/3 (NC) bypass the cache. */
    MRC(p15, 0, sctlr, c1, c0, 0);
    sctlr |= SCTLR_M_BIT | SCTLR_I_BIT;
#ifdef FWTPM_DCACHE
    sctlr |= SCTLR_C_BIT;
#endif
    DSB();
    MCR(p15, 0, sctlr, c1, c0, 0);
    ISB();

#ifdef FWTPM_DCACHE
    emit_str("mpu: enabled (icache + dcache, shared NC)\r\n");
#else
    emit_str("mpu: enabled (icache on, dcache off)\r\n");
#endif
}
