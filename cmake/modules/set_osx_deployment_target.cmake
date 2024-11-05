# It configures the default OSX deployment target (CMAKE_OSX_DEPLOYMENT_TARGET)
# If the value is already set it will not be changed, to allow users to override
# the default ones.
#
# This function should be called before project().
#
#  set_osx_deployment_target(
#      ARM64 <min_version>
#      x86_64 <min_version>
#  )
#


function(set_osx_deployment_target)

set(options "")
set(oneValueArgs ARM64 x86_64)
set(multiValueArgs "")

cmake_parse_arguments("set_osx_deployment_target"
    "${options}"
    "${oneValueArgs}"
    "${multiValueArgs}"
    ${ARGN}
)

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
            set(CMAKE_OSX_DEPLOYMENT_TARGET ${set_osx_deployment_target_ARM64}
                CACHE
                STRING "Minimum OS X deployment version")
        else()
            set(CMAKE_OSX_DEPLOYMENT_TARGET ${set_osx_deployment_target_x86_64}
                CACHE
                STRING "Minimum OS X deployment version")
        endif()

        unset(HOST_ARCHITECTURE)

    endif()
endif()

endfunction()
