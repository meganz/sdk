include(vcpkg_common_functions)

if(NOT VCPKG_CMAKE_SYSTEM_NAME OR VCPKG_CMAKE_SYSTEM_NAME STREQUAL "WindowsStore")
    vcpkg_get_windows_sdk(WINDOWS_SDK)

    if (WINDOWS_SDK MATCHES "10.")
        GET_FILENAME_COMPONENT(WINDOWS_KIT_10_PATH "[HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows Kits\\Installed Roots;KitsRoot10]" ABSOLUTE CACHE)
        set(LIBGLFILEPATH  "${WINDOWS_KIT_10_PATH}\\Lib\\${WINDOWS_SDK}\\um\\${TRIPLET_SYSTEM_ARCH}\\OpenGL32.Lib")
        set(LIBGLUFILEPATH "${WINDOWS_KIT_10_PATH}\\Lib\\${WINDOWS_SDK}\\um\\${TRIPLET_SYSTEM_ARCH}\\GlU32.Lib")
        set(HEADERSPATH    "${WINDOWS_KIT_10_PATH}\\Include\\${WINDOWS_SDK}\\um")
    elseif(WINDOWS_SDK MATCHES "8.")
        GET_FILENAME_COMPONENT(WINDOWS_KIT_81_PATH "[HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows Kits\\Installed Roots;KitsRoot81]" ABSOLUTE CACHE)
        set(LIBGLFILEPATH  "${WINDOWS_KIT_81_PATH}\\Lib\\winv6.3\\um\\${TRIPLET_SYSTEM_ARCH}\\OpenGL32.Lib")
        set(LIBGLUFILEPATH "${WINDOWS_KIT_81_PATH}\\Lib\\winv6.3\\um\\${TRIPLET_SYSTEM_ARCH}\\GlU32.Lib")
        set(HEADERSPATH    "${WINDOWS_KIT_81_PATH}\\Include\\um")
    else()
        message(FATAL_ERROR "Portfile not yet configured for Windows SDK with version: ${WINDOWS_SDK}")
    endif()

    if (NOT EXISTS "${LIBGLFILEPATH}")
        message(FATAL_ERROR "Cannot find Windows ${WINDOWS_SDK} SDK. File does not exist: ${LIBGLFILEPATH}")
    endif()

    if (NOT EXISTS "${LIBGLUFILEPATH}")
        message(FATAL_ERROR "Cannot find Windows ${WINDOWS_SDK} SDK. File does not exist: ${LIBGLUFILEPATH}")
    endif()

    file(MAKE_DIRECTORY
        ${CURRENT_PACKAGES_DIR}/include/gl
        ${CURRENT_PACKAGES_DIR}/lib
        ${CURRENT_PACKAGES_DIR}/debug/lib
        ${CURRENT_PACKAGES_DIR}/share/opengl
    )

    file(COPY
        "${HEADERSPATH}\\gl\\GL.h"
        "${HEADERSPATH}\\gl\\GLU.h"
        DESTINATION ${CURRENT_PACKAGES_DIR}/include/gl
    )
    file(COPY ${LIBGLFILEPATH}  DESTINATION ${CURRENT_PACKAGES_DIR}/lib)
    file(COPY ${LIBGLUFILEPATH} DESTINATION ${CURRENT_PACKAGES_DIR}/lib)
    file(COPY ${LIBGLFILEPATH}  DESTINATION ${CURRENT_PACKAGES_DIR}/debug/lib)
    file(COPY ${LIBGLUFILEPATH} DESTINATION ${CURRENT_PACKAGES_DIR}/debug/lib)

    if (WINDOWS_SDK MATCHES "10.")
        file(WRITE ${CURRENT_PACKAGES_DIR}/share/opengl/copyright "See https://developer.microsoft.com/windows/downloads/windows-10-sdk for the Windows 10 SDK license")
    elseif(WINDOWS_SDK MATCHES "8.")
        file(WRITE ${CURRENT_PACKAGES_DIR}/share/opengl/copyright "See https://developer.microsoft.com/windows/downloads/windows-8-1-sdk for the Windows 8.1 SDK license")
    endif()
else()
    set(VCPKG_POLICY_EMPTY_PACKAGE enabled)
endif()
