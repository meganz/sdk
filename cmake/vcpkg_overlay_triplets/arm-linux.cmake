set(VCPKG_TARGET_ARCHITECTURE arm)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_CMAKE_SYSTEM_NAME Linux)

if(PORT MATCHES "ffmpeg")
    # build this library as dynamic (usually because it is LGPL licensed)
    set(VCPKG_LIBRARY_LINKAGE dynamic)
else()
    # build this library statically (much simpler installation, debugging, etc)
	set(VCPKG_LIBRARY_LINKAGE static)
endif()

set(VCPKG_C_FLAGS "${VCPKG_C_FLAGS} -mfpu=vfp")
set(VCPKG_CXX_FLAGS "${VCPKG_CXX_FLAGS} -mfpu=vfp")