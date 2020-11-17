#include(vcpkg_common_functions)

vcpkg_check_linkage(ONLY_STATIC_LIBRARY)

set(_VCPKG_ROOT_DIR "${_VCPKG_ROOT_DIR}${VCPKG_ROOT_DIR}")
set(SOURCE_PATH ${_VCPKG_ROOT_DIR}/pdfium/pdfium)
set(CMAKE_INSTALL_BINDIR $SOURCE_PATH/vcpkg-built/bin)
set(CMAKE_INSTALL_LIBDIR $SOURCE_PATH/vcpkg-built/lib)


file(COPY ${CMAKE_CURRENT_LIST_DIR}/CMakeLists.txt DESTINATION ${SOURCE_PATH})

vcpkg_configure_cmake(
  SOURCE_PATH ${SOURCE_PATH}
  PREFER_NINJA
)

vcpkg_install_cmake()

file(INSTALL ${SOURCE_PATH}/public/ DESTINATION "${CURRENT_PACKAGES_DIR}/" RENAME "include")
file(INSTALL ${SOURCE_PATH}/LICENSE DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}/" RENAME copyright)
