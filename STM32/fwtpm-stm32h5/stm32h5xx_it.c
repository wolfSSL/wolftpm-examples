/* stm32h5xx_it.c
 *
 * Interrupt handlers for STM32H5 secure world.
 *
 * Copyright (C) 2006-2025 wolfSSL Inc.
 */

#include "stm32h5xx_hal.h"

void NMI_Handler(void)
{
}

void HardFault_Handler(void)
{
    while (1) {}
}

void MemManage_Handler(void)
{
    while (1) {}
}

void BusFault_Handler(void)
{
    while (1) {}
}

void UsageFault_Handler(void)
{
    while (1) {}
}

void SecureFault_Handler(void)
{
    while (1) {}
}

void SVC_Handler(void)
{
}

void DebugMon_Handler(void)
{
}

void PendSV_Handler(void)
{
}

void SysTick_Handler(void)
{
    HAL_IncTick();
}

#if TZEN_ENABLED
void GTZC_IRQHandler(void)
{
    HAL_GTZC_IRQHandler();
}
#endif
