/* stm32h5xx_hal_msp.c
 *
 * MSP (MCU Support Package) initialization callbacks for STM32H5.
 * Configures GPIO, clocks, and NVIC for USART3 and RNG peripherals.
 *
 * Copyright (C) 2006-2025 wolfSSL Inc.
 */

#include "stm32h5xx_hal.h"

void HAL_MspInit(void)
{
    /* On STM32H5, PWR and SYSCFG clocks are enabled by default */
}

/* USART3 MSP Init: PD8=TX (AF7), PD9=RX (AF7) */
void HAL_UART_MspInit(UART_HandleTypeDef* huart)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    if (huart->Instance == USART3) {
        __HAL_RCC_USART3_CLK_ENABLE();
        __HAL_RCC_GPIOD_CLK_ENABLE();

        /* PD8 = USART3_TX, PD9 = USART3_RX */
        GPIO_InitStruct.Pin = GPIO_PIN_8 | GPIO_PIN_9;
        GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
        GPIO_InitStruct.Pull = GPIO_NOPULL;
        GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
        GPIO_InitStruct.Alternate = GPIO_AF7_USART3;
        HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);
    }
}

void HAL_UART_MspDeInit(UART_HandleTypeDef* huart)
{
    if (huart->Instance == USART3) {
        __HAL_RCC_USART3_CLK_DISABLE();
        HAL_GPIO_DeInit(GPIOD, GPIO_PIN_8 | GPIO_PIN_9);
    }
}

/* RNG MSP Init: enable RNG clock with HSI48 source */
void HAL_RNG_MspInit(RNG_HandleTypeDef* hrng)
{
    RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};

    if (hrng->Instance == RNG) {
        /* Select HSI48 as RNG clock source */
        PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_RNG;
        PeriphClkInit.RngClockSelection = RCC_RNGCLKSOURCE_HSI48;
        HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit);

        __HAL_RCC_RNG_CLK_ENABLE();
    }
}

void HAL_RNG_MspDeInit(RNG_HandleTypeDef* hrng)
{
    if (hrng->Instance == RNG) {
        __HAL_RCC_RNG_CLK_DISABLE();
    }
}
