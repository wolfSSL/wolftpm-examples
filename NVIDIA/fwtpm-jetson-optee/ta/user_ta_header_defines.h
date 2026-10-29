#ifndef USER_TA_HEADER_DEFINES_H
#define USER_TA_HEADER_DEFINES_H

#include "uuid_macro.inc"
#include "ta_sizes.h"

/* Single instance: one trusted application instance is one TPM, since a
 * second would be a second TPM sharing one NV journal. Multi session so more
 * than one normal-world client can hold a session, as the kernel resource
 * manager does for a discrete part.
 *
 * Do not add TA_FLAG_DEVICE_ENUM_TEE_STORAGE_PRIVATE here to get onto the
 * Linux TEE bus. The devkit defines it, but the OP-TEE core shipped in L4T
 * R36.4.4 is older and rejects the unknown flag bit, so the trusted
 * application stops loading at all and every session open returns
 * TEEC_ERROR_BAD_FORMAT. The devkit being newer than the core is the trap. */
/* Keep alive, or OP-TEE destroys the instance when its last session closes
 * and the next client reaches a TPM that has not seen TPM2_Startup, which
 * lets it reset the PCRs without a reboot. The cost is that a newly installed
 * binary takes effect only after a reboot, so FWTPM_TA_NO_KEEP_ALIVE exists
 * for iterating on the port and for sweep.sh, never for deployment. */
#ifdef FWTPM_TA_NO_KEEP_ALIVE
    #define FWTPM_TA_KEEP_ALIVE 0
#else
    #define FWTPM_TA_KEEP_ALIVE TA_FLAG_INSTANCE_KEEP_ALIVE
#endif

#define TA_FLAGS            (TA_FLAG_EXEC_DDR | TA_FLAG_SINGLE_INSTANCE | \
                             TA_FLAG_MULTI_SESSION | FWTPM_TA_KEEP_ALIVE)

#define TA_CURRENT_TA_EXT_PROPERTIES \
    { "gp.ta.description", USER_TA_PROP_TYPE_STRING, "wolfTPM fwTPM" }

#endif /* USER_TA_HEADER_DEFINES_H */
