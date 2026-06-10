SUMMARY = "wolfTPM fwTPM Cortex-R5 firmware (lock-step)"
DESCRIPTION = "Stages the prebuilt fwtpm_r5.elf into /lib/firmware/ so \
Linux remoteproc can load it onto the ZCU102 RPU. Build the ELF first \
under firmware/fwtpm-r5/ (see ../firmware/fwtpm-r5/README), then run \
this recipe."
LICENSE = "GPL-3.0-or-later"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/GPL-3.0-or-later;md5=1c76c4cc354acaac30ed4d5eefea7245"

# The ELF is staged in files/ by the developer (or by a top-level Makefile).
SRC_URI = "file://fwtpm_r5.elf"

S = "${WORKDIR}"

do_compile() {
    :
}

do_install() {
    install -d ${D}${nonarch_base_libdir}/firmware
    install -m 0644 ${S}/fwtpm_r5.elf \
        ${D}${nonarch_base_libdir}/firmware/fwtpm_r5.elf
}

FILES:${PN} = "${nonarch_base_libdir}/firmware/fwtpm_r5.elf"

# This is an architecture-independent firmware blob.
INHIBIT_PACKAGE_STRIP = "1"
INHIBIT_PACKAGE_DEBUG_SPLIT = "1"
INSANE_SKIP:${PN} = "arch ldflags textrel staticdev"
