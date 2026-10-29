include $(sub-dir)/../srcs.mk

# All four are symlinks created by prep.sh. The OP-TEE TA build resolves these
# against this directory, so the real trees cannot be named here.
#
# inc-cfg is this port directory and holds user_settings.h.
global-incdirs-y += inc-cfg
global-incdirs-y += inc-wolfssl
global-incdirs-y += inc-wolftpm
global-incdirs-y += port

# One user_settings.h configures both, so neither falls back to a generated
# options.h. Without WOLFTPM_USER_SETTINGS, wolfTPM would include
# <wolftpm/options.h> and silently pick up an installed one.
cflags-y += -DWOLFSSL_USER_SETTINGS
cflags-y += -DWOLFTPM_USER_SETTINGS
# Required: the TA toolchain is a Linux cross compiler and predefines
# __unix__, so wolfCrypt has to be told what it is really building for.
cflags-y += -DWOLFSSL_OPTEE

# The devkit names no architecture, so the compiler defaults to plain armv8-a
# and the assembler rejects the AES and SHA-2 crypto extensions and the
# SQRDMLSH the ML-KEM NTT uses, which came in FEAT_RDM. The Cortex-A78AE on a
# Jetson Orin is ARMv8.2-A and has all of them. A target without them wants
# this line removed and WOLFSSL_ARMASM dropped from user_settings.h.
cflags-y += -march=armv8.2-a+crypto

# The devkit warning set is stricter than either upstream tree builds with.
cflags-y += -Wno-unused-parameter -Wno-declaration-after-statement
cflags-y += -Wno-switch-default -Wno-redundant-decls -Wno-undef
cflags-y += -Wno-missing-declarations -Wno-missing-prototypes
cflags-y += -Wno-shadow -Wno-cast-align -Wno-float-equal

# OP-TEE trusted application entry points and the secure storage NV HAL, from
# wolfTPM: TEE Internal Core API only, so nothing in them is specific to this
# board. The wolfCrypt heap and RNG mapping comes from wolfSSL itself.
srcs-y += port/fwtpm_optee_ta.c
srcs-y += port/fwtpm_optee_nv.c

srcs-y += $(addprefix wt/,$(notdir $(FWTPM_SRCS)))
srcs-y += $(addprefix wc/,$(notdir $(WC_SRCS)))
srcs-y += $(addprefix wc/,$(notdir $(WC_SRCS_TA)))
