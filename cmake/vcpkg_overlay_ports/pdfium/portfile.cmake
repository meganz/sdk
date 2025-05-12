
# From V8 port file in vcpkg repo: https://github.com/microsoft/vcpkg/blob/master/ports/v8/portfile.cmake
function(pdfium_from_git)
    set(pdfiumArgs DESTINATION URL REF SOURCE)
    cmake_parse_arguments(pdfium "" "${pdfiumArgs}" "" ${ARGN})

    if(EXISTS ${pdfium_SOURCE}/${pdfium_DESTINATION})
        vcpkg_execute_required_process(
            COMMAND ${GIT} reset --hard
            WORKING_DIRECTORY ${pdfium_SOURCE}/${pdfium_DESTINATION}
            LOGNAME build-${TARGET_TRIPLET})
    else()
        vcpkg_execute_required_process(
            COMMAND ${GIT} clone ${pdfium_URL} ${pdfium_DESTINATION}
            WORKING_DIRECTORY ${pdfium_SOURCE}
            LOGNAME build-${TARGET_TRIPLET})
        vcpkg_execute_required_process(
            COMMAND ${GIT} fetch --depth 1 origin ${pdfium_REF}
            WORKING_DIRECTORY ${pdfium_SOURCE}/${pdfium_DESTINATION}
            LOGNAME build-${TARGET_TRIPLET})
        vcpkg_execute_required_process(
            COMMAND ${GIT} checkout FETCH_HEAD
            WORKING_DIRECTORY ${pdfium_SOURCE}/${pdfium_DESTINATION}
            LOGNAME build-${TARGET_TRIPLET})
    endif()
endfunction()

vcpkg_find_acquire_program(GIT)

vcpkg_from_git(
    OUT_SOURCE_PATH SOURCE_PATH
    URL https://pdfium.googlesource.com/pdfium.git
    REF 7a8409531fbb58d7d15ae331e645977b113d7ced # chromium/6778
    PATCHES
)

message(STATUS "Working on submodules and other dependencies...")

pdfium_from_git(
    DESTINATION build
    URL https://chromium.googlesource.com/chromium/src/build.git
    REF 9b11bd3a6a523134ac35bcc9d1f59d04cc6f5821  # The one in pdfium DEPS file, field 'build_revision'
    SOURCE ${SOURCE_PATH}
)

pdfium_from_git(
    DESTINATION third_party/abseil-cpp
    URL https://chromium.googlesource.com/chromium/src/third_party/abseil-cpp.git
    REF d2ea9f0eb1a31f0e5a0ab11837ed19333700ab4c  # The one in pdfium DEPS file, field 'abseil_revision'
    SOURCE ${SOURCE_PATH}
)

pdfium_from_git(
    DESTINATION third_party/fast_float/src
    URL https://chromium.googlesource.com/external/github.com/fastfloat/fast_float.git
    REF 3e57d8dcfb0a04b5a8a26b486b54490a2e9b310f  # The one in pdfium DEPS file, field 'abseil_revision'
    SOURCE ${SOURCE_PATH}
)

vcpkg_check_linkage(ONLY_STATIC_LIBRARY)

file(COPY ${CMAKE_CURRENT_LIST_DIR}/CMakeLists.txt DESTINATION ${SOURCE_PATH})

vcpkg_cmake_configure(
  SOURCE_PATH "${SOURCE_PATH}"
)

vcpkg_cmake_install()
vcpkg_cmake_config_fixup(PACKAGE_NAME pdfium CONFIG_PATH share/pdfium)

set(PDFIUM_PREFIX ${CURRENT_PACKAGES_DIR})
configure_file("${CMAKE_CURRENT_LIST_DIR}/pdfium.pc.in" "${CURRENT_PACKAGES_DIR}/lib/pkgconfig/pdfium.pc" @ONLY)
if(NOT VCPKG_BUILD_TYPE)
  set(PDFIUM_PREFIX ${CURRENT_PACKAGES_DIR}/debug)
  configure_file("${CMAKE_CURRENT_LIST_DIR}/pdfium.pc.in" "${PDFIUM_PREFIX}/lib/pkgconfig/pdfium.pc" @ONLY)
  vcpkg_replace_string("${PDFIUM_PREFIX}/lib/pkgconfig/pdfium.pc" "-lbz2" " -lbz2d")
  vcpkg_replace_string("${PDFIUM_PREFIX}/lib/pkgconfig/pdfium.pc" "-lpng16" " -lpng16d")
  vcpkg_replace_string("${PDFIUM_PREFIX}/lib/pkgconfig/pdfium.pc" "-lfreetype" " -lfreetyped")
endif()

include(CMakePackageConfigHelpers)
configure_package_config_file(${CMAKE_CURRENT_LIST_DIR}/Config.cmake.in
    "${CURRENT_PACKAGES_DIR}/share/pdfium/pdfiumConfig.cmake"
    INSTALL_DESTINATION share/pdfium
    )

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")
file(INSTALL ${SOURCE_PATH}/LICENSE DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}/" RENAME copyright)
