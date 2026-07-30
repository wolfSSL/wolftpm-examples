/* miv_gpio.c
 *
 * Minimal CoreGPIO output driver for the Mi-V RV32 soft core.
 *
 * CoreGPIO register map: the combined 32-bit output register is at offset
 * 0xA0 (GPIO_OUT_REG). Writing it drives all output ports at once.
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

#include "miv_gpio.h"

#define MIV_GPIO_OUT_REG_OFFSET     0xA0u

void miv_gpio_set_outputs(uintptr_t base, uint32_t value)
{
    volatile uint32_t* out_reg;

    out_reg = (volatile uint32_t*)(base + MIV_GPIO_OUT_REG_OFFSET);
    *out_reg = value;
}
