/* main.c
 *
 * fwTPM STM32 Secure world entry point.
 * Initializes clocks, peripherals, TrustZone isolation, and runs the fwTPM
 * with a UART command interface for development/testing.
 *
 * UART protocol (length-prefixed):
 *   Host -> Device: [U32BE cmdLen][cmdPayload]
 *   Device -> Host: [U32BE rspLen][rspPayload]
 *
 * Copyright (C) 2006-2025 wolfSSL Inc.
 *
 * This file is part of wolfTPM.
 */

#include "user_settings.h"
#include "stm32h5xx_hal.h"
#include <wolftpm/fwtpm/fwtpm.h>
#include <wolftpm/fwtpm/fwtpm_nv.h>
#include <wolftpm/fwtpm/fwtpm_command.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Forward declarations */
extern int FWTPM_NV_FlashHAL_Init(FWTPM_NV_HAL* hal);
extern int FWTPM_Clock_STM32_Init(FWTPM_CTX* ctx);

static void SystemClock_Config(void);
static void SystemIsolation_Config(void);
static void MX_USART3_UART_Init(void);
static void MX_RNG_Init(void);
static void Error_Handler(void);

/* Global handles */
UART_HandleTypeDef huart3;
RNG_HandleTypeDef hrng;

/* Global fwTPM context pointer (used by fwtpm_nsc.c) */
volatile FWTPM_CTX* g_fwtpmCtx = NULL;

/* Static fwTPM context (avoids heap fragmentation on constrained targets) */
static FWTPM_CTX g_ctx;

/* UART command loop */
static void FwTPM_UartCommandLoop(FWTPM_CTX* ctx);
#ifdef FWTPM_SELFTEST
static int FwTPM_RunSelfTest(FWTPM_CTX* ctx);
#endif

/* Newlib _write syscall: route printf output.
 * When semihosting is enabled, output goes through the debug probe (SWD)
 * so UART stays clean for the mssim TPM protocol.
 * Otherwise, printf goes to USART3 (only safe before entering command mode). */
#ifdef ARM_SEMIHOSTING
/* Semihosting: _write is provided by rdimon.specs */
extern int _write(int fd, char* ptr, int len);
#else
static volatile int g_uartCmdMode = 0; /* set to 1 when entering TPM cmd loop */
int _write(int fd, char* ptr, int len)
{
    (void)fd;
    if (g_uartCmdMode == 0) {
        HAL_UART_Transmit(&huart3, (uint8_t*)ptr, (uint16_t)len, HAL_MAX_DELAY);
    }
    /* In command mode, printf output is silently dropped to avoid
     * corrupting the mssim protocol on UART */
    return len;
}
#endif

#ifdef ARM_SEMIHOSTING
extern void initialise_monitor_handles(void);
#endif

int main(void)
{
    int rc;
    FWTPM_CTX* ctx;
    FWTPM_NV_HAL nvHal;

    /* HAL init: SysTick, NVIC priority, low-level init */
    HAL_Init();

#ifdef ARM_SEMIHOSTING
    /* Enable semihosting: printf output goes through SWD debug probe */
    initialise_monitor_handles();
#endif

    /* Enable instruction cache */
    HAL_ICACHE_Enable();

#if TZEN_ENABLED
    /* Configure TrustZone memory isolation */
    SystemIsolation_Config();

    /* Enable SecureFault handler */
    SCB->SHCSR |= SCB_SHCSR_SECUREFAULTENA_Msk;
#endif

    /* Configure system clock: 250MHz from HSE via PLL */
    SystemClock_Config();

    /* Initialize peripherals */
    MX_USART3_UART_Init();
    MX_RNG_Init();

    printf("\r\n=== wolfTPM fwTPM Server (STM32H5 Secure) ===\r\n");

    /* Use static fwTPM context (avoids heap fragmentation) */
    memset(&g_ctx, 0, sizeof(g_ctx));
    ctx = &g_ctx;

    /* Register NV flash HAL */
    FWTPM_NV_FlashHAL_Init(&nvHal);
    FWTPM_NV_SetHAL(ctx, &nvHal);

    /* Register clock HAL */
    FWTPM_Clock_STM32_Init(ctx);

    /* Initialize fwTPM */
    rc = FWTPM_Init(ctx);
    if (rc != 0) {
        printf("ERROR: FWTPM_Init failed: %d\r\n", rc);
        Error_Handler();
    }

    g_fwtpmCtx = ctx;

    printf("fwTPM v%s initialized OK\r\n", FWTPM_GetVersionString());
    printf("  FWTPM_CTX size: %u bytes\r\n",
        (unsigned int)sizeof(FWTPM_CTX));
    printf("  NV flash: 0x%08X (%u KB)\r\n",
        (unsigned int)FWTPM_NV_FLASH_BASE,
        (unsigned int)(FWTPM_NV_FLASH_SIZE / 1024));
#ifdef FWTPM_SELFTEST
    /* Self-test mode: run built-in TPM tests and exit via BKPT.
     * Used for CI with m33mu emulator: --expect-bkpt 0x4A (pass). */
    {
        int testRc;
        printf("Running self-test...\r\n");
        testRc = FwTPM_RunSelfTest(ctx);
        if (testRc == 0) {
            printf("SELF-TEST PASSED\r\n");
            __asm volatile("bkpt #0x4A"); /* pass */
        }
        else {
            printf("SELF-TEST FAILED (rc=%d)\r\n", testRc);
            __asm volatile("bkpt #0x01"); /* fail */
        }
    }
#else
    printf("Waiting for UART commands...\r\n");

    /* Enter UART command processing loop.
     * In non-semihosting mode, suppress printf to avoid corrupting mssim. */
#ifndef ARM_SEMIHOSTING
    g_uartCmdMode = 1;
#endif
    FwTPM_UartCommandLoop(ctx);
#endif /* FWTPM_SELFTEST */

    /* Should not reach here */
    FWTPM_Cleanup(ctx);
    return 0;
}

