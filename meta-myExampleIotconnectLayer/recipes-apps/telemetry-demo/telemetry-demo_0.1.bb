SUMMARY = "Recipe that compiles and deploys a telemetry demo"

LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

DEPENDS += " iotc-c-sdk"
RDEPENDS:${PN} += " iotc-c-telemetry-demo-service"
PROVIDES = "${PN} ${PN}-dev"

SRC_URI = "file://cmke-src; \
file://eg-private-repo-data \
"

SRCREV_FORMAT="machine_meta"
SRCREV="${AUTOREV}"

S="${WORKDIR}/cmke-src"

inherit cmake

PACKAGES = "${PN} ${PN}-dev ${PN}-dbg ${PN}-staticdev"

APP_INSTALL_DIR = "${base_prefix}/usr/bin/local/iotc"
PRIVATE_DATA_DIR = "${base_prefix}/usr/local/iotc"

FILES:${PN}-dev = "${PRIVATE_DATA_DIR}/* \
"

cmake_do_generate_toolchain_file:append() {
	cat >> ${WORKDIR}/toolchain.cmake <<EOF
$cmake_crosscompiling

set( PC_CURL_LIBRARY_DIRS "${STAGING_LIBDIR}")
set( IOTC_C_LIB_INCLUDE_DIR  "${STAGING_BASELIBDIR}/iotc-c-lib/include")
set( IOTC_C_SDK_DIR  "${STAGING_DIR_TARGET}/iotc-generic-c-sdk")
set( IOTC_C_SDK_INCLUDE_DIR  "${STAGING_DIR_TARGET}/iotc-generic-c-sdk/include")
#find_package(CURL REQUIRED)
EOF
}


do_install() {
    install -d ${D}${APP_INSTALL_DIR}
    install -m 0755 telemetry-demo ${D}${APP_INSTALL_DIR}

    install -d ${D}${PRIVATE_DATA_DIR}
    for f in ${WORKDIR}/eg-private-repo-data/*
    do
        if [ -f $f ]; then
            install -m 0755 $f ${D}${PRIVATE_DATA_DIR}/
        fi
    done
}
