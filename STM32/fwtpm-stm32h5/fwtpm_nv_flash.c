/* fwtpm_nv_flash.c
 *
 * Generic STM32 internal flash NV HAL for fwTPM.
 * Uses STM32 HAL FLASH API - works across STM32 families (H5, L5, U5, etc.)
 *
 * Copyright (C) 2006-2025 wolfSSL Inc.
 *
 * This file is part of wolfTPM.
 */

#include "user_settings.h"
#include "stm32h5xx_hal.h"
#include <wolftpm/fwtpm/fwtpm.h>
#include <wolftpm/fwtpm/fwtpm_nv.h>
#include <string.h>

#ifndef FWTPM_NV_FLASH_BASE
    #error "Define FWTPM_NV_FLASH_BASE in user_settings.h"
#endif
#ifndef FWTPM_NV_FLASH_SIZE
    #define FWTPM_NV_FLASH_SIZE         (128 * 1024)
#endif
#ifndef FWTPM_NV_FLASH_SECTOR_SIZE
    #define FWTPM_NV_FLASH_SECTOR_SIZE  (8 * 1024)
#endif
#ifndef FWTPM_NV_FLASH_PROGRAM_SIZE
    #define FWTPM_NV_FLASH_PROGRAM_SIZE 16  /* quadword for H5 */
#endif

/* STM32H5 internal flash is memory-mapped and directly readable */
static int StmFlashRead(void* ctx, word32 offset, byte* buf, word32 size)
{
    volatile const byte* src;
    word32 i;
    (void)ctx;

    if (offset + size > FWTPM_NV_FLASH_SIZE) {
        return TPM_RC_FAILURE;
    }

    src = (volatile const byte*)(FWTPM_NV_FLASH_BASE + offset);
    for (i = 0; i < size; i++) {
        buf[i] = src[i];
    }
    return TPM_RC_SUCCESS;
}

/* Write to flash in FWTPM_NV_FLASH_PROGRAM_SIZE aligned chunks.
 * STM32H5 requires 128-bit (16-byte) aligned quadword writes. */
static int StmFlashWrite(void* ctx, word32 offset, const byte* buf,
    word32 size)
{
    HAL_StatusTypeDef status;
    uint32_t addr;
    word32 written = 0;
    byte alignBuf[FWTPM_NV_FLASH_PROGRAM_SIZE];
    word32 chunkSz;
    (void)ctx;

    if (offset + size > FWTPM_NV_FLASH_SIZE) {
        return TPM_RC_FAILURE;
    }

    /* Disable instruction cache during flash operations */
    HAL_ICACHE_Disable();

    status = HAL_FLASH_Unlock();
    if (status != HAL_OK) {
        HAL_ICACHE_Invalidate();
        HAL_ICACHE_Enable();
        return TPM_RC_FAILURE;
    }

    while (written < size) {
        addr = FWTPM_NV_FLASH_BASE + offset + written;
        chunkSz = size - written;

        if (chunkSz >= FWTPM_NV_FLASH_PROGRAM_SIZE) {
            /* Full quadword write */
            status = HAL_FLASH_Program(FLASH_TYPEPROGRAM_QUADWORD,
                addr, (uint32_t)(uintptr_t)(buf + written));
            written += FWTPM_NV_FLASH_PROGRAM_SIZE;
        }
        else {
            /* Partial final chunk: pad with 0xFF (erased state) */
            memset(alignBuf, 0xFF, sizeof(alignBuf));
            memcpy(alignBuf, buf + written, chunkSz);
            status = HAL_FLASH_Program(FLASH_TYPEPROGRAM_QUADWORD,
                addr, (uint32_t)(uintptr_t)alignBuf);
            written += chunkSz;
        }

        if (status != HAL_OK) {
            break;
        }
    }

    HAL_FLASH_Lock();
    HAL_ICACHE_Invalidate();
    HAL_ICACHE_Enable();

    return (status == HAL_OK) ? TPM_RC_SUCCESS : TPM_RC_FAILURE;
}

/* Erase flash sectors covering the NV region */
static int StmFlashErase(void* ctx, word32 offset, word32 size)
{
    HAL_StatusTypeDef status;
    FLASH_EraseInitTypeDef eraseInit;
    uint32_t sectorError = 0;
    uint32_t startSector;
    uint32_t numSectors;
    (void)ctx;
    (void)offset;
    (void)size;

    /* Calculate sector numbers based on absolute address.
     * Physical flash base is 0x08000000 regardless of TZ aliasing. */
#if TZEN_ENABLED
    startSector = (FWTPM_NV_FLASH_BASE - 0x0C000000) / FWTPM_NV_FLASH_SECTOR_SIZE;
#else
    startSector = (FWTPM_NV_FLASH_BASE - 0x08000000) / FWTPM_NV_FLASH_SECTOR_SIZE;
#endif
    numSectors = FWTPM_NV_FLASH_SIZE / FWTPM_NV_FLASH_SECTOR_SIZE;

    HAL_ICACHE_Disable();

    status = HAL_FLASH_Unlock();
    if (status != HAL_OK) {
        HAL_ICACHE_Invalidate();
        HAL_ICACHE_Enable();
        return TPM_RC_FAILURE;
    }

    eraseInit.TypeErase = FLASH_TYPEERASE_SECTORS;
    eraseInit.Banks = FLASH_BANK_1;
    eraseInit.Sector = startSector;
    eraseInit.NbSectors = numSectors;

    status = HAL_FLASHEx_Erase(&eraseInit, &sectorError);

    HAL_FLASH_Lock();
    HAL_ICACHE_Invalidate();
    HAL_ICACHE_Enable();

    return (status == HAL_OK) ? TPM_RC_SUCCESS : TPM_RC_FAILURE;
}

/* Initialize and populate the NV flash HAL struct */
int FWTPM_NV_FlashHAL_Init(FWTPM_NV_HAL* hal)
{
    if (hal == NULL) {
        return -1;
    }
    hal->read = StmFlashRead;
    hal->write = StmFlashWrite;
    hal->erase = StmFlashErase;
    hal->ctx = NULL;
    hal->maxSize = FWTPM_NV_FLASH_SIZE;
    return 0;
}
