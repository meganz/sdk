vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO yasm/yasm
    REF v1.3.0
    SHA512 f5053e2012e0d2ce88cc1cc06e3bdb501054aed5d1f78fae40bb3e676fe2eb9843d335a612d7614d99a2b9e49dca998d57f61b0b89fac8225afa4ae60ae848f1
)

if(VCPKG_LIBRARY_LINKAGE STREQUAL "static")
    set(_build_shared "0")
else()
    set(_build_shared "1")
endif()

vcpkg_configure_cmake(
    SOURCE_PATH ${SOURCE_PATH}
    PREFER_NINJA
    OPTIONS
        "-DBUILD_SHARED_LIBS=${_build_shared}"
        -DYASM_BUILD_TESTS=0
)

vcpkg_install_cmake()

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")

if(VCPKG_LIBRARY_LINKAGE STREQUAL "static")
    file(REMOVE_RECURSE 
        "${CURRENT_PACKAGES_DIR}/bin" 
        "${CURRENT_PACKAGES_DIR}/debug/bin"
    )
endif()

file(INSTALL ${SOURCE_PATH}/COPYING DESTINATION ${CURRENT_PACKAGES_DIR}/share/yasm RENAME copyright)