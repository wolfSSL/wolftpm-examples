/* Normal-world client for the fwTPM trusted application.
 *
 * Minimal libteec driver used to bring the TA up on the board. The real
 * client is the WOLFTPM_OPTEE transport in wolfTPM, which lets unmodified
 * wolfTPM examples drive the same TA.
 *
 * Build and run on the board:
 *   make && sudo ./fwtpm_client
 */

#include <err.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <tee_client_api.h>

#include <wolftpm/fwtpm/fwtpm_optee.h>

static const TEEC_UUID taUuid = {
    0x9013be0e, 0x39b0, 0x4ed4,
    { 0x80, 0xe0, 0x12, 0x42, 0xc0, 0xda, 0xf3, 0x82 }
};

static TEEC_Context ctx;
static TEEC_Session sess;
static int fails;

static unsigned int BeU32(const unsigned char* p)
{
    return ((unsigned int)p[0] << 24) | ((unsigned int)p[1] << 16) |
           ((unsigned int)p[2] << 8) | (unsigned int)p[3];
}

/* Returns the TPM response code, or 0xFFFFFFFF if the invoke itself failed. */
static unsigned int Submit(const char* name, const unsigned char* cmd,
    size_t cmdSz, unsigned char* rsp, size_t rspCap, size_t* rspSz)
{
    TEEC_Operation op;
    TEEC_Result res;
    uint32_t origin = 0;
    unsigned int rc;

    memset(&op, 0, sizeof(op));
    op.paramTypes = TEEC_PARAM_TYPES(TEEC_MEMREF_TEMP_INPUT,
                                     TEEC_MEMREF_TEMP_OUTPUT,
                                     TEEC_NONE, TEEC_NONE);
    op.params[0].tmpref.buffer = (void*)cmd;
    op.params[0].tmpref.size = cmdSz;
    op.params[1].tmpref.buffer = rsp;
    op.params[1].tmpref.size = rspCap;

    res = TEEC_InvokeCommand(&sess, FWTPM_OPTEE_CMD_SUBMIT, &op, &origin);
    if (res != TEEC_SUCCESS) {
        printf("%-28s TEEC 0x%08x origin %u\n", name, res, origin);
        *rspSz = 0;
        return 0xFFFFFFFFu;
    }
    *rspSz = op.params[1].tmpref.size;
    if (*rspSz < 10) {
        printf("%-28s short response %zu\n", name, *rspSz);
        return 0xFFFFFFFFu;
    }
    rc = BeU32(rsp + 6);
    printf("%-28s rsp %3zu bytes, TPM_RC 0x%08X %s\n", name, *rspSz, rc,
        (rc == 0) ? "OK" : "FAIL");
    return rc;
}

static void Expect(const char* name, const unsigned char* cmd, size_t cmdSz)
{
    unsigned char rsp[8192];
    size_t rspSz = 0;

    if (Submit(name, cmd, cmdSz, rsp, sizeof(rsp), &rspSz) != 0) {
        fails++;
    }
}

static const unsigned char cmdStartup[] = {
    0x80, 0x01, 0x00, 0x00, 0x00, 0x0C,
    0x00, 0x00, 0x01, 0x44,
    0x00, 0x00
};
static const unsigned char cmdSelfTest[] = {
    0x80, 0x01, 0x00, 0x00, 0x00, 0x0B,
    0x00, 0x00, 0x01, 0x43,
    0x00
};
static const unsigned char cmdGetRandom[] = {
    0x80, 0x01, 0x00, 0x00, 0x00, 0x0C,
    0x00, 0x00, 0x01, 0x7B,
    0x00, 0x20
};
/* TPM_CAP_TPM_PROPERTIES, TPM_PT_MANUFACTURER, one property */
static const unsigned char cmdGetCapMfg[] = {
    0x80, 0x01, 0x00, 0x00, 0x00, 0x16,
    0x00, 0x00, 0x01, 0x7A,
    0x00, 0x00, 0x00, 0x06,
    0x00, 0x00, 0x01, 0x05,
    0x00, 0x00, 0x00, 0x01
};
/* TPM_PT_REVISION: the reason the vendor fTPM has no post-quantum commands */
static const unsigned char cmdGetCapRev[] = {
    0x80, 0x01, 0x00, 0x00, 0x00, 0x16,
    0x00, 0x00, 0x01, 0x7A,
    0x00, 0x00, 0x00, 0x06,
    0x00, 0x00, 0x01, 0x02,
    0x00, 0x00, 0x00, 0x01
};

