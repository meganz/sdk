set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)

set(VCPKG_CMAKE_SYSTEM_NAME Darwin)
set(VCPKG_OSX_ARCHITECTURES x86_64)
set(VCPKG_OSX_DEPLOYMENT_TARGET 10.15)

if(PORT MATCHES "ffmpeg")
    # build this library as dynamic (usually because it is LGPL licensed)
    set(VCPKG_LIBRARY_LINKAGE dynamic)
else()
    # build this library statically (much simpler installation, debugging, etc)
	set(VCPKG_LIBRARY_LINKAGE static)
endif()

set(_macos_version_min_flag "-mmacosx-version-min=${VCPKG_OSX_DEPLOYMENT_TARGET}")

set(VCPKG_C_FLAGS "${VCPKG_C_FLAGS} ${_macos_version_min_flag}")
set(VCPKG_CXX_FLAGS "${VCPKG_CXX_FLAGS} ${_macos_version_min_flag}")
set(VCPKG_LINKER_FLAGS "${VCPKG_LINKER_FLAGS} ${_macos_version_min_flag}")