#ifdef FWTPM_SELFTEST
/* Built-in self-test: exercises key TPM commands via FWTPM_ProcessCommand.
 * Returns 0 on success, negative on failure. */
static int FwTPM_RunSelfTest(FWTPM_CTX* ctx)
{
    int rspSize;
    uint32_t rc;
    uint8_t cmd[64];
    int pos;

    /* Helper: build a simple TPM command (no sessions) */
    #define BUILD_CMD(cc, extra_len) do { \
        pos = 0; \
        cmd[pos++] = 0x80; cmd[pos++] = 0x01; /* tag: NO_SESSIONS */ \
        cmd[pos++] = 0; cmd[pos++] = 0; \
        cmd[pos++] = 0; cmd[pos++] = (uint8_t)(10 + (extra_len)); /* size */ \
        cmd[pos++] = (uint8_t)((cc) >> 24); cmd[pos++] = (uint8_t)((cc) >> 16); \
        cmd[pos++] = (uint8_t)((cc) >> 8); cmd[pos++] = (uint8_t)(cc); \
    } while (0)

    #define CHECK_RC(name) do { \
        if (rspSize < 10) { printf("  %s: short response\r\n", name); return -1; } \
        rc = ((uint32_t)ctx->rspBuf[6] << 24) | ((uint32_t)ctx->rspBuf[7] << 16) | \
             ((uint32_t)ctx->rspBuf[8] << 8) | (uint32_t)ctx->rspBuf[9]; \
        printf("  %s: rc=0x%08X %s\r\n", name, (unsigned)rc, rc == 0 ? "OK" : "FAIL"); \
        if (rc != 0 && rc != 0x100) return -1; \
    } while (0)

    /* 1. TPM2_Startup(CLEAR) */
    BUILD_CMD(0x144, 2);
    cmd[pos++] = 0; cmd[pos++] = 0; /* SU_CLEAR */
    rspSize = FWTPM_MAX_COMMAND_SIZE;
    FWTPM_ProcessCommand(ctx, cmd, pos, ctx->rspBuf, &rspSize, 0);
    CHECK_RC("Startup");

    /* 2. TPM2_SelfTest(fullTest=YES) */
    BUILD_CMD(0x143, 1);
    cmd[pos++] = 1; /* fullTest = YES */
    rspSize = FWTPM_MAX_COMMAND_SIZE;
    FWTPM_ProcessCommand(ctx, cmd, pos, ctx->rspBuf, &rspSize, 0);
    CHECK_RC("SelfTest");

    /* 3. TPM2_GetRandom(16) */
    BUILD_CMD(0x17B, 2);
    cmd[pos++] = 0; cmd[pos++] = 16; /* 16 bytes */
    rspSize = FWTPM_MAX_COMMAND_SIZE;
    FWTPM_ProcessCommand(ctx, cmd, pos, ctx->rspBuf, &rspSize, 0);
    CHECK_RC("GetRandom");
    if (rspSize >= 12) {
        uint16_t randSz = ((uint16_t)ctx->rspBuf[10] << 8) | ctx->rspBuf[11];
        printf("  Random bytes: %u\r\n", randSz);
    }

    /* 4. TPM2_GetCapability(TPM_PT_MANUFACTURER) */
    BUILD_CMD(0x17A, 12);
    /* capability = TPM_CAP_TPM_PROPERTIES (6) */
    cmd[pos++] = 0; cmd[pos++] = 0; cmd[pos++] = 0; cmd[pos++] = 6;
    /* property = TPM_PT_MANUFACTURER (0x105) */
    cmd[pos++] = 0; cmd[pos++] = 0; cmd[pos++] = 1; cmd[pos++] = 5;
    /* count = 1 */
    cmd[pos++] = 0; cmd[pos++] = 0; cmd[pos++] = 0; cmd[pos++] = 1;
    rspSize = FWTPM_MAX_COMMAND_SIZE;
    FWTPM_ProcessCommand(ctx, cmd, pos, ctx->rspBuf, &rspSize, 0);
    CHECK_RC("GetCapability");

    #undef BUILD_CMD
    #undef CHECK_RC

    printf("All self-tests passed\r\n");
    return 0;
}
#endif /* FWTPM_SELFTEST */

