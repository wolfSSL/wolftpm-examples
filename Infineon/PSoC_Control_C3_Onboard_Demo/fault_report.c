/* fault_report.c
 *
 * Say what happened before spinning.
 *
 * wolfBoot's startup parks every fault in a bare while(1), which on a board
 * whose only output is the console is indistinguishable from a slow TPM: the
 * log simply stops. These override the weak handlers and print the fault
 * registers first, so a crash can be told apart from a wait.
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
#include <stdint.h>

#include "demo_util.h"

#define SCB_CFSR    (*(volatile uint32_t *)0xE000ED28UL)
#define SCB_HFSR    (*(volatile uint32_t *)0xE000ED2CUL)
#define SCB_MMFAR   (*(volatile uint32_t *)0xE000ED34UL)
#define SCB_BFAR    (*(volatile uint32_t *)0xE000ED38UL)

static void report(const char* which)
{
    demo_put("{\"event\":\"fault\",\"kind\":\"");
    demo_put(which);
    demo_put("\",\"cfsr\":\"0x");
    demo_put_hex32(SCB_CFSR);
    demo_put("\",\"hfsr\":\"0x");
    demo_put_hex32(SCB_HFSR);
    demo_put("\",\"mmfar\":\"0x");
    demo_put_hex32(SCB_MMFAR);
    demo_put("\",\"bfar\":\"0x");
    demo_put_hex32(SCB_BFAR);
    demo_put("\",\"handler_frame\":\"0x");
    demo_put_hex32((uint32_t)(uintptr_t)&which);
    demo_put("\"}\r\n");
    while (1)
        ;
}

void isr_fault(void)      { report("hard"); }
void isr_memfault(void)   { report("mem"); }
void isr_busfault(void)   { report("bus"); }
void isr_usagefault(void) { report("usage"); }
