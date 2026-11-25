# SPDX-License-Identifier: GPL-3.0-only
# Install layout and Debian package for the CardputerZero store.
#
# Everything that lands in a shared directory is named after the package, as
# the store requires.

set(MESHZERO_MAINTAINER "Pedro Santos <8346634+s0liton@users.noreply.github.com>" CACHE STRING "Debian package maintainer")
set(MESHZERO_RADIO_BINARY "${CMAKE_CURRENT_SOURCE_DIR}/third_party/meshtasticd/arm64/meshtasticd"
    CACHE FILEPATH "Radio daemon binary to ship (see packaging/fetch-radio.sh)")
option(MESHZERO_REQUIRE_RADIO "Fail when the radio daemon binary is missing" OFF)

install(TARGETS meshzerod RUNTIME DESTINATION /usr/bin)
if(TARGET meshzero)
    install(TARGETS meshzero RUNTIME DESTINATION /usr/bin)
    # TrueType fonts for characters outside the built-in ASCII set.
    install(FILES
        "${MESHZERO_FONT_SOURCE_DIR}/Montserrat-Medium.ttf"
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/Montserrat-Bold.ttf"
        "${MESHZERO_FONT_SOURCE_DIR}/DejaVuSans.ttf"
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/NotoEmoji-Medium.ttf"
        DESTINATION /usr/share/meshzero/fonts)
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/Montserrat-OFL.txt"
        DESTINATION /usr/share/doc/meshzero/font-licenses/Montserrat RENAME MeshCard-OFL.txt)
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/NotoEmoji-OFL.txt"
        DESTINATION /usr/share/doc/meshzero/font-licenses/NotoEmoji RENAME OFL.txt)
    install(DIRECTORY
        "${MESHZERO_FONT_SOURCE_DIR}/font_license/Montserrat"
        "${MESHZERO_FONT_SOURCE_DIR}/font_license/DejaVuSans"
        "${MESHZERO_FONT_SOURCE_DIR}/font_license/FontAwesome5"
        DESTINATION /usr/share/doc/meshzero/font-licenses)
endif()

if(EXISTS "${MESHZERO_RADIO_BINARY}")
    install(PROGRAMS "${MESHZERO_RADIO_BINARY}" DESTINATION /usr/lib/meshzero)
    get_filename_component(_radio_dir "${MESHZERO_RADIO_BINARY}" DIRECTORY)
    install(FILES "${_radio_dir}/VERSION" DESTINATION /usr/lib/meshzero RENAME meshtasticd.version OPTIONAL)
elseif(MESHZERO_REQUIRE_RADIO)
    message(FATAL_ERROR "Radio daemon not found at ${MESHZERO_RADIO_BINARY}. Run packaging/fetch-radio.sh first.")
else()
    message(STATUS "Radio daemon binary not found; the package will not include it")
endif()

install(PROGRAMS packaging/radio-start DESTINATION /usr/lib/meshzero)
install(FILES packaging/radio.yaml packaging/meshzero.conf DESTINATION /etc/meshzero)
install(FILES packaging/meshzero.service DESTINATION /usr/lib/systemd/system)
install(FILES packaging/tmpfiles.conf DESTINATION /usr/lib/tmpfiles.d RENAME meshzero.conf)
install(FILES packaging/meshzero.desktop DESTINATION /usr/share/APPLaunch/applications)
install(FILES assets/meshzero.png DESTINATION /usr/share/APPLaunch/share/images)
install(FILES README.md LICENSE THIRD_PARTY_NOTICES.md DESTINATION /usr/share/doc/meshzero)

set(CPACK_GENERATOR "DEB")
set(CPACK_SET_DESTDIR ON)
set(CPACK_INSTALL_PREFIX "/usr")
set(CPACK_OUTPUT_FILE_PREFIX "${CMAKE_CURRENT_SOURCE_DIR}/dist")
set(CPACK_PACKAGE_NAME "meshzero")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_CONTACT "${MESHZERO_MAINTAINER}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Meshtastic client for the CardputerZero and Cap LoRa-1262")
set(CPACK_PACKAGE_DESCRIPTION
"Chat, direct messages, node list, map and radio settings for a CardputerZero
with the Cap LoRa-1262. The Meshtastic Linux daemon runs in the background as
the radio, and the Meshtastic TCP API is offered to several clients at once so
that tools such as MeshMonitor can use it over Wi-Fi.")
set(CPACK_PACKAGE_FILE_NAME "meshzero_${PROJECT_VERSION}_arm64")
set(CPACK_DEBIAN_PACKAGE_NAME "meshzero")
set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "arm64")
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "${MESHZERO_MAINTAINER}")
set(CPACK_DEBIAN_PACKAGE_SECTION "hamradio")
set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
set(CPACK_DEBIAN_PACKAGE_HOMEPAGE "https://github.com/s0liton/MeshZero")
# The first line covers MeshZero's own programs; the rest is what the bundled
# radio daemon links against (taken from its upstream Debian 13 package).
set(CPACK_DEBIAN_PACKAGE_DEPENDS
    "libc6 (>= 2.38), libstdc++6 (>= 11), libgcc-s1, libsystemd0, adduser, libgpiod3 (>= 2.1), libi2c0 (>= 4.0), libinput10, liborcania2.3, libsdl2-2.0-0, libssl3t64, libulfius2.7t64, libusb-1.0-0, libuv1t64, libx11-6, libxkbcommon0, libyaml-cpp0.8")
# Scanning QR codes with the camera uses rpicam-vid when it is there.
set(CPACK_DEBIAN_PACKAGE_SUGGESTS "rpicam-apps | rpicam-apps-lite")
# Looking for a newer release uses curl when it is there.
set(CPACK_DEBIAN_PACKAGE_RECOMMENDS "curl, ca-certificates")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS OFF)
set(CPACK_DEBIAN_PACKAGE_CONTROL_STRICT_PERMISSION TRUE)
set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA
    "${CMAKE_CURRENT_SOURCE_DIR}/packaging/conffiles"
    "${CMAKE_CURRENT_SOURCE_DIR}/packaging/postinst"
    "${CMAKE_CURRENT_SOURCE_DIR}/packaging/prerm"
    "${CMAKE_CURRENT_SOURCE_DIR}/packaging/postrm")
set(CPACK_STRIP_FILES OFF)

include(CPack)
