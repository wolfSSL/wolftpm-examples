/* miv_sysserv.h
 *
 * PolarFire System Controller services reached from the Mi-V fabric through the
 * CoreSysServices_PF APB peripheral (mailbox). Shared by the fwTPM entropy seed
 * (nonce service) and the persistent NV backend (secure NVM / sNVM read+write).
 *
 * Independent implementation from the CoreSysServices_PF handbook register map;
 * does not reuse Microchip's driver sources.
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

#ifndef MIV_SYSSERV_H
#define MIV_SYSSERV_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* sNVM geometry: 221 pages. Non-authenticated plaintext mode stores 252 usable
 * bytes/page; authenticated-ciphertext mode stores 236 usable bytes/page plus a
 * 12-byte user secret key (the on-die factory key binds ciphertext to the
 * device; the user key adds an application layer). */
#define MIV_SNVM_PAGE_DATA        252U
#define MIV_SNVM_PAGE_DATA_AUTH   236U
#define MIV_SNVM_USK_LEN          12U
#define MIV_SNVM_PAGE_COUNT       221U

/* Run one System Controller service through the CoreSysServices_PF mailbox.
 *   opcode        service command (0x21 nonce, 0x10 sNVM write, 0x18 sNVM read)
 *   wr,wrSz       input mailbox bytes (NULL/0 if none); wrSz must be a multiple
 *                 of 4 and 4-byte aligned
 *   rd,rdSz       output mailbox bytes (NULL/0 if none); same alignment rules
 *   mbOffset      mailbox offset (0 for all services used here)
 *   rdWordOffset  read start offset in 32-bit words within the mailbox
 * Returns 0 on success (controller status 0), a negative value on timeout, or
 * -(100 + status) when the controller reports a non-zero status. */
int miv_sysserv_exec(uint8_t opcode,
                     const uint8_t* wr, uint16_t wrSz,
                     uint8_t* rd, uint16_t rdSz,
                     uint16_t mbOffset, uint16_t rdWordOffset);

/* Nonce service: fill 32 bytes from the on-die NRBG. Returns 0 on success. */
int miv_sysserv_nonce(unsigned char out[32]);

/* sNVM non-authenticated plaintext page write/read. page in 0..220.
 * write copies 252 bytes from data; read copies 252 bytes into data (the 4-byte
 * admin header returned by the controller is consumed internally).
 * Returns 0 on success. */
int miv_snvm_write_page(uint8_t page, const uint8_t* data);
int miv_snvm_read_page(uint8_t page, uint8_t* data);

/* sNVM authenticated-ciphertext page write/read. data is 236 bytes; usk is a
 * 12-byte user secret key that must match between write and read. The page is
 * encrypted and integrity-protected with the on-die factory key plus usk.
 * Returns 0 on success (read returns an authentication error if usk/mode differ
 * or the page was never written in this mode). */
int miv_snvm_write_page_auth(uint8_t page, const uint8_t* data,
                             const uint8_t* usk);
int miv_snvm_read_page_auth(uint8_t page, uint8_t* data, const uint8_t* usk);

#ifdef __cplusplus
}
#endif

#endif /* MIV_SYSSERV_H */
