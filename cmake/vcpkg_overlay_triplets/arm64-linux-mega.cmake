set(VCPKG_TARGET_ARCHITECTURE arm64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_CMAKE_SYSTEM_NAME Linux)

if(PORT MATCHES "ffmpeg")
    # build this library as dynamic (usually because it is LGPL licensed)
    set(VCPKG_LIBRARY_LINKAGE dynamic)
else()
    # build this library statically (much simpler installation, debugging, etc)
	set(VCPKG_LIBRARY_LINKAGE static)
endif()

