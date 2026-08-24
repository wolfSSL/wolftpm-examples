/* fwtpm_trng_sysmon.c
 *
 * FPGA hardware-entropy seed source for the MicroBlaze V fwTPM, using the
 * Spartan UltraScale+ SYSMONE4 (System Monitor) as a noise source instead of
 * wolfCrypt's MemUse entropy. Built with -DFWTPM_TINY_HWTRNG.
 *
 * The SYSMONE4 is a hardened on-die ADC that measures temperature and supply
 * rails (VCCINT/VCCAUX/...). The low bits of its conversions carry thermal and
 * electrical noise. Exposed to the MicroBlaze V through an AXI System Management
 * Wizard slave, its DRP registers are read as memory-mapped words; this driver
 * oversamples the noisy LSBs of several channels and mixes them to build a seed
 * for wolfCrypt's Hash-DRBG (wired via CUSTOM_RAND_GENERATE_SEED).
 *
 * NOTE: this requires the FPGA design to instantiate the AXI System Management
 * Wizard (SYSMONE4) at SCU35_SYSMON_BASE. For production use, condition and
 * health-test the raw LSB stream (SP800-90B) before use; the DRBG provides the
 * cryptographic expansion.
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

#include "user_settings.h"

#ifdef FWTPM_TINY_HWTRNG

#include <stdint.h>
#include <stddef.h>

#include "scu35_board.h"
#include "mbv_time.h"

/* AXI System Management Wizard base (SYSMONE4). Add the IP to the design and set
 * this to its assigned AXI slot. */
#ifndef SCU35_SYSMON_BASE
#define SCU35_SYSMON_BASE   0x44A30000UL
#endif

/* AXI System Management Wizard register map: the on-chip measurement registers
 * sit at fixed offsets 0x400 (temperature), 0x404 (VCCINT), 0x408 (VCCAUX) -
 * 16-bit right-justified ADC codes in bits [15:4], whose low bits carry thermal
 * and electrical noise. These offsets are HW-validated on the SCU35. */
#define SYSMON_TEMP     0x400u   /* on-chip temperature */
#define SYSMON_VCCINT   0x404u   /* VCCINT supply */
#define SYSMON_VCCAUX   0x408u   /* VCCAUX supply */

static uint32_t sysmon_rd(uint32_t off)
{
    return *(volatile uint32_t*)(uintptr_t)(SCU35_SYSMON_BASE + off);
}

/* Fill out[0..sz) with seed bytes derived from the SYSMON ADC channels and the
 * free-running AXI timer. The noisy ADC low bits are the entropy; each full
 * 32-bit sample word is folded across the WHOLE request into a running 32-bit
 * accumulator (rotate-left + xor the sample + FNV multiply mix), so state
 * carries between output bytes instead of resetting, and the well-mixed high
 * byte is emitted. A stuck-source health check fails closed (non-zero return)
 * if the temperature channel never varies across the request, so a missing or
 * wedged SYSMON cannot hand the DRBG a constant seed.
 *
 * NOTE: bring-up entropy source. For production, run the raw ADC stream through
 * an SP800-90B-conditioned extractor with continuous health tests before use;
 * the DRBG provides only the cryptographic expansion. Returns 0 on success. */
int mbv_trng_seed(unsigned char* out, unsigned int sz)
{
    unsigned int i;
    unsigned int j;
    uint32_t acc = 0x811C9DC5u;   /* nonzero FNV offset basis */
    uint32_t tmin = 0xFFFFFFFFu;
    uint32_t tmax = 0u;
    uint32_t temps = 0u;

    if (out == NULL) {
        return -1;
    }
    for (i = 0; i < sz; i++) {
        /* Oversample the rotating channels; fold each full sample word in. */
        for (j = 0; j < 16u; j++) {
            uint32_t s;
            switch (j & 0x3u) {
                case 0:
                    s = sysmon_rd(SYSMON_TEMP);
                    if (s < tmin) { tmin = s; }
                    if (s > tmax) { tmax = s; }
                    temps++;
                    break;
                case 1:  s = sysmon_rd(SYSMON_VCCINT); break;
                case 2:  s = sysmon_rd(SYSMON_VCCAUX); break;
                default: s = (uint32_t)mbv_ticks();    break;
            }
            acc = (acc << 1) | (acc >> 31);   /* rotate-left 1 */
            acc ^= s;                          /* inject full sample word */
            acc *= 16777619u;                  /* FNV multiply mix (avalanche) */
        }
        out[i] = (unsigned char)(acc >> 24);   /* emit well-mixed high bits */
    }
    /* Fail closed if the temperature ADC never moved across the whole request
     * (SYSMON absent or stuck): never seed the DRBG from a constant source. A
     * live monitor's low bits jitter read-to-read, so tmax > tmin holds. */
    if (temps > 1u && tmax == tmin) {
        return -1;
    }
    return 0;
}

#endif /* FWTPM_TINY_HWTRNG */
