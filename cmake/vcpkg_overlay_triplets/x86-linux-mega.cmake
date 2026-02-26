set(VCPKG_TARGET_ARCHITECTURE x86)
set(VCPKG_CRT_LINKAGE dynamic)

set(VCPKG_CMAKE_SYSTEM_NAME Linux)

if(PORT MATCHES "ffmpeg")
    # build this library as dynamic (usually because it is LGPL licensed)
    set(VCPKG_LIBRARY_LINKAGE dynamic)
    set(VCPKG_FIXUP_ELF_RPATH ON)
else()
    # build this library statically (much simpler installation, debugging, etc)
    set(VCPKG_LIBRARY_LINKAGE static)
endif()

set(VCPKG_C_FLAGS "${VCPKG_C_FLAGS}")
set(VCPKG_CXX_FLAGS "${VCPKG_CXX_FLAGS}")
set(VCPKG_LINKER_FLAGS "${VCPKG_LINKER_FLAGS}")
