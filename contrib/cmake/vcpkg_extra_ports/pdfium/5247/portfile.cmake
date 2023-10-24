
get_filename_component(GIT_PATH ${GIT} DIRECTORY)

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
            COMMAND ${GIT} clone --depth 1 ${pdfium_URL} ${pdfium_DESTINATION}
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

vcpkg_from_git(
    OUT_SOURCE_PATH SOURCE_PATH
    URL https://pdfium.googlesource.com/pdfium.git
    REF ee44620f2b58999b0d272d58fa0b994d5935f688 # chromium/5247
    PATCHES
        "fix_win_build.patch"
)

message(STATUS "Working on submodules and other dependencies...")

pdfium_from_git(
    DESTINATION build
    URL https://chromium.googlesource.com/chromium/src/build.git
    REF 26f8da34750ac3bccf683ed5f70d86f21c54b22b  # The one in pdfium DEPS file, field 'build_revision'
    SOURCE ${SOURCE_PATH}
)

pdfium_from_git(
    DESTINATION third_party/abseil-cpp
    URL https://chromium.googlesource.com/chromium/src/third_party/abseil-cpp.git
    REF 62a4d6866aeeca02036c510b2f3f286084dd62af  # The one in pdfium DEPS file, field 'abseil_revision'
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
