# No configurable values.
set(USE_SQLITE 1)
set(USE_SODIUM 1)
set(USE_CRYPTOPP 1)

if (NOT WIN32)
    set(USE_PTHREAD 1)
    set(USE_CPPTHREAD 0)
else()
    set(USE_CPPTHREAD 1)
    set(CMAKE_GENERATOR_TOOLSET "v142")
endif()

include(detect_host_architecture)

# Configure CMAKE_OSX_DEPLOYMENT_TARGET if not already set
include(set_osx_deployment_target)
set_osx_deployment_target(
    ARM64 "11.1"
    x86_64 "10.15"
)

if (CMAKE_SYSTEM_NAME STREQUAL "Android")
    # Ensure that compatibility with Android devices that use a 16KiB page size is enabled
    set(ANDROID_SUPPORT_FLEXIBLE_PAGE_SIZES ON)
endif()