int main(void)
{
    TEEC_Operation op;
    TEEC_Result res;
    unsigned char rsp[8192];
    unsigned char bad[12];
    size_t rspSz = 0;
    uint32_t origin = 0;
    unsigned int rc;
    unsigned int i;

    res = TEEC_InitializeContext(NULL, &ctx);
    if (res != TEEC_SUCCESS) {
        errx(1, "TEEC_InitializeContext 0x%08x", res);
    }
    res = TEEC_OpenSession(&ctx, &sess, &taUuid, TEEC_LOGIN_PUBLIC, NULL,
        NULL, &origin);
    if (res != TEEC_SUCCESS) {
        TEEC_FinalizeContext(&ctx);
        errx(1, "TEEC_OpenSession 0x%08x origin %u", res, origin);
    }
    printf("session open\n");

    memset(&op, 0, sizeof(op));
    op.paramTypes = TEEC_PARAM_TYPES(TEEC_VALUE_OUTPUT, TEEC_NONE,
                                     TEEC_NONE, TEEC_NONE);
    res = TEEC_InvokeCommand(&sess, FWTPM_OPTEE_CMD_GET_INFO, &op, &origin);
    if (res != TEEC_SUCCESS) {
        printf("GET_INFO failed 0x%08x origin %u\n", res, origin);
        fails++;
    }
    else {
        printf("ABI version %u, max command size %u\n",
            op.params[0].value.a, op.params[0].value.b);
        if (op.params[0].value.a != FWTPM_OPTEE_ABI_VERSION) {
            printf("  ABI mismatch, client built for %u\n",
                FWTPM_OPTEE_ABI_VERSION);
            fails++;
        }
    }

    Expect("TPM2_Startup", cmdStartup, sizeof(cmdStartup));
    Expect("TPM2_SelfTest", cmdSelfTest, sizeof(cmdSelfTest));

    rc = Submit("TPM2_GetCapability(mfg)", cmdGetCapMfg, sizeof(cmdGetCapMfg),
        rsp, sizeof(rsp), &rspSz);
    if (rc != 0) {
        fails++;
    }
    else if (rspSz >= 27) {
        /* tag(2) size(4) rc(4) more(1) cap(4) count(4) property(4) value(4) */
        printf("  TPM_PT_MANUFACTURER = 0x%08X \"%.4s\"\n",
            BeU32(rsp + 23), (const char*)(rsp + 23));
    }

    rc = Submit("TPM2_GetCapability(rev)", cmdGetCapRev, sizeof(cmdGetCapRev),
        rsp, sizeof(rsp), &rspSz);
    if (rc != 0) {
        fails++;
    }
    else if (rspSz >= 27) {
        printf("  TPM_PT_REVISION = %u (spec %u.%02u)\n",
            BeU32(rsp + 23), BeU32(rsp + 23) / 100, BeU32(rsp + 23) % 100);
    }

    rc = Submit("TPM2_GetRandom", cmdGetRandom, sizeof(cmdGetRandom),
        rsp, sizeof(rsp), &rspSz);
    if (rc != 0) {
        fails++;
    }
    else if (rspSz >= 44) {
        printf("  32 random bytes:");
        for (i = 0; i < 8; i++) {
            printf(" %02x", rsp[12 + i]);
        }
        printf(" ...\n");
    }

    /* Negative control: an unimplemented command code must be refused, or a
     * TPM that answered everything with success would look identical. */
    memcpy(bad, cmdGetRandom, sizeof(bad));
    bad[8] = 0xFF;
    bad[9] = 0xFF;
    rc = Submit("bogus command code", bad, sizeof(bad), rsp, sizeof(rsp),
        &rspSz);
    if (rc == 0) {
        printf("  negative control FAILED: bogus command accepted\n");
        fails++;
    }
    else {
        printf("  negative control OK: refused with 0x%08X\n", rc);
    }

    /* Negative control: a short output buffer must report the size needed
     * rather than truncate the response. */
    memset(&op, 0, sizeof(op));
    op.paramTypes = TEEC_PARAM_TYPES(TEEC_MEMREF_TEMP_INPUT,
                                     TEEC_MEMREF_TEMP_OUTPUT,
                                     TEEC_NONE, TEEC_NONE);
    op.params[0].tmpref.buffer = (void*)cmdGetRandom;
    op.params[0].tmpref.size = sizeof(cmdGetRandom);
    op.params[1].tmpref.buffer = rsp;
    op.params[1].tmpref.size = 4;
    res = TEEC_InvokeCommand(&sess, FWTPM_OPTEE_CMD_SUBMIT, &op, &origin);
    if (res == TEEC_ERROR_SHORT_BUFFER) {
        printf("%-28s OK: needs %zu bytes\n", "short output buffer",
            op.params[1].tmpref.size);
    }
    else {
        printf("%-28s FAILED: 0x%08x\n", "short output buffer", res);
        fails++;
    }

    res = TEEC_InvokeCommand(&sess, FWTPM_OPTEE_CMD_RESET, NULL, &origin);
    printf("%-28s %s\n", "CMD_RESET",
        (res == TEEC_SUCCESS) ? "OK" : "FAIL");
    if (res != TEEC_SUCCESS) {
        fails++;
    }

    TEEC_CloseSession(&sess);
    TEEC_FinalizeContext(&ctx);

    printf("\n%s\n", (fails == 0) ? "fwTPM TA checks PASSED"
                                  : "fwTPM TA checks FAILED");
    return (fails == 0) ? 0 : 1;
}
