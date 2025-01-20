include(${CMAKE_CURRENT_LIST_DIR}/find_visual_studio_path.cmake)

find_visual_studio_path()

set(VCPKG_VISUAL_STUDIO_PATH ${VISUAL_STUDIO_PATH})

set(VCPKG_PLATFORM_TOOLSET v142)

set(VCPKG_TARGET_ARCHITECTURE arm64)

# use dynamic C and CPP libraries (needed if we use any DLLs, eg Qt)
set(VCPKG_CRT_LINKAGE dynamic)

if(PORT MATCHES "ffmpeg")
    # build this library as DLL (usually because it is LGPL licensed)
    set(VCPKG_LIBRARY_LINKAGE dynamic)
else()
    # build this library statically (much simpler installation, debugging, etc)
    set(VCPKG_LIBRARY_LINKAGE static)
endif()
