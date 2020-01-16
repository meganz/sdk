include(vcpkg_common_functions)

if(NOT VCPKG_CMAKE_SYSTEM_NAME OR VCPKG_CMAKE_SYSTEM_NAME STREQUAL "WindowsStore")
    vcpkg_check_linkage(ONLY_STATIC_LIBRARY)
endif()

vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO MediaArea/MediaInfoLib
    REF v19.09
    SHA512 2c1170234321793a576a7f60eecf4064bbf00654fe39184af86e97a66c54f3493a21c927a8baf709f8c925c55de7a169889bf72601cf4e5951ab6870c96793e4
    HEAD_REF master

	PATCHES add_config_options_to_setup_h.patch
)

vcpkg_configure_cmake(
    SOURCE_PATH ${SOURCE_PATH}/Project/CMake
    PREFER_NINJA
	
	OPTIONS -DBUILD_ZENLIB=0 -DBUILD_ZLIB=0
)

vcpkg_install_cmake()
vcpkg_fixup_cmake_targets(CONFIG_PATH share/mediainfolib TARGET_PATH share/mediainfolib)

file(REMOVE_RECURSE ${CURRENT_PACKAGES_DIR}/debug/include)
file(REMOVE_RECURSE ${CURRENT_PACKAGES_DIR}/lib/pkgconfig)
file(REMOVE_RECURSE ${CURRENT_PACKAGES_DIR}/debug/lib/pkgconfig)

file(INSTALL ${SOURCE_PATH}/LICENSE DESTINATION ${CURRENT_PACKAGES_DIR}/share/libmediainfo RENAME copyright)

vcpkg_test_cmake(PACKAGE_NAME MediaInfoLib MODULE)
