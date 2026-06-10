SUMMARY = "wolfTPM fwTPM rpmsg smoke-test client"
DESCRIPTION = "Userspace test that opens the wolftpm rpmsg endpoint via \
/dev/rpmsg_ctrl0 and runs TPM2_Startup, SelfTest, GetRandom(32), and \
GetCapability(manufacturer) against the R5 fwTPM."
LICENSE = "GPL-3.0-or-later"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/GPL-3.0-or-later;md5=1c76c4cc354acaac30ed4d5eefea7245"

SRC_URI = "file://fwtpm_rpmsg_test.c"

S = "${WORKDIR}"

do_compile() {
    ${CC} ${CFLAGS} ${LDFLAGS} \
        -O2 -Wall -Wextra \
        -o ${S}/fwtpm_rpmsg_test \
        ${S}/fwtpm_rpmsg_test.c
}

do_install() {
    install -d ${D}${bindir}
    install -m 0755 ${S}/fwtpm_rpmsg_test ${D}${bindir}/fwtpm_rpmsg_test
}

FILES:${PN} = "${bindir}/fwtpm_rpmsg_test"
