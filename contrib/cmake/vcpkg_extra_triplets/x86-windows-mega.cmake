
if(EXISTS "C:\\Program Files (x86)\\Microsoft Visual Studio\\2019\\Professional")
    set(VCPKG_VISUAL_STUDIO_PATH "C:\\Program Files (x86)\\Microsoft Visual Studio\\2019\\Professional")
    set(VCPKG_PLATFORM_TOOLSET v142)
elseif(EXISTS "C:\\Program Files (x86)\\Microsoft Visual Studio\\2017\\Professional")
    set(VCPKG_VISUAL_STUDIO_PATH "C:\\Program Files (x86)\\Microsoft Visual Studio\\2017\\Professional")
    set(VCPKG_PLATFORM_TOOLSET v141)
elseif(EXISTS "C:\\Program Files (x86)\\Microsoft Visual Studio\\2019\\Community")
    message(AUTHOR_WARNING "Microsoft Visual Studio 2019 Community will be used. Professional would be more appropriate.")
    set(VCPKG_VISUAL_STUDIO_PATH "C:\\Program Files (x86)\\Microsoft Visual Studio\\2019\\Community")
    set(VCPKG_PLATFORM_TOOLSET v142)
elseif(EXISTS "C:\\Program Files (x86)\\Microsoft Visual Studio\\2017\\Community")
    message(AUTHOR_WARNING "Microsoft Visual Studio 2017 Community will be used. Professional would be more appropriate.")
    set(VCPKG_VISUAL_STUDIO_PATH "C:\\Program Files (x86)\\Microsoft Visual Studio\\2017\\Community")
    set(VCPKG_PLATFORM_TOOLSET v141)
else()
    message(FATAL_ERROR "Microsoft Visual Studio 2019 or 2017 could not be found")
endif()

set(VCPKG_TARGET_ARCHITECTURE x86)

# use dynamic C and CPP libraries (needed if we use any DLLs, eg Qt)
set(VCPKG_CRT_LINKAGE dynamic)

if(PORT MATCHES "ffmpeg" OR 
   PORT MATCHES "openssl" OR 
   PORT MATCHES "curl" OR 
   PORT MATCHES "qt5-" OR
   PORT MATCHES "c-ares")
    # build this library as DLL (usually because it is LGPL licensed)
    set(VCPKG_LIBRARY_LINKAGE dynamic)
else()
    # build this library statically (much simpler installation, debugging, etc)
	set(VCPKG_LIBRARY_LINKAGE static)
endif()
