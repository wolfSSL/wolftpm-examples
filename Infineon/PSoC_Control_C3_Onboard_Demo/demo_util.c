/* demo_util.c
 *
 * Console output, timing and board setup shared by the on-board demo acts.
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
#include "demo_util.h"

#include "hal/psoc_c3.h"

extern void uart_init(void);
extern void uart_write(const char *buf, unsigned int sz);

#ifndef PSOC_C3_TPM_SEL_PORT
#define PSOC_C3_TPM_SEL_PORT    7
#endif
#ifndef PSOC_C3_TPM_SEL_PIN
#define PSOC_C3_TPM_SEL_PIN     7
#endif
#ifndef PSOC_C3_UART_SCB
#define PSOC_C3_UART_SCB        3
#endif
#define DEMO_UART_BASE  PSOC_C3_SCB_BASE(PSOC_C3_UART_SCB)

#define DEMCR       (*(volatile uint32_t *)0xE000EDFCUL)
#define DWT_CTRL    (*(volatile uint32_t *)0xE0001000UL)
#define DWT_CYCCNT  (*(volatile uint32_t *)0xE0001004UL)
#define DWT_LAR     (*(volatile uint32_t *)0xE0001FB0UL)

void demo_board_init(void)
{
    psoc_c3_peri_init();
    psoc_c3_pin_setup(PSOC_C3_TPM_SEL_PORT, PSOC_C3_TPM_SEL_PIN, 0,
            GPIO_CFG_DM_STRONG);
    GPIO_PRT_OUT(PSOC_C3_TPM_SEL_PORT) &= ~(1UL << PSOC_C3_TPM_SEL_PIN);

    uart_init();

    DWT_LAR = 0xC5ACCE55UL;
    DEMCR |= (1UL << 24);
    DWT_CYCCNT = 0;
    DWT_CTRL |= 1UL;
}

void demo_put(const char *s)
{
    unsigned int n = 0;

    while (s[n] != 0)
        n++;
    uart_write(s, n);
}

void demo_put_u32(uint32_t v)
{
    char tmp[12];
    char out[12];
    int t = 0, n = 0;

    if (v == 0) {
        out[n++] = '0';
    }
    else {
        while (v > 0) {
            tmp[t++] = (char)('0' + (v % 10));
            v /= 10;
        }
        while (t > 0)
            out[n++] = tmp[--t];
    }
    uart_write(out, (unsigned int)n);
}

void demo_put_hex32(uint32_t v)
{
    static const char d[] = "0123456789abcdef";
    char out[8];
    int i;

    for (i = 7; i >= 0; i--) {
        out[i] = d[v & 0xF];
        v >>= 4;
    }
    uart_write(out, 8);
}

void demo_put_hexbuf(const uint8_t *b, uint32_t n)
{
    static const char d[] = "0123456789ABCDEF";
    char pair[2];
    uint32_t i;

    for (i = 0; i < n; i++) {
        pair[0] = d[(b[i] >> 4) & 0xF];
        pair[1] = d[b[i] & 0xF];
        uart_write(pair, 2);
    }
}

void demo_put_b64(const uint8_t *b, uint32_t n)
{
    static const char t[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    char q[4];
    uint32_t i;

    for (i = 0; i < n; i += 3) {
        uint32_t v = (uint32_t)b[i] << 16;
        uint32_t left = n - i;

        if (left > 1)
            v |= (uint32_t)b[i + 1] << 8;
        if (left > 2)
            v |= (uint32_t)b[i + 2];

        q[0] = t[(v >> 18) & 0x3F];
        q[1] = t[(v >> 12) & 0x3F];
        q[2] = (left > 1) ? t[(v >> 6) & 0x3F] : '=';
        q[3] = (left > 2) ? t[v & 0x3F] : '=';
        uart_write(q, 4);
    }
}

uint32_t demo_cycles(void)
{
    return DWT_CYCCNT;
}

uint32_t demo_ms_since(uint32_t startCycles)
{
    return (DWT_CYCCNT - startCycles) / (CPU_HZ / 1000UL);
}

/* Milliseconds since boot. The cycle counter wraps about every 24 s, so each
 * call folds in the elapsed cycles; valid only while called more often than
 * that, which wolfTPM's wait loops are. */
uint32_t demo_uptime_ms(void)
{
    static uint32_t lastCycles;
    static uint32_t carryCycles;
    static uint32_t ms;
    uint32_t now = DWT_CYCCNT;
    uint32_t delta = now - lastCycles + carryCycles;

    lastCycles = now;
    ms += delta / (CPU_HZ / 1000UL);
    /* Keep the sub-millisecond remainder so a run of short calls still
     * advances the clock instead of truncating to zero every time. */
    carryCycles = delta % (CPU_HZ / 1000UL);

    return ms;
}

uint8_t demo_getc(void)
{
    while ((SCB_RX_FIFO_STATUS(DEMO_UART_BASE) & SCB_RX_FIFO_USED_Msk) == 0)
        ;
    return (uint8_t)SCB_RX_FIFO_RD(DEMO_UART_BASE);
}

void demo_fail(const char *where, int rc)
{
    demo_put("{\"event\":\"error\",\"where\":\"");
    demo_put(where);
    demo_put("\",\"rc\":\"0x");
    demo_put_hex32((uint32_t)rc);
    demo_put("\"}\r\n");
}