/* Helper: read exactly N bytes from UART */
static int UartRecv(uint8_t* buf, uint32_t sz)
{
    if (HAL_UART_Receive(&huart3, buf, (uint16_t)sz, 10000) != HAL_OK) {
        return -1;
    }
    return 0;
}

/* Helper: send exactly N bytes over UART */
static int UartSend(const uint8_t* buf, uint32_t sz)
{
    if (HAL_UART_Transmit(&huart3, buf, (uint16_t)sz, HAL_MAX_DELAY)
            != HAL_OK) {
        return -1;
    }
    return 0;
}

/* Helper: load U32 big-endian from buffer */
static uint32_t LoadU32BE(const uint8_t* p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

/* Helper: store U32 big-endian to buffer */
static void StoreU32BE(uint8_t* p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)(v);
}

/* Helper: send U32BE ack (0 = success) */
static void UartSendAck(void)
{
    uint8_t ack[4] = {0, 0, 0, 0};
    UartSend(ack, 4);
}

/* mssim signal codes (from fwtpm_io.h) */
#define MSSIM_SIGNAL_POWER_ON   1
#define MSSIM_SIGNAL_POWER_OFF  2
#define MSSIM_SIGNAL_RESET      17
#define MSSIM_SEND_COMMAND      8
#define MSSIM_SESSION_END       20
#define MSSIM_STOP              21

/* UART command loop: mssim protocol (compatible with wolfTPM swtpm client)
 *
 * Command:  [U32BE cmdCode][U8 locality][U32BE cmdSz][cmdPayload]
 * Response: [U32BE rspSz][rspPayload][U32BE ack=0]
 * Signal:   [U32BE signalCode] → [U32BE ack=0]
 * Session end: [U32BE 20] → (no ack)
 *
 * Also auto-detects swtpm protocol (raw TPM packets starting with
 * tag 0x8001/0x8002). */
