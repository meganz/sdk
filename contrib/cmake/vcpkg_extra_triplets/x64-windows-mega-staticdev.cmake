# This triplet file shows how to build an all-static version, which is easy to develop with but not suitable for release due to third party licensing.
# Additionally it shows how to work with VS community edition, for home enthusiasts.
# When buliding all dependencies this way, debug iterators can be turned off - those can cause large delays in the SDK which can make testing in debug a bit slow.

if(EXISTS "C:\\Program Files (x86)\\Microsoft Visual Studio\\2019\\Community".)
    set(VCPKG_VISUAL_STUDIO_PATH "C:\\Program Files (x86)\\Microsoft Visual Studio\\2019\\Community")
else()
    message(FATAL_ERROR "Microsoft Visual Studio 2019 Community could not be found")
endif()

set(VCPKG_PLATFORM_TOOLSET v142)
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE static)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_CXX_FLAGS "${VCPKG_CXX_FLAGS} -D_ITERATOR_DEBUG_LEVEL=0")
set(VCPKG_C_FLAGS "${VCPKG_C_FLAGS} -D_ITERATOR_DEBUG_LEVEL=0")
