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
#define TA_FLAGS            (TA_FLAG_EXEC_DDR | TA_FLAG_SINGLE_INSTANCE | \
                             TA_FLAG_MULTI_SESSION)

#define TA_CURRENT_TA_EXT_PROPERTIES \
    { "gp.ta.description", USER_TA_PROP_TYPE_STRING, "wolfTPM fwTPM" }

#endif /* USER_TA_HEADER_DEFINES_H */
