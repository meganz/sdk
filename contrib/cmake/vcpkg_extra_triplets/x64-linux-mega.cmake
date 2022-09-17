set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)

set(VCPKG_CMAKE_SYSTEM_NAME Linux)
set(VCPKG_LINUX_ARCHITECTURES x86_64) # unused in build_from_scratch

if(PORT MATCHES "curl" OR
   PORT MATCHES "ffmpeg" OR
   PORT MATCHES "c-ares")
    # build this library as dynamic (usually because it is LGPL licensed)
    set(VCPKG_LIBRARY_LINKAGE dynamic)
else()
    # build this library statically (much simpler installation, debugging, etc)
    set(VCPKG_LIBRARY_LINKAGE static)
endif()

set(VCPKG_C_FLAGS "${VCPKG_C_FLAGS}")
set(VCPKG_CXX_FLAGS "${VCPKG_CXX_FLAGS}")
set(VCPKG_LINKER_FLAGS "${VCPKG_LINKER_FLAGS}")
