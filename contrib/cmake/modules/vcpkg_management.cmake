macro(process_vcpkg_libraries overlays_path)

    set(VCPKG_TOOLCHAIN_PATH "${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake")

    # Use internal VCPKG tools
    set(VCPKG_BOOTSTRAP_OPTIONS "-disableMetrics")
    list(APPEND VCPKG_OVERLAY_TRIPLETS "${overlays_path}/vcpkg_overlay_triplets")
    list(APPEND VCPKG_OVERLAY_PORTS "${overlays_path}/vcpkg_overlay_ports")

    if(NOT VCPKG_TARGET_TRIPLET)
        # Try to guess the triplet if it is not set.
        if(APPLE AND NOT IOS)
            if (CMAKE_OSX_ARCHITECTURES MATCHES "x86_64" OR (NOT CMAKE_OSX_ARCHITECTURES AND CMAKE_SYSTEM_PROCESSOR STREQUAL "x86_64"))
                set(VCPKG_TARGET_TRIPLET "x64-osx-mega")
            else()
                set(VCPKG_TARGET_TRIPLET "arm64-osx-mega")
            endif()
        elseif(WIN32)
            if(CMAKE_GENERATOR_PLATFORM MATCHES "Win32")
                set(VCPKG_TARGET_TRIPLET "x86-windows-mega")
            else()
                set(VCPKG_TARGET_TRIPLET "x64-windows-mega")
            endif()
        else()
            set(VCPKG_TARGET_TRIPLET "x64-linux-mega")
        endif()
    endif()

    if (USE_OPENSSL)
        list(APPEND VCPKG_MANIFEST_FEATURES "use-openssl")
    endif()

    if (USE_MEDIAINFO)
        list(APPEND VCPKG_MANIFEST_FEATURES "use-mediainfo")
    endif()

    if (USE_FREEIMAGE)
        list(APPEND VCPKG_MANIFEST_FEATURES "use-freeimage")
    endif()

    if (USE_FFMPEG)
        list(APPEND VCPKG_MANIFEST_FEATURES "use-ffmpeg")
    endif()

    if (USE_LIBUV)
        list(APPEND VCPKG_MANIFEST_FEATURES "use-libuv")
    endif()

    if (USE_PDFIUM)
        list(APPEND VCPKG_MANIFEST_FEATURES "use-pdfium")
    endif()

    if (USE_C_ARES)
        list(APPEND VCPKG_MANIFEST_FEATURES "use-cares")
    endif()

    if (USE_READLINE)
        list(APPEND VCPKG_MANIFEST_FEATURES "use-readline")
    endif()

    if (ENABLE_SDKLIB_TESTS)
        list(APPEND VCPKG_MANIFEST_FEATURES "sdk-tests")
    endif()

    set(CMAKE_TOOLCHAIN_FILE ${CMAKE_TOOLCHAIN_FILE} ${VCPKG_TOOLCHAIN_PATH})
    message(STATUS "Using VCPKG dependencies. VCPKG base path: ${VCPKG_ROOT} and tripplet ${VCPKG_TARGET_TRIPLET}")

endmacro()
