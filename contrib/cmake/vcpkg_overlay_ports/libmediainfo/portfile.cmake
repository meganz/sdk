string(REGEX REPLACE "^([0-9]+)[.]([1-9])\$" "\\1.0\\2" MEDIAINFO_VERSION "${VERSION}")
vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO MediaArea/MediaInfoLib
    REF "v${MEDIAINFO_VERSION}"
    SHA512 6bc58f98ac1fc9637db0e8dd3a2e15b62036a2d33763e148cde425fceea798324f7c5f53cd51fc698d5b5b05fe1fc0fbfc4f391d2ec135eba6763eedfa5bd101
    HEAD_REF master
    PATCHES
        msvc-arm.diff
        dependencies.diff
        no-windows-namespace.diff
        include-definitions.patch
)

vcpkg_find_acquire_program(PKGCONFIG)

vcpkg_check_features(OUT_FEATURE_OPTIONS FEATURE_OPTIONS
	FEATURES
        minsize     MEDIAINFO_MINIMIZESIZE
        minimal     MEDIAINFO_MINIMAL_YES
        advanced    MEDIAINFO_ADVANCED_YES
        xml         MEDIAINFO_XML_YES
        id3v2       MEDIAINFO_ID3V2_YES
        mpega       MEDIAINFO_MPEGA_YES
    INVERTED_FEATURES
        text        MEDIAINFO_TEXT_NO
        image       MEDIAINFO_IMAGE_NO
        archive     MEDIAINFO_ARCHIVE_NO
        tag         MEDIAINFO_TAG_NO
)

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}/Project/CMake"
    OPTIONS
        -DBUILD_ZENLIB=0
        -DBUILD_ZLIB=0
        "-DPKG_CONFIG_EXECUTABLE=${PKGCONFIG}"
        -DCMAKE_REQUIRE_FIND_PACKAGE_PkgConfig=1
        -DCMAKE_REQUIRE_FIND_PACKAGE_TinyXML=1
        ${FEATURE_OPTIONS}
)

vcpkg_cmake_install()
vcpkg_cmake_config_fixup(PACKAGE_NAME mediainfolib)
vcpkg_fixup_pkgconfig()
if(NOT VCPKG_BUILD_TYPE AND VCPKG_TARGET_IS_WINDOWS)
    vcpkg_replace_string("${CURRENT_PACKAGES_DIR}/debug/lib/pkgconfig/libmediainfo.pc" " -lmediainfo" " -lmediainfod")
endif()

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")

vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")
