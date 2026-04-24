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

    /* Overflow-safe bounds check: reject oversized size first, then the
     * offset, without forming offset+size which can wrap for large inputs. */
    if (size > FWTPM_NV_FLASH_SIZE ||
        offset > FWTPM_NV_FLASH_SIZE - size) {
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
    /* STM32H5 quadword programming requires the source pointer to be
     * 16-byte aligned. Default GCC stack alignment for byte[] is 8. */
    byte alignBuf[FWTPM_NV_FLASH_PROGRAM_SIZE]
        __attribute__((aligned(FWTPM_NV_FLASH_PROGRAM_SIZE)));
    word32 chunkSz;
    (void)ctx;

    /* Destination address must also be quadword-aligned. */
    if ((offset % FWTPM_NV_FLASH_PROGRAM_SIZE) != 0) {
        return TPM_RC_FAILURE;
    }

    /* Overflow-safe bounds check (see StmFlashRead). */
    if (size > FWTPM_NV_FLASH_SIZE ||
        offset > FWTPM_NV_FLASH_SIZE - size) {
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
            /* Full quadword write via aligned staging buffer */
            memcpy(alignBuf, buf + written, FWTPM_NV_FLASH_PROGRAM_SIZE);
            status = HAL_FLASH_Program(FLASH_TYPEPROGRAM_QUADWORD,
                addr, (uint32_t)(uintptr_t)alignBuf);
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

/* Erase flash sectors covering the requested [offset, offset+size) range.
 * Both offset and size must be whole multiples of the sector size. */
static int StmFlashErase(void* ctx, word32 offset, word32 size)
{
    HAL_StatusTypeDef status;
    FLASH_EraseInitTypeDef eraseInit;
    uint32_t sectorError = 0;
    uint32_t physAddr;
    uint32_t bankBase;
    uint32_t startSector;
    uint32_t numSectors;
    (void)ctx;

    /* Require sector-aligned offset and size (callers asking for a
     * partial-sector erase would otherwise quietly wipe adjacent data). */
    if ((offset % FWTPM_NV_FLASH_SECTOR_SIZE) != 0 ||
        (size % FWTPM_NV_FLASH_SECTOR_SIZE) != 0 || size == 0) {
        return TPM_RC_FAILURE;
    }

    /* Overflow-safe bounds check (see StmFlashRead). */
    if (size > FWTPM_NV_FLASH_SIZE ||
        offset > FWTPM_NV_FLASH_SIZE - size) {
        return TPM_RC_FAILURE;
    }

    /* Calculate physical flash address (remove TZ secure alias if present).
     * STM32H563: Bank 1 = 0x08000000-0x080FFFFF,
     *            Bank 2 = 0x08100000-0x081FFFFF */
#if TZEN_ENABLED
    physAddr = FWTPM_NV_FLASH_BASE - 0x0C000000 + 0x08000000;
#else
    physAddr = FWTPM_NV_FLASH_BASE;
#endif
    physAddr += offset;
    numSectors = size / FWTPM_NV_FLASH_SECTOR_SIZE;

    HAL_ICACHE_Disable();

    status = HAL_FLASH_Unlock();
    if (status != HAL_OK) {
        HAL_ICACHE_Invalidate();
        HAL_ICACHE_Enable();
        return TPM_RC_FAILURE;
    }

    /* Select bank and compute sector index relative to bank start */
    if (physAddr >= 0x08100000) {
        bankBase = 0x08100000;
        eraseInit.Banks = FLASH_BANK_2;
    }
    else {
        bankBase = 0x08000000;
        eraseInit.Banks = FLASH_BANK_1;
    }
    startSector = (physAddr - bankBase) / FWTPM_NV_FLASH_SECTOR_SIZE;

    eraseInit.TypeErase = FLASH_TYPEERASE_SECTORS;
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
