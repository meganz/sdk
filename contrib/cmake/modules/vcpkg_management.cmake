macro(process_vcpkg_libraries)

set(VCPKG_TOOLCHAIN_PATH "${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake")

## If VCPKG_ROOT path is in CMAKE_TOOLCHAIN_FILE, VCPKG is alredy configured by us, or we are a subproject.
if(NOT CMAKE_TOOLCHAIN_FILE MATCHES "${VCPKG_TOOLCHAIN_PATH}")
    # Use internal VCPKG tools
    set(VCPKG_BOOTSTRAP_OPTIONS "-disableMetrics")
    list(APPEND VCPKG_OVERLAY_TRIPLETS "${CMAKE_CURRENT_LIST_DIR}/contrib/cmake/vcpkg_overlay_triplets")
    list(APPEND VCPKG_OVERLAY_PORTS "${CMAKE_CURRENT_LIST_DIR}/contrib/cmake/vcpkg_overlay_ports")

    if(NOT VCPKG_TARGET_TRIPLET)
        # Try to guess the triplet if it is not set.
        if(APPLE AND NOT IOS)
            set(VCPKG_TARGET_TRIPLET "arm64-osx-mega")
        elseif(WIN32)
            set(VCPKG_TARGET_TRIPLET "x64-windows-mega")
        else()
            set(VCPKG_TARGET_TRIPLET "x64-linux")
        endif()
    endif()

    set(CMAKE_TOOLCHAIN_FILE ${CMAKE_TOOLCHAIN_FILE} ${VCPKG_TOOLCHAIN_PATH})
    message(STATUS "Using VCPKG dependencies. VCPKG base path: ${VCPKG_ROOT} and tripplet ${VCPKG_TARGET_TRIPLET}")
else()
    message(STATUS "VCPKG already configured with base path ${VCPKG_ROOT}")
endif()

endmacro()
