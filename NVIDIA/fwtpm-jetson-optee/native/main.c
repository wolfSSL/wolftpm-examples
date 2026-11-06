/* Native dry run for the fwTPM-in-OP-TEE port.
 *
 * Builds the same wolfCrypt and fwTPM source list, with the same
 * user_settings.h, that the trusted application uses, and drives
 * FWTPM_ProcessCommand through a RAM NV HAL. Its only job is to find source
 * list, configuration and NV HAL problems off-board.
 */

#include <wolftpm/tpm2_types.h>
#include <wolftpm/fwtpm/fwtpm.h>
#include <wolftpm/fwtpm/fwtpm_nv.h>
#include <wolftpm/fwtpm/fwtpm_command.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The heap needs no shim: this host build does not define WOLFSSL_OPTEE, so
 * wolfCrypt uses its normal allocator, and the trusted application gets
 * TEE_Malloc from wolfSSL. Only the RNG needs standing in for, because
 * NO_FILESYSTEM closes the host seed path. */

int fwtpm_native_rand(unsigned char* out, unsigned int sz)
{
    FILE* f;
    size_t rd;

    f = fopen("/dev/urandom", "rb");
    if (f == NULL) {
        return -1;
    }
    rd = fread(out, 1, sz, f);
    fclose(f);
    return (rd == (size_t)sz) ? 0 : -1;
}

/* --- RAM NV HAL, standing in for OP-TEE secure storage --- */

#define NV_SIZE (64 * 1024)
static unsigned char nvRam[NV_SIZE];

static int NvRead(void* ctx, word32 offset, byte* buf, word32 size)
{
    (void)ctx;
    if ((word32)offset + size > NV_SIZE) {
        return -1;
    }
    memcpy(buf, &nvRam[offset], size);
    return 0;
}

static int NvWrite(void* ctx, word32 offset, const byte* buf, word32 size)
{
    (void)ctx;
    if ((word32)offset + size > NV_SIZE) {
        return -1;
    }
    memcpy(&nvRam[offset], buf, size);
    return 0;
}

static int NvErase(void* ctx, word32 offset, word32 size)
{
    (void)ctx;
    if ((word32)offset + size > NV_SIZE) {
        return -1;
    }
    memset(&nvRam[offset], 0xFF, size);
    return 0;
}

static int NvIntegrityKey(void* ctx, byte* key, word32* keySz)
{
    word32 i;

    (void)ctx;
    if (*keySz < 32) {
        return -1;
    }
    /* Fixed key: the dry run only needs the journal MAC to be consistent.
     * The trusted application derives this from the hardware unique key. */
    for (i = 0; i < 32; i++) {
        key[i] = (byte)(0x5A ^ i);
    }
    *keySz = 32;
    return 0;
}

/* --- Command helpers --- */

static FWTPM_CTX fwCtx;
static byte rspBuf[FWTPM_MAX_COMMAND_SIZE];

static int RunCmd(const char* name, const byte* cmd, int cmdSize)
{
    int rc;
    int rspSize = (int)sizeof(rspBuf);
    word32 tpmRc = 0xFFFFFFFF;

    rc = FWTPM_ProcessCommand(&fwCtx, cmd, cmdSize, rspBuf, &rspSize, 0);
    if (rc != 0) {
        printf("%-28s transport rc %d\n", name, rc);
        return -1;
    }
    if (rspSize < 10) {
        printf("%-28s short response %d\n", name, rspSize);
        return -1;
    }
    tpmRc = ((word32)rspBuf[6] << 24) | ((word32)rspBuf[7] << 16) |
            ((word32)rspBuf[8] << 8)  | (word32)rspBuf[9];
    printf("%-28s rsp %3d bytes, TPM_RC 0x%08X %s\n", name, rspSize,
        (unsigned int)tpmRc, (tpmRc == 0) ? "OK" : "FAIL");
    return (tpmRc == 0) ? 0 : -1;
}

