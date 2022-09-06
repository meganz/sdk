
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

vcpkg_configure_cmake(
  SOURCE_PATH ${SOURCE_PATH}
  PREFER_NINJA
)

vcpkg_install_cmake()

file(INSTALL ${SOURCE_PATH}/public/ DESTINATION "${CURRENT_PACKAGES_DIR}/" RENAME "include")
file(INSTALL ${SOURCE_PATH}/LICENSE DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}/" RENAME copyright)
