/* i2c_trace.c
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
#include "i2c_trace.h"

#ifdef DEMO_I2C_TRACE

#include "demo_util.h"

typedef struct {
    uint32_t cycles;
    uint16_t len;
    uint8_t  reg;
    uint8_t  rc;
    uint8_t  tries;
    uint8_t  val;
    uint8_t  isRead;
} I2C_TRACE_ENT;

static I2C_TRACE_ENT trace[I2C_TRACE_ENTRIES];
static uint32_t traceCount;   /* total offered, so a wrap is visible */

void i2c_trace_reset(void)
{
    traceCount = 0;
}

/* Keeps the first entries rather than the last. The interesting part of a
 * stalled exchange is the command write at the start; a ring would have
 * overwritten it with thousands of identical status polls. */
void i2c_trace_add(uint8_t reg, uint8_t isRead, uint8_t rc, uint8_t tries,
    uint8_t val, uint16_t len, uint32_t cycles)
{
    if (traceCount < I2C_TRACE_ENTRIES) {
        I2C_TRACE_ENT* e = &trace[traceCount];

        e->cycles = cycles;
        e->len    = len;
        e->reg    = reg;
        e->rc     = rc;
        e->tries  = tries;
        e->val    = val;
        e->isRead = isRead;
    }
    traceCount++;

    /* A line every so often, so a run that never returns still shows whether
     * the bus is being driven or the CPU has stopped. Rare enough that the
     * console does not perturb what is being measured. */
    if ((traceCount % 256) == 0) {
        demo_put("{\"event\":\"i2c.alive\",\"n\":");
        demo_put_u32(traceCount);
        demo_put(",\"ms\":");
        demo_put_u32(demo_uptime_ms());
        demo_put(",\"reg\":\"0x");
        demo_put_hex32(reg);
        demo_put("\",\"val\":\"0x");
        demo_put_hex32(val);
        demo_put("\"}\r\n");
    }
}

void i2c_trace_dump(void)
{
    const I2C_TRACE_ENT* e;
    uint32_t held, first, i, n;

    held = (traceCount < I2C_TRACE_ENTRIES) ? traceCount : I2C_TRACE_ENTRIES;
    first = 0;

    demo_put("{\"event\":\"i2c.trace.begin\",\"total\":");
    demo_put_u32(traceCount);
    demo_put(",\"held\":");
    demo_put_u32(held);
    demo_put("}\r\n");

    for (i = 0; i < held; i++) {
        n = first + i;
        e = &trace[n];
        demo_put("{\"event\":\"i2c.xfer\",\"n\":");
        demo_put_u32(n);
        demo_put(",\"reg\":\"0x");
        demo_put_hex32(e->reg);
        demo_put("\",\"rw\":\"");
        demo_put(e->isRead ? "r" : "w");
        demo_put("\",\"rc\":");
        demo_put_u32(e->rc);
        demo_put(",\"tries\":");
        demo_put_u32(e->tries);
        demo_put(",\"val\":\"0x");
        demo_put_hex32(e->val);
        demo_put("\",\"len\":");
        demo_put_u32(e->len);
        demo_put(",\"us\":");
        demo_put_u32(e->cycles / (CPU_HZ / 1000000UL));
        demo_put("}\r\n");
    }

    demo_put("{\"event\":\"i2c.trace.end\"}\r\n");
}

#endif /* DEMO_I2C_TRACE */
