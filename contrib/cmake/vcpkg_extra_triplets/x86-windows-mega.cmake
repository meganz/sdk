
set(VCPKG_VISUAL_STUDIO_PATH "C:\\Program Files (x86)\\Microsoft Visual Studio\\2017\\Professional")
set(VCPKG_PLATFORM_TOOLSET v140_xp)

set(VCPKG_TARGET_ARCHITECTURE x86)
set(VCPKG_CRT_LINKAGE dynamic)

if(PORT MATCHES "ffmpeg" OR 
   PORT MATCHES "openssl" OR 
   PORT MATCHES "curl" OR 
   PORT MATCHES "c-ares")
    set(VCPKG_LIBRARY_LINKAGE dynamic)
else()
	set(VCPKG_LIBRARY_LINKAGE static)
endif()
