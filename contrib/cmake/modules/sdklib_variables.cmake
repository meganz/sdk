# No configurable values.
set(USE_SQLITE 1)
set(USE_SODIUM 1)
set(USE_CRYPTOPP 1)

if (NOT WIN32)
    set(USE_PTHREAD 1)
    set(USE_CPPTHREAD 0)
else()
    set(USE_CPPTHREAD 1)
endif()

# Adjust default CMAKE_OSX_DEPLOYMENT_TARGET variable depending on target architecture
# It should be set before calling project().
if(NOT CMAKE_OSX_DEPLOYMENT_TARGET)
    # CMAKE_OSX_DEPLOYMENT_TARGET also affects IOS builds
    if(CMAKE_HOST_APPLE AND NOT (CMAKE_SYSTEM_NAME STREQUAL "iOS"))

        # Minimum deployment target may differ if we are building for intel or arm64 targets
        # CMAKE_SYSTEM_PROCESSOR and CMAKE_HOST_SYSTEM_PROCESSOR are only available after project()
        execute_process(
            COMMAND uname -m
            OUTPUT_VARIABLE HOST_ARCHITECTURE
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )

        if(CMAKE_OSX_ARCHITECTURES MATCHES "arm64" OR (NOT CMAKE_OSX_ARCHITECTURES AND HOST_ARCHITECTURE STREQUAL "arm64"))
            set(CMAKE_OSX_DEPLOYMENT_TARGET "11.1" CACHE STRING "Minimum OS X deployment version")
        else()
            set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimum OS X deployment version")
        endif()

        unset(HOST_ARCHITECTURE)

    endif()
endif()
