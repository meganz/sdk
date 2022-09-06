#include(vcpkg_common_functions)

get_filename_component(GIT_PATH ${GIT} DIRECTORY)

vcpkg_from_git(
    OUT_SOURCE_PATH SOURCE_PATH
    URL https://chromium.googlesource.com/chromium/src/third_party/freetype2.git
    REF 2db58e061ecc0d738a41d13ed8908e967bd0014c # The one in pdfium DEPS file, field 'freetype_revision'
)

vcpkg_check_linkage(ONLY_STATIC_LIBRARY)

vcpkg_configure_cmake(
  SOURCE_PATH ${SOURCE_PATH}
  PREFER_NINJA
)

vcpkg_install_cmake()

file(REMOVE_RECURSE ${CURRENT_PACKAGES_DIR}/debug/include)
file(REMOVE_RECURSE ${CURRENT_PACKAGES_DIR}/debug/lib/cmake)
file(REMOVE_RECURSE ${CURRENT_PACKAGES_DIR}/lib/cmake)

file(INSTALL ${SOURCE_PATH}/include DESTINATION "${CURRENT_PACKAGES_DIR}/")
file(INSTALL ${SOURCE_PATH}/docs/FTL.TXT DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}/" RENAME copyright)