static void FwTPM_UartCommandLoop(FWTPM_CTX* ctx)
{
    uint8_t hdr[4];
    uint32_t firstWord;
    uint16_t tag;
    uint32_t mssimCmd;
    uint8_t locality;
    uint32_t cmdSize;
    uint32_t remaining;
    int rspSize;
    uint8_t rspHdr[4];

    while (1) {
        /* Read first 4 bytes to determine protocol */
        if (UartRecv(hdr, 4) != 0) {
            continue;
        }

        /* Check for swtpm protocol: raw TPM command starts with
         * tag 0x8001 (NO_SESSIONS) or 0x8002 (SESSIONS) */
        tag = ((uint16_t)hdr[0] << 8) | (uint16_t)hdr[1];
        if (tag == 0x8001 || tag == 0x8002) {
            /* swtpm: first 4 bytes are tag(2) + start of size(2).
             * Read 6 more bytes to complete 10-byte TPM header. */
            memcpy(ctx->cmdBuf, hdr, 4);
            if (UartRecv(ctx->cmdBuf + 4, 6) != 0) {
                continue;
            }
            cmdSize = LoadU32BE(ctx->cmdBuf + 2);

            if (cmdSize < 10 || cmdSize > FWTPM_MAX_COMMAND_SIZE) {
                continue;
            }

            /* Read remaining command bytes */
            remaining = cmdSize - 10;
            if (remaining > 0) {
                if (UartRecv(ctx->cmdBuf + 10, remaining) != 0) {
                    continue;
                }
            }

            /* Process and send raw response (no mssim framing) */
            rspSize = FWTPM_MAX_COMMAND_SIZE;
            FWTPM_ProcessCommand(ctx, ctx->cmdBuf, (int)cmdSize,
                ctx->rspBuf, &rspSize, 0);
            if (rspSize > 0) {
                UartSend(ctx->rspBuf, (uint32_t)rspSize);
            }
            continue;
        }

        /* mssim protocol: first 4 bytes are a command code (big-endian) */
        firstWord = LoadU32BE(hdr);
        mssimCmd = firstWord;

        /* SESSION_END: no ack, just loop */
        if (mssimCmd == MSSIM_SESSION_END) {
            continue;
        }

        /* STOP: terminate server */
        if (mssimCmd == MSSIM_STOP) {
            UartSendAck();
            return;
        }

        /* Platform signals: ack and continue */
        if (mssimCmd == MSSIM_SIGNAL_POWER_ON) {
            ctx->powerOn = 1;
            UartSendAck();
            continue;
        }
        if (mssimCmd == MSSIM_SIGNAL_POWER_OFF) {
            ctx->powerOn = 0;
            ctx->wasStarted = 0;
            UartSendAck();
            continue;
        }
        if (mssimCmd == MSSIM_SIGNAL_RESET) {
            ctx->wasStarted = 0;
            UartSendAck();
            continue;
        }
        if (mssimCmd != MSSIM_SEND_COMMAND) {
            /* Unknown or other signal: just ack */
            UartSendAck();
            continue;
        }

        /* SEND_COMMAND: read locality(1) + cmdSize(4) + cmdPayload */
        if (UartRecv(&locality, 1) != 0) {
            continue;
        }
        if (UartRecv(hdr, 4) != 0) {
            continue;
        }
        cmdSize = LoadU32BE(hdr);

        if (cmdSize == 0 || cmdSize > FWTPM_MAX_COMMAND_SIZE) {
            /* Send zero-length response + ack */
            StoreU32BE(rspHdr, 0);
            UartSend(rspHdr, 4);
            UartSendAck();
            continue;
        }

        if (UartRecv(ctx->cmdBuf, cmdSize) != 0) {
            StoreU32BE(rspHdr, 0);
            UartSend(rspHdr, 4);
            UartSendAck();
            continue;
        }

        /* Process TPM command */
        rspSize = FWTPM_MAX_COMMAND_SIZE;
        FWTPM_ProcessCommand(ctx, ctx->cmdBuf, (int)cmdSize,
            ctx->rspBuf, &rspSize, (int)locality);

        /* Send mssim response: size(4) + payload + ack(4) */
        StoreU32BE(rspHdr, (uint32_t)rspSize);
        UartSend(rspHdr, 4);
        if (rspSize > 0) {
            UartSend(ctx->rspBuf, (uint32_t)rspSize);
        }
        UartSendAck();
    }
}

/* System Clock Configuration: 250MHz from 8MHz HSE via PLL */
static void SystemClock_Config(void)
{
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

    /* Voltage scaling for max frequency */
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE0);
    while (!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY)) {}

    /* HSE bypass (8MHz from ST-Link) + HSI48 for RNG + PLL */
    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI48 |
                                       RCC_OSCILLATORTYPE_HSE;
    RCC_OscInitStruct.HSEState = RCC_HSE_BYPASS_DIGITAL;
    RCC_OscInitStruct.HSI48State = RCC_HSI48_ON;
    RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource = RCC_PLL1_SOURCE_HSE;
    RCC_OscInitStruct.PLL.PLLM = 4;
    RCC_OscInitStruct.PLL.PLLN = 250;
    RCC_OscInitStruct.PLL.PLLP = 2;
    RCC_OscInitStruct.PLL.PLLQ = 2;
    RCC_OscInitStruct.PLL.PLLR = 2;
    RCC_OscInitStruct.PLL.PLLRGE = RCC_PLL1_VCIRANGE_1;
    RCC_OscInitStruct.PLL.PLLVCOSEL = RCC_PLL1_VCORANGE_WIDE;
    RCC_OscInitStruct.PLL.PLLFRACN = 0;
    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) {
        Error_Handler();
    }

    /* Bus clocks: all at 250MHz (no dividers) */
    RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK |
                                  RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2 |
                                  RCC_CLOCKTYPE_PCLK3;
    RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;
    RCC_ClkInitStruct.APB3CLKDivider = RCC_HCLK_DIV1;
    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_5) != HAL_OK) {
        Error_Handler();
    }

    __HAL_FLASH_SET_PROGRAM_DELAY(FLASH_PROGRAMMING_DELAY_2);
}

