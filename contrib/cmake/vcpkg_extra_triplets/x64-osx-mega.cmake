set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)

set(VCPKG_CMAKE_SYSTEM_NAME Darwin)
set(VCPKG_OSX_DEPLOYMENT_TARGET 10.9)

if(PORT MATCHES "curl" OR
   PORT MATCHES "ffmpeg" OR
   PORT MATCHES "c-ares")
    # build this library as dynamic (usually because it is LGPL licensed)
    set(VCPKG_LIBRARY_LINKAGE dynamic)
else()
    # build this library statically (much simpler installation, debugging, etc)
	set(VCPKG_LIBRARY_LINKAGE static)
endif()
