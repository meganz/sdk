#[[
    Run this file from its current folder in script mode, with triplet as a defined parameter:
	
        cmake -DTRIPLET=<triplet> [-DPLATFORM=<platform>] [-DEXTRA_ARGS=<-DVAR=VALUE>[;<-DVAR=VALUE...>] ] [-DTARGET=<target>[;<targets>...] ] -P build_from_scratch.cmake
		
	eg, for getting started on windows:
	
		cmake -DTRIPLET=x64-windows-mega -DEXTRA_ARGS="-DUSE_PDFIUM=0" -P build_from_scratch.cmake

        eg, for getting started on iOS (building for x64 ios simulator) (comment out unused libs in preferred-ports.sdk first):

                cmake -DTRIPLET=x64-ios-mega -DPLATFORM=ios -DEXTRA_ARGS="-DUSE_PDFIUM=0;-DUSE_MEDIAINFO=0;-DUSE_OPENSSL=0;-DUSE_FREEIMAGE=0;-DUSE_FFMPEG=0;-DUSE_PCRE=0;-DMEGA_USE_C_ARES=0" -P build_from_scratch.cmake
		
    It will set up and build 3rdparty dependencies in a folder next to the SDK folder, and also
    set up the project (Visual Studio on Windows) and bulid it in an SDK subfolder "build-<triplet>"

    The parameter PLATFORM is optional. If specified, its argument will be used to set the target platform
    for a cross-compile build. Valid platforms are given in the platform enumerator
    in build3rdparty/build3rdparty.cpp. Passing a PLATFORM arg that disagrees with that in the TRIPLET is
    undefined behavior. If PLATFORM is not specified, a build for the host platform is assumed.

    The parameters EXTRA_ARGS and TARGET are both optional parameters. You may pass a single argument
    in the normal way, but lists of arguments must be semicolon-delimited to conform with CMake's list syntax.
    Please note in the examples below the use of quotes when a list is passed: this is needed to prevent your
    shell from parsing the semicolons.

    The parameter EXTRA_ARGS controls configuration parameters that will be used to control the SDK build.
    These must be passed with the CMake definition syntax. For example:

        -DEXTRA_ARGS=-DUSE_PDFIUM=0
        -DEXTRA_ARGS="-DUSE_PDFIUM=0;-DUSE_FREEIMAGE=0"

    The parameter TARGET, if provided, will build only the selected targets. For example:

        -DTARGET=megacli
        -DTARGET="megacli;test_unit"

    If omitted, the script will build the whole project in a manner equivalent to calling `make all`.
    
	Pdfium is one third party library dependency whose source must be fetched manually, see 3rdparty_deps.txt.
	Once you have the pdfium source, you can rerun this script to build it.  Or, if you already have it
	before first running this script, copy it to 3rdParty_sdk/vcpkg/pdfium once the script has created that folder
	and it will be built during the first run - avoid disabling it by leaving out those steps flags of course.
]]
function(usage_exit err_msg)
    message(FATAL_ERROR
"${err_msg}
Build from scratch helper script usage:
    cmake -P build_from_scratch.cmake <triplet>
Script must be run from its current folder")
endfunction()

set(_script_cwd ${CMAKE_CURRENT_SOURCE_DIR})

if(NOT EXISTS "${_script_cwd}/build_from_scratch.cmake")
    usage_exit("Script was not run from its containing directory")
endif()

if(NOT TRIPLET)
    usage_exit("Triplet was not provided")
endif()

if(NOT EXTRA_ARGS)
    set(_extra_cmake_args "")
else()
    set(_extra_cmake_args ${EXTRA_ARGS})
endif()

set(_triplet ${TRIPLET})
set(_sdk_dir "${_script_cwd}/../..")

message(STATUS "Building for triplet ${_triplet} with SDK dir ${_sdk_dir}")

set (_3rdparty_sdk_dir "${_sdk_dir}/../3rdparty_sdk")
if(PLATFORM)
    set(_3rdparty_sdk_dir ${_3rdparty_sdk_dir}_${PLATFORM})
endif()

file(MAKE_DIRECTORY ${_3rdparty_sdk_dir})

set(CMAKE_EXECUTE_PROCESS_COMMAND_ECHO STDOUT)

set(_cmake ${CMAKE_COMMAND})

function(execute_checked_command)
    execute_process(
        ${ARGV}
        RESULT_VARIABLE _result
        ERROR_VARIABLE _error
    )

    if(_result)
        message(FATAL_ERROR "Execute_process command had nonzero exit code ${_result} with error ${_error}")
    endif()
endfunction()

# Configure and build the build3rdparty tool

execute_checked_command(
    COMMAND ${_cmake}
        -S ${_script_cwd}/build3rdParty
        -B ${_3rdparty_sdk_dir}
        -DCMAKE_BUILD_TYPE=Release
)

execute_checked_command(
    COMMAND ${_cmake}
        --build ${_3rdparty_sdk_dir}
        --config Release
)

# Use the prep tool to set up just our dependencies and no others

if(WIN32)
    set(_3rdparty_tool_exe "${_3rdparty_sdk_dir}/Release/build3rdParty.exe")
    set(_3rdparty_vcpkg_dir "${_3rdparty_sdk_dir}/Release/vcpkg/")
else()
    set(_3rdparty_tool_exe "${_3rdparty_sdk_dir}/build3rdParty")
    set(_3rdparty_vcpkg_dir "${_3rdparty_sdk_dir}/vcpkg/")
endif()

set(_3rdparty_tool_common_args
    --ports "${_script_cwd}/preferred-ports-sdk.txt"
    --triplet ${_triplet}
    --sdkroot ${_sdk_dir}
)

if(PLATFORM)
    list(APPEND _3rdparty_tool_common_args --platform ${PLATFORM})
endif()

execute_checked_command(
    COMMAND ${_3rdparty_tool_exe}
        --setup
        --removeunusedports
        ${_3rdparty_tool_common_args}
    WORKING_DIRECTORY ${_3rdparty_sdk_dir}
)

execute_checked_command(
    COMMAND ${_3rdparty_tool_exe}
        --build
        ${_3rdparty_tool_common_args}
    WORKING_DIRECTORY ${_3rdparty_sdk_dir}
)

# Allows use of the VCPKG_XXXX variables defined in the triplet file
# We search our own custom triplet folder, and then the standard ones searched by vcpkg
foreach(_triplet_dir
    "${_script_cwd}/vcpkg_extra_triplets/"
    "${_3rdparty_vcpkg_dir}/triplets/"
    "${_3rdparty_vcpkg_dir}/triplets/community"
)
    set(_triplet_file "${_triplet_dir}/${_triplet}.cmake")
    if(EXISTS ${_triplet_file})
        message(STATUS "Using triplet ${_triplet} at ${_triplet_file}")
        include(${_triplet_file})
        set(_triplet_file_found 1)
        break()
    endif()
endforeach()

if(NOT _triplet_file_found)
    message(FATAL_ERROR "Could not find triplet ${_triplet} in Mega vcpkg_extra_triplets nor in vcpkg triplet folders")
endif()

# Now set up to build this repo
# Logic between Windows and other platforms diverges slightly here:
# in the CMake paradigm, Visual Studio is what's called a multi-configuration generator,
# meaning we can do separate builds (Debug, Release) from one configuration.
# The default generators on *nix platforms (Ninja, Unix Makefiles) are single-config, so we
# configure once for each build type
# In a future extension, we may wish to have users provide the configs as a semicolon-separated list

set(_common_cmake_args
    "-DMega3rdPartyDir=${_3rdparty_sdk_dir}"
    "-DVCPKG_TRIPLET=${_triplet}"
    -DUSE_THIRDPARTY_FROM_VCPKG=1
    -S ${_script_cwd}
)

if(TARGET)
    set(_cmake_target_args --target ${TARGET})
elseif(TARGETS) # allow TARGETS as a synonym for TARGET :)
    set(_cmake_target_args --target ${TARGETS})
endif()

if(WIN32)
    if(_triplet MATCHES "staticdev$")
        list(APPEND _extra_cmake_args -DMEGA_LINK_DYNAMIC_CRT=0 -DUNCHECKED_ITERATORS=1)
    endif()

    if(VCPKG_TARGET_ARCHITECTURE STREQUAL "x86")
        set(_arch "Win32")
    else()
        set(_arch ${VCPKG_TARGET_ARCHITECTURE})
    endif()

    set(_toolset ${VCPKG_PLATFORM_TOOLSET})
    set(_build_dir "${_sdk_dir}/build-${_triplet}")

    file(MAKE_DIRECTORY ${_build_dir})

    execute_checked_command(
        COMMAND ${_cmake}
            -G "Visual Studio 16 2019"
            -A ${_arch}
            # Could also pass -T VCPKG_PLATFORM_TOOLSET
            -B ${_build_dir}
            ${_common_cmake_args}
            ${_extra_cmake_args}
    )

    foreach(_config "Release" "Debug")
        execute_checked_command(
            COMMAND ${_cmake}
                --build ${_build_dir}
                ${_cmake_target_args}
                --config ${_config}
                --parallel 4
        )
    endforeach()
else()
    if(VCPKG_CMAKE_SYSTEM_NAME STREQUAL "iOS")
        LIST(APPEND _extra_cmake_args -DHAVE_FFMPEG=0 -DENABLE_SYNC=0)
        include(${_3rdparty_vcpkg_dir}/scripts/toolchains/ios.cmake)
        if(NOT CMAKE_OSX_SYSROOT)
            # Probably should figure out why vcpkg doesn't set this var for arm64 ios,
            # this is what controls cross-compiling for apple targets
            set(CMAKE_OSX_SYSROOT "iphoneos")
        endif()
        set(_toolchain_cross_compile_args
            "-DCMAKE_SYSTEM_PROCESSOR=${CMAKE_SYSTEM_PROCESSOR}"
            "-DCMAKE_OSX_ARCHITECTURES=${CMAKE_OSX_ARCHITECTURES}"
            "-DCMAKE_SYSTEM_NAME=${CMAKE_SYSTEM_NAME}"
            "-DCMAKE_OSX_SYSROOT=${CMAKE_OSX_SYSROOT}"
        )
    endif()

    # Are we building for OSX?
    if (VCPKG_CMAKE_SYSTEM_NAME STREQUAL "Darwin")
        # Determine the host's architecture.
        execute_process(
            COMMAND uname -m
            OUTPUT_VARIABLE HOST_ARCHITECTURE
            OUTPUT_STRIP_TRAILING_WHITESPACE)

        # Are we crosscompiling? Compare against the triplet value
        if (NOT HOST_ARCHITECTURE STREQUAL VCPKG_OSX_ARCHITECTURES)
            set(_toolchain_cross_compile_args
                "-DCMAKE_OSX_ARCHITECTURES=${VCPKG_OSX_ARCHITECTURES}")
                message(STATUS "Cross compiling for arch ${VCPKG_OSX_ARCHITECTURES} in ${HOST_ARCHITECTURE} system.")
        endif ()

        # Clean up after ourselves.
        unset(HOST_ARCHITECTURE)
    endif ()

    foreach(_config "Debug" "Release")
        set(_build_dir "${_sdk_dir}/build-${_triplet}-${_config}")
        file(MAKE_DIRECTORY ${_build_dir})

        execute_checked_command(
            COMMAND ${_cmake}
                -B ${_build_dir}
                "-DCMAKE_BUILD_TYPE=${_config}"
                ${_common_cmake_args}
                ${_extra_cmake_args}
                ${_toolchain_cross_compile_args}
        )

        execute_checked_command(
            COMMAND ${_cmake}
                --build ${_build_dir}
                ${_cmake_target_args}
                --parallel 4
        )
    endforeach()
endif()
