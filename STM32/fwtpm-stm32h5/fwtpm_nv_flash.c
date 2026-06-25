/* fwtpm_nv_flash.c
 *
 * STM32 internal flash NV HAL for the fwTPM append-only journal.
 *
 * With WOLFTPM_FWTPM_NV_APPEND_ONLY the fwTPM core buffers writes into program
 * granules and only ever calls write() with writeAlign-aligned, forward, into
 * erased bytes, so this HAL is a plain flash driver: read raw bytes, program
 * 16-byte quadwords, erase whole sectors. No read-modify-write in the port.
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

/* Physical flash base of the NV region. Under TrustZone the NV region is read
 * through the secure alias (0x0C......); the flash controller erase/program
 * operations use the non-aliased physical address. */
#if TZEN_ENABLED
    #define FWTPM_NV_FLASH_PHYS_BASE \
        (FWTPM_NV_FLASH_BASE - 0x0C000000 + 0x08000000)
#else
    #define FWTPM_NV_FLASH_PHYS_BASE  FWTPM_NV_FLASH_BASE
#endif

/* STM32H5 internal flash is memory-mapped and directly readable. */
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

/* Program flash. The append-only core guarantees offset and size are
 * FWTPM_NV_FLASH_PROGRAM_SIZE aligned and target erased cells, so this is a
 * plain quadword program. STM32H5 needs a 16-byte aligned source. Unaligned
 * offset or size is rejected: the loop programs whole quadwords, so an
 * unaligned size would program past (offset+size) into the next granule. */
static int StmFlashWrite(void* ctx, word32 offset, const byte* buf,
    word32 size)
{
    HAL_StatusTypeDef status = HAL_OK;
    word32 written = 0;
    byte alignBuf[FWTPM_NV_FLASH_PROGRAM_SIZE]
        __attribute__((aligned(FWTPM_NV_FLASH_PROGRAM_SIZE)));
    (void)ctx;

    if ((offset % FWTPM_NV_FLASH_PROGRAM_SIZE) != 0 ||
        (size % FWTPM_NV_FLASH_PROGRAM_SIZE) != 0) {
        return TPM_RC_FAILURE;
    }
    if (size > FWTPM_NV_FLASH_SIZE ||
        offset > FWTPM_NV_FLASH_SIZE - size) {
        return TPM_RC_FAILURE;
    }
    if (size == 0) {
        return TPM_RC_SUCCESS;
    }

    HAL_ICACHE_Disable();
    status = HAL_FLASH_Unlock();
    if (status != HAL_OK) {
        HAL_ICACHE_Invalidate();
        HAL_ICACHE_Enable();
        return TPM_RC_FAILURE;
    }

    while (written < size) {
        /* Size is granule-aligned (checked above), so every iteration
         * programs a full quadword copied to an aligned source buffer. */
        XMEMCPY(alignBuf, buf + written, FWTPM_NV_FLASH_PROGRAM_SIZE);
        status = HAL_FLASH_Program(FLASH_TYPEPROGRAM_QUADWORD,
            (uint32_t)(FWTPM_NV_FLASH_PHYS_BASE + offset + written),
            (uint32_t)(uintptr_t)alignBuf);
        if (status != HAL_OK) {
            break;
        }
        written += FWTPM_NV_FLASH_PROGRAM_SIZE;
    }

    HAL_FLASH_Lock();
    HAL_ICACHE_Invalidate();
    HAL_ICACHE_Enable();

    return (status == HAL_OK) ? TPM_RC_SUCCESS : TPM_RC_FAILURE;
}

/* Erase the whole sectors covering [offset, offset+size). Called by the core
 * at compaction (e.g. erase(0, maxSize)). */
static int StmFlashErase(void* ctx, word32 offset, word32 size)
{
    HAL_StatusTypeDef status;
    FLASH_EraseInitTypeDef eraseInit;
    uint32_t sectorError = 0;
    uint32_t physAddr;
    uint32_t bankBase;
    (void)ctx;

    /* Require sector-aligned offset and size (a partial-sector erase would
     * otherwise quietly wipe adjacent data). */
    if ((offset % FWTPM_NV_FLASH_SECTOR_SIZE) != 0 ||
        (size % FWTPM_NV_FLASH_SECTOR_SIZE) != 0 || size == 0) {
        return TPM_RC_FAILURE;
    }
    if (size > FWTPM_NV_FLASH_SIZE ||
        offset > FWTPM_NV_FLASH_SIZE - size) {
        return TPM_RC_FAILURE;
    }

    physAddr = FWTPM_NV_FLASH_PHYS_BASE + offset;

    HAL_ICACHE_Disable();
    status = HAL_FLASH_Unlock();
    if (status != HAL_OK) {
        HAL_ICACHE_Invalidate();
        HAL_ICACHE_Enable();
        return TPM_RC_FAILURE;
    }

    /* Bank 1 = 0x08000000-0x080FFFFF, Bank 2 = 0x08100000-0x081FFFFF. */
    if (physAddr >= 0x08100000) {
        bankBase = 0x08100000;
        eraseInit.Banks = FLASH_BANK_2;
    }
    else {
        bankBase = 0x08000000;
        eraseInit.Banks = FLASH_BANK_1;
    }
    eraseInit.TypeErase = FLASH_TYPEERASE_SECTORS;
    eraseInit.Sector = (physAddr - bankBase) / FWTPM_NV_FLASH_SECTOR_SIZE;
    eraseInit.NbSectors = size / FWTPM_NV_FLASH_SECTOR_SIZE;

    status = HAL_FLASHEx_Erase(&eraseInit, &sectorError);

    HAL_FLASH_Lock();
    HAL_ICACHE_Invalidate();
    HAL_ICACHE_Enable();

    return (status == HAL_OK) ? TPM_RC_SUCCESS : TPM_RC_FAILURE;
}

/* Populate the NV HAL. Append-only mode (built with
 * WOLFTPM_FWTPM_NV_APPEND_ONLY) lets the byte-granular journal run on
 * write-once flash: the core buffers program granules so write() stays a plain
 * flash program. */
int FWTPM_NV_FlashHAL_Init(FWTPM_NV_HAL* hal)
{
    if (hal == NULL) {
        return -1;
    }

    XMEMSET(hal, 0, sizeof(*hal));
    hal->read       = StmFlashRead;
    hal->write      = StmFlashWrite;
    hal->erase      = StmFlashErase;
    hal->ctx        = NULL;
    hal->maxSize    = FWTPM_NV_FLASH_SIZE;
    hal->writeAlign = FWTPM_NV_FLASH_PROGRAM_SIZE; /* 16-byte quadword */
    hal->appendOnly = 1;
    return 0;
}
