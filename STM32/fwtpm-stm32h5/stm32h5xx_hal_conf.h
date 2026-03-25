/* stm32h5xx_hal_conf.h
 *
 * STM32H5 HAL module configuration for fwTPM port.
 * Only enables the modules needed by the fwTPM server.
 *
 * Copyright (C) 2006-2025 wolfSSL Inc.
 */

#ifndef STM32H5xx_HAL_CONF_H
#define STM32H5xx_HAL_CONF_H

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================= */
/* Module selection                                                          */
/* ========================================================================= */
#define HAL_MODULE_ENABLED
#define HAL_CORTEX_MODULE_ENABLED
#define HAL_DMA_MODULE_ENABLED
#define HAL_FLASH_MODULE_ENABLED
#define HAL_FLASH_EX_MODULE_ENABLED
#define HAL_GPIO_MODULE_ENABLED
#define HAL_GTZC_MODULE_ENABLED
#define HAL_ICACHE_MODULE_ENABLED
#define HAL_PWR_MODULE_ENABLED
#define HAL_PWR_EX_MODULE_ENABLED
#define HAL_RCC_MODULE_ENABLED
#define HAL_RCC_EX_MODULE_ENABLED
#define HAL_RNG_MODULE_ENABLED
#define HAL_UART_MODULE_ENABLED
#define HAL_EXTI_MODULE_ENABLED

/* ========================================================================= */
/* Oscillator Values                                                         */
/* ========================================================================= */
#if !defined(HSE_VALUE)
    #define HSE_VALUE    8000000U   /* 8 MHz HSE from ST-Link */
#endif
#if !defined(HSE_STARTUP_TIMEOUT)
    #define HSE_STARTUP_TIMEOUT    100U
#endif
#if !defined(CSI_VALUE)
    #define CSI_VALUE    4000000U
#endif
#if !defined(HSI_VALUE)
    #define HSI_VALUE    64000000U
#endif
#if !defined(HSI48_VALUE)
    #define HSI48_VALUE  48000000U
#endif
#if !defined(LSI_VALUE)
    #define LSI_VALUE    32000U
#endif
#if !defined(LSE_VALUE)
    #define LSE_VALUE    32768U
#endif
#if !defined(LSE_STARTUP_TIMEOUT)
    #define LSE_STARTUP_TIMEOUT    5000U
#endif
#if !defined(EXTERNAL_CLOCK_VALUE)
    #define EXTERNAL_CLOCK_VALUE   12288000U
#endif

/* ========================================================================= */
/* System configuration                                                      */
/* ========================================================================= */
#define VDD_VALUE                  3300U  /* mV */
#define TICK_INT_PRIORITY          15U    /* Lowest priority for SysTick */
#define USE_RTOS                   0U
#define PREFETCH_ENABLE            0U
#define INSTRUCTION_CACHE_ENABLE   1U

/* ========================================================================= */
/* Assert configuration                                                      */
/* ========================================================================= */
/* #define USE_FULL_ASSERT    1U */

/* ========================================================================= */
/* Include HAL module headers                                                */
/* ========================================================================= */
#ifdef HAL_RCC_MODULE_ENABLED
    #include "stm32h5xx_hal_rcc.h"
    #include "stm32h5xx_hal_rcc_ex.h"
#endif
#ifdef HAL_GPIO_MODULE_ENABLED
    #include "stm32h5xx_hal_gpio.h"
    #include "stm32h5xx_hal_gpio_ex.h"
#endif
#ifdef HAL_DMA_MODULE_ENABLED
    #include "stm32h5xx_hal_dma.h"
    #include "stm32h5xx_hal_dma_ex.h"
#endif
#ifdef HAL_CORTEX_MODULE_ENABLED
    #include "stm32h5xx_hal_cortex.h"
#endif
#ifdef HAL_FLASH_MODULE_ENABLED
    #include "stm32h5xx_hal_flash.h"
    #include "stm32h5xx_hal_flash_ex.h"
#endif
#ifdef HAL_GTZC_MODULE_ENABLED
    #include "stm32h5xx_hal_gtzc.h"
#endif
#ifdef HAL_ICACHE_MODULE_ENABLED
    #include "stm32h5xx_hal_icache.h"
#endif
#ifdef HAL_PWR_MODULE_ENABLED
    #include "stm32h5xx_hal_pwr.h"
    #include "stm32h5xx_hal_pwr_ex.h"
#endif
#ifdef HAL_RNG_MODULE_ENABLED
    #include "stm32h5xx_hal_rng.h"
#endif
#ifdef HAL_UART_MODULE_ENABLED
    #include "stm32h5xx_hal_uart.h"
    #include "stm32h5xx_hal_uart_ex.h"
#endif
#ifdef HAL_EXTI_MODULE_ENABLED
    #include "stm32h5xx_hal_exti.h"
#endif

#ifdef USE_FULL_ASSERT
    #define assert_param(expr) ((expr) ? (void)0U : assert_failed((uint8_t *)__FILE__, __LINE__))
    void assert_failed(uint8_t* file, uint32_t line);
#else
    #define assert_param(expr) ((void)0U)
#endif

#ifdef __cplusplus
}
#endif

#endif /* STM32H5xx_HAL_CONF_H */
