/* fwtpm_trng_scb.c
 *
 * Hardware entropy seed for fwTPM on PolarFire SoC via the MSS System
 * Controller "nonce" service (SYS_SERV_CMD_NONCE). This is the default
 * backend (FWTPM_RNG=SCB_NONCE; see Makefile); the opt-in JITTER build
 * uses the development-only mpfs_rng_seed_cb in mpfs_hal.c.
 *
 * This replaces the predictable MTIME-jitter seed with a real DRBG seed
 * source for production key generation. The System Controller mailbox is
 * a single shared hardware resource: ensure no other master (HSS at
 * runtime, a Linux mss-sys-services driver) drives the SCB mailbox while
 * hart 4 issues these requests, or the service can corrupt/stall. Under
 * the wolfBoot-hosted track this is the same path wolfBoot exposes as
 * mpfs_nonce(). Bench-validated on the MPFS250T Video Kit (seeds the DRBG;
 * fwTPM init and the smoke test pass); the shared-mailbox contention above
 * is the remaining item to confirm for a production deployment.
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

#include "mpfs_hal.h"
#include <stdint.h>
#include <stddef.h>

#ifdef FWTPM_RNG_SCB_NONCE

/* MSS System Controller register block (polling-mode service requests).
 * Addresses and protocol per the PolarFire SoC System Services spec. */
#define SCBCTRL_BASE                0x37020000UL
#define SCBMBOX_BASE                0x37020800UL

#define SERVICES_CR_OFFSET          0x50u   /* control register */
#define SERVICES_SR_OFFSET          0x54u   /* status register */
#define SERVICES_CR_REQ_MASK        0x01u   /* request bit (self-clears) */
#define SERVICES_CR_COMMAND_SHIFT   16      /* opcode field position */
#define SERVICES_SR_BUSY_MASK       0x02u   /* service in progress */
#define SERVICES_SR_STATUS_SHIFT    16      /* 16-bit service status */

#define SYS_SERV_CMD_NONCE          0x21u   /* 256-bit random nonce service */
#define MPFS_NONCE_LEN              32

/* Request-accept is quick; service completion (nonce/PUF) is much slower. */
#define SCB_REQ_TIMEOUT             10000UL
#define SCB_BUSY_TIMEOUT            20000000UL

#define SCBCTRL_REG(off)  (*((volatile uint32_t *)(SCBCTRL_BASE + (off))))
#define SCBMBOX_BYTE(off) (*((volatile uint8_t  *)(SCBMBOX_BASE + (off))))

static int scb_mailbox_busy(void)
{
    return (SCBCTRL_REG(SERVICES_SR_OFFSET) & SERVICES_SR_BUSY_MASK) ? 1 : 0;
}

/* Issue a no-payload System Controller service in polling mode and return
 * the 16-bit service status (0 = success) or a negative transport error. */
static int scb_request_nopayload(uint8_t opcode)
{
    uint32_t cmd;
    uint32_t timeout;

    if (scb_mailbox_busy() != 0)
        return -2;

    /* Raise the request: opcode in the command field, request bit set,
     * mailbox word offset 0. Ensure prior stores retire first. */
    __asm__ volatile("fence w,w" ::: "memory");
    cmd = (((uint32_t)(opcode & 0x7Fu)) << SERVICES_CR_COMMAND_SHIFT) |
          SERVICES_CR_REQ_MASK;
    SCBCTRL_REG(SERVICES_CR_OFFSET) = cmd;

    /* Wait for the request bit to self-clear (command accepted). */
    timeout = SCB_REQ_TIMEOUT;
    while ((SCBCTRL_REG(SERVICES_CR_OFFSET) & SERVICES_CR_REQ_MASK) != 0) {
        if (timeout == 0)
            return -3;
        timeout--;
    }

    /* Wait for the busy bit to clear (service complete). */
    timeout = SCB_BUSY_TIMEOUT;
    while (scb_mailbox_busy() != 0) {
        if (timeout == 0)
            return -4;
        timeout--;
    }

    return (int)((SCBCTRL_REG(SERVICES_SR_OFFSET) >> SERVICES_SR_STATUS_SHIFT)
                 & 0xFFFFu);
}

/* wolfCrypt DRBG seed callback (CUSTOM_RAND_GENERATE_SEED). Fills sz bytes
 * from the System Controller nonce service, 32 bytes per request. Returns 0
 * on success, nonzero on any SCB failure -- never falls back to a weaker
 * source (WC_NO_RNG_SEED_FALLBACK is set), so a hardware fault fails the
 * seed rather than silently producing weak keys. */
int mpfs_rng_seed_scb(unsigned char *output, unsigned int sz)
{
    unsigned int done;
    unsigned int i;
    unsigned int chunk;
    int rc;

    if (output == NULL)
        return -1;

    for (done = 0; done < sz; done += chunk) {
        rc = scb_request_nopayload(SYS_SERV_CMD_NONCE);
        if (rc != 0)
            return rc < 0 ? rc : -10;

        chunk = sz - done;
        if (chunk > (unsigned int)MPFS_NONCE_LEN)
            chunk = (unsigned int)MPFS_NONCE_LEN;

        for (i = 0; i < chunk; i++)
            output[done + i] = SCBMBOX_BYTE(i);
    }

    return 0;
}

#endif /* FWTPM_RNG_SCB_NONCE */
