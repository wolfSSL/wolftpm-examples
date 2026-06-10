FILESEXTRAPATHS:prepend := "${THISDIR}/${PN}:"

SRC_URI:append = " file://bsp.cfg"
KERNEL_FEATURES:append = " bsp.cfg"

# OpenAMP / RPMsg / remoteproc support for the R5 fwTPM.
SRC_URI:append = " file://openamp.cfg"
KERNEL_FEATURES:append = " openamp.cfg"