static const byte cmdStartup[] = {
    0x80, 0x01,                 /* TPM_ST_NO_SESSIONS */
    0x00, 0x00, 0x00, 0x0C,     /* size 12 */
    0x00, 0x00, 0x01, 0x44,     /* TPM_CC_Startup */
    0x00, 0x00                  /* TPM_SU_CLEAR */
};

static const byte cmdGetRandom[] = {
    0x80, 0x01,
    0x00, 0x00, 0x00, 0x0C,     /* size 12 */
    0x00, 0x00, 0x01, 0x7B,     /* TPM_CC_GetRandom */
    0x00, 0x20                  /* 32 bytes requested */
};

static const byte cmdGetCapability[] = {
    0x80, 0x01,
    0x00, 0x00, 0x00, 0x16,     /* size 22 */
    0x00, 0x00, 0x01, 0x7A,     /* TPM_CC_GetCapability */
    0x00, 0x00, 0x00, 0x06,     /* TPM_CAP_TPM_PROPERTIES */
    0x00, 0x00, 0x01, 0x05,     /* TPM_PT_MANUFACTURER */
    0x00, 0x00, 0x00, 0x01      /* propertyCount 1 */
};

static const byte cmdSelfTest[] = {
    0x80, 0x01,
    0x00, 0x00, 0x00, 0x0B,     /* size 11 */
    0x00, 0x00, 0x01, 0x43,     /* TPM_CC_SelfTest */
    0x00                        /* fullTest = NO */
};

int main(void)
{
    FWTPM_NV_HAL nvHal;
    int rc;
    int fails = 0;

    memset(nvRam, 0xFF, sizeof(nvRam));
    memset(&fwCtx, 0, sizeof(fwCtx));
    memset(&nvHal, 0, sizeof(nvHal));

    nvHal.read = NvRead;
    nvHal.write = NvWrite;
    nvHal.erase = NvErase;
    nvHal.get_integrity_key = NvIntegrityKey;
    nvHal.maxSize = NV_SIZE;
    nvHal.ctx = NULL;

    rc = FWTPM_NV_SetHAL(&fwCtx, &nvHal);
    if (rc != 0) {
        printf("FWTPM_NV_SetHAL failed %d\n", rc);
        return 1;
    }

    rc = FWTPM_Init(&fwCtx);
    if (rc != 0) {
        printf("FWTPM_Init failed %d\n", rc);
        return 1;
    }
    printf("FWTPM_Init OK, sizeof(FWTPM_CTX) = %u bytes\n",
        (unsigned int)sizeof(FWTPM_CTX));

    fails += (RunCmd("TPM2_Startup", cmdStartup,
        (int)sizeof(cmdStartup)) != 0);
    fails += (RunCmd("TPM2_SelfTest", cmdSelfTest,
        (int)sizeof(cmdSelfTest)) != 0);
    fails += (RunCmd("TPM2_GetCapability", cmdGetCapability,
        (int)sizeof(cmdGetCapability)) != 0);
    fails += (RunCmd("TPM2_GetRandom", cmdGetRandom,
        (int)sizeof(cmdGetRandom)) != 0);

    /* Negative control: a command code the fwTPM does not implement must be
     * refused rather than silently succeed. */
    {
        byte bad[12];
        memcpy(bad, cmdGetRandom, sizeof(bad));
        bad[8] = 0xFF;
        bad[9] = 0xFF;
        if (RunCmd("bogus command code", bad, (int)sizeof(bad)) == 0) {
            printf("  negative control FAILED: bogus command accepted\n");
            fails++;
        }
        else {
            printf("  negative control OK: bogus command refused\n");
        }
    }

    FWTPM_Cleanup(&fwCtx);

    printf("\n%s\n", (fails == 0) ? "native dry run PASSED"
                                  : "native dry run FAILED");
    return (fails == 0) ? 0 : 1;
}
