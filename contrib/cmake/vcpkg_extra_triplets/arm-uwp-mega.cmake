#set(VCPKG_VISUAL_STUDIO_PATH "C:\\Program Files (x86)\\Microsoft Visual Studio\\2017\\Professional")
set(VCPKG_PLATFORM_TOOLSET v141)

set(VCPKG_TARGET_ARCHITECTURE arm)
set(VCPKG_CMAKE_SYSTEM_NAME WindowsStore)
set(VCPKG_CMAKE_SYSTEM_VERSION 10.0)

# use dynamic C and CPP libraries (needed if we use any DLLs, eg Qt)
set(VCPKG_CRT_LINKAGE dynamic)

set(VCPKG_LIBRARY_LINKAGE static)
