include(vcpkg_common_functions)

vcpkg_check_linkage(ONLY_STATIC_LIBRARY)

set(_VCPKG_ROOT_DIR "${_VCPKG_ROOT_DIR}${VCPKG_ROOT_DIR}")

set(SOURCE_PATH ${_VCPKG_ROOT_DIR}/pdfium/pdfium/third_party/freetype/src)

vcpkg_configure_cmake(
  SOURCE_PATH ${SOURCE_PATH}
  PREFER_NINJA
)

vcpkg_install_cmake()

file(REMOVE_RECURSE ${CURRENT_PACKAGES_DIR}/debug/include)
file(REMOVE_RECURSE ${CURRENT_PACKAGES_DIR}/debug/lib/cmake)
file(REMOVE_RECURSE ${CURRENT_PACKAGES_DIR}/lib/cmake)

file(INSTALL ${SOURCE_PATH}/include DESTINATION "${CURRENT_PACKAGES_DIR}/")
file(INSTALL ${SOURCE_PATH}/../FTL.TXT DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}/" RENAME copyright)