#if TZEN_ENABLED
/* TrustZone memory isolation: SRAM3 non-secure, GTZC interrupt */
static void SystemIsolation_Config(void)
{
    uint32_t index;
    MPCBB_ConfigTypeDef MPCBB_desc = {0};

    __HAL_RCC_GTZC1_CLK_ENABLE();

    /* Configure SRAM3 (0x20050000) as fully non-secure */
    MPCBB_desc.SecureRWIllegalMode = GTZC_MPCBB_SRWILADIS_ENABLE;
    MPCBB_desc.InvertSecureState = GTZC_MPCBB_INVSECSTATE_NOT_INVERTED;
    MPCBB_desc.AttributeConfig.MPCBB_LockConfig_array[0] = 0x00000000U;
    for (index = 0; index < 20; index++) {
        MPCBB_desc.AttributeConfig.MPCBB_SecConfig_array[index] = 0x00000000U;
        MPCBB_desc.AttributeConfig.MPCBB_PrivConfig_array[index] = 0xFFFFFFFFU;
    }
    if (HAL_GTZC_MPCBB_ConfigMem(SRAM3_BASE, &MPCBB_desc) != HAL_OK) {
        while (1) {}
    }

    /* Release GPIOB.0 (LED_GREEN) for non-secure */
    __HAL_RCC_GPIOB_CLK_ENABLE();
    HAL_GPIO_ConfigPinAttributes(GPIOB, GPIO_PIN_0, GPIO_PIN_NSEC);

    /* Clear and enable GTZC illegal access interrupts */
    HAL_GTZC_TZIC_ClearFlag(GTZC_PERIPH_ALL);
    HAL_GTZC_TZIC_EnableIT(GTZC_PERIPH_ALL);
    HAL_NVIC_SetPriority(GTZC_IRQn, 0, 0);
    HAL_NVIC_EnableIRQ(GTZC_IRQn);
}
#endif /* TZEN_ENABLED */

/* USART3: 115200 8N1 on PD8(TX)/PD9(RX) — ST-Link VCP */
static void MX_USART3_UART_Init(void)
{
    huart3.Instance = USART3;
    huart3.Init.BaudRate = 115200;
    huart3.Init.WordLength = UART_WORDLENGTH_8B;
    huart3.Init.StopBits = UART_STOPBITS_1;
    huart3.Init.Parity = UART_PARITY_NONE;
    huart3.Init.Mode = UART_MODE_TX_RX;
    huart3.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    huart3.Init.OverSampling = UART_OVERSAMPLING_16;
    huart3.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
    huart3.Init.ClockPrescaler = UART_PRESCALER_DIV1;
    huart3.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
    if (HAL_UART_Init(&huart3) != HAL_OK) {
        Error_Handler();
    }
}

/* Hardware RNG using HSI48 clock source */
static void MX_RNG_Init(void)
{
    hrng.Instance = RNG;
    hrng.Init.ClockErrorDetection = RNG_CED_ENABLE;
    if (HAL_RNG_Init(&hrng) != HAL_OK) {
        Error_Handler();
    }
}

static void Error_Handler(void)
{
    __disable_irq();
    while (1) {}
}

/* Newlib syscall stubs (not needed when using rdimon semihosting specs) */
#ifndef ARM_SEMIHOSTING
int _close(int fd) { (void)fd; return -1; }
int _lseek(int fd, int ptr, int dir) { (void)fd; (void)ptr; (void)dir; return 0; }
int _read(int fd, char* ptr, int len) { (void)fd; (void)ptr; (void)len; return 0; }
int _fstat(int fd, void* st) { (void)fd; (void)st; return 0; }
int _isatty(int fd) { (void)fd; return 1; }
void _exit(int status) { (void)status; while (1) {} }
int _getpid(void) { return 1; }
int _kill(int pid, int sig) { (void)pid; (void)sig; return -1; }
#endif

#ifndef ARM_SEMIHOSTING
void* _sbrk(int incr);
extern char _end; /* from linker script */
void* _sbrk(int incr)
{
    static char* heap_end = 0;
    char* prev_heap_end;
    if (heap_end == 0) {
        heap_end = &_end;
    }
    prev_heap_end = heap_end;
    heap_end += incr;
    return (void*)prev_heap_end;
}
#endif

#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t* file, uint32_t line)
{
    (void)file;
    (void)line;
    Error_Handler();
}
#endif
