LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

inherit systemd

SYSTEMD_AUTO_ENABLE = "enable"
SYSTEMD_SERVICE:${PN} = "iotc-c-telemetry-demo.service"

SRC_URI = "file://iotc-c-telemetry-demo.service"

FILES:${PN} = "${systemd_unitdir}/system/iotc-c-telemetry-demo.service"

do_install() {
  install -d ${D}/${systemd_unitdir}/system
  install -m 0644 ${WORKDIR}/iotc-c-telemetry-demo.service ${D}/${systemd_unitdir}/system
}
