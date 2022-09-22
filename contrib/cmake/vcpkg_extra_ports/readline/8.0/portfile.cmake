vcpkg_fail_port_install(
    MESSAGE "This port provides GNU readline for Mac only, on other platforms it is not applicable or system readline is adequate"
    ON_TARGET "WINDOWS" "UWP" "LINUX" "ANDROID" "FREEBSD" "OPENBSD"
)

set(READLINE_VERSION 8.0)

vcpkg_download_distfile(
    READLINE_TGZ
    URLS "https://ftp.gnu.org/gnu/readline/readline-${READLINE_VERSION}.tar.gz"
    FILENAME "readline-${READLINE_VERSION}.tar.gz"
    SHA512 41759d27bc3a258fefd7f4ff3277fa6ab9c21abb7b160e1a75aa8eba547bd90b288514e76264bd94fb0172da8a4faa54aab2c07b68a0356918ecf7f1969e866f
)

vcpkg_extract_source_archive_ex(
    OUT_SOURCE_PATH SOURCE_PATH
    ARCHIVE "${READLINE_TGZ}"
)

if(NOT "${TARGET_TRIPLET}" STREQUAL "${HOST_TRIPLET}")
    # Crosscompiling
    if(VCPKG_TARGET_ARCHITECTURE STREQUAL "arm64")
        set(BUILD_TRIPLET --host=aarch64-apple-darwin)
    endif()
endif()

vcpkg_configure_make(
    SOURCE_PATH ${SOURCE_PATH}
    BUILD_TRIPLET ${BUILD_TRIPLET}
    NO_ADDITIONAL_PATHS
)

vcpkg_install_make()

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/share")

file(INSTALL ${SOURCE_PATH}/COPYING DESTINATION ${CURRENT_PACKAGES_DIR}/share/readline RENAME copyright)
