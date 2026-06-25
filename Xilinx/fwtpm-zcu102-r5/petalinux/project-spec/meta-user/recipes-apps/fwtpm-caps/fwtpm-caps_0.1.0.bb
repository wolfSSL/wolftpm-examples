SUMMARY = "wolfTPM fwTPM capabilities hello-world client"
DESCRIPTION = "Userspace client that opens the wolftpm rpmsg endpoint via \
/dev/rpmsg_ctrl0 and runs TPM2_Startup then a decoded GetCapability \
(family, spec revision, manufacturer, vendor string, firmware version) \
against the R5 fwTPM. The rpmsg-transport analogue of wolfTPM's \
examples/wrap/caps."
LICENSE = "GPL-3.0-or-later"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/GPL-3.0-or-later;md5=1c76c4cc354acaac30ed4d5eefea7245"

SRC_URI = "file://fwtpm_caps.c"

S = "${WORKDIR}"

do_compile() {
    ${CC} ${CFLAGS} ${LDFLAGS} \
        -O2 -Wall -Wextra \
        -o ${S}/fwtpm_caps \
        ${S}/fwtpm_caps.c
}

do_install() {
    install -d ${D}${bindir}
    install -m 0755 ${S}/fwtpm_caps ${D}${bindir}/fwtpm_caps
}

FILES:${PN} = "${bindir}/fwtpm_caps"
