/* i2c_trace.h
 *
 * A record of what the TPM's I2C bus actually did, kept in RAM and printed
 * afterwards.
 *
 * Printing from inside the transfer path is not an option: the console is
 * slower than the bus and the trace would measure itself. Entries go into a
 * ring buffer and the act dumps them once it is finished.
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
#ifndef I2C_TRACE_H_INCLUDED
#define I2C_TRACE_H_INCLUDED

#include <stdint.h>

#ifdef DEMO_I2C_TRACE

#ifndef I2C_TRACE_ENTRIES
#define I2C_TRACE_ENTRIES 512
#endif

/* reg      TIS register offset, which identifies the phase: 0x18 TPM_STS,
 *          0x24 the FIFO, 0x04 TPM_ACCESS.
 * val      first byte transferred. For a TPM_STS read this is the status
 *          byte itself, which is the one value worth having.
 * tries    attempts consumed, register phase in the low nibble and data
 *          phase in the high nibble.
 * cycles   wall time for the whole callback. A stall shows up here. */
void i2c_trace_add(uint8_t reg, uint8_t isRead, uint8_t rc, uint8_t tries,
    uint8_t val, uint16_t len, uint32_t cycles);

void i2c_trace_reset(void);

/* Emit every entry as its own event, oldest first. */
void i2c_trace_dump(void);

#else

#define i2c_trace_add(reg, isRead, rc, tries, val, len, cycles) do { } while (0)
#define i2c_trace_reset() do { } while (0)
#define i2c_trace_dump() do { } while (0)

#endif /* DEMO_I2C_TRACE */

#endif /* I2C_TRACE_H_INCLUDED */
