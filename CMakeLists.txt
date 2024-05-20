
option(OLD_CMAKE "" OFF)

if(OLD_CMAKE)
    # This file is here in the root folder so it can be opened as a CMake project in Visual Studio, eg in order to debug a Linux build in WSL2.
    cmake_minimum_required(VERSION 3.13)
    project(TryWSL)
    include(${CMAKE_CURRENT_LIST_DIR}/contrib/cmake/CMakeLists.txt)

    # Do not continue with the rest of the CMake file.
    return()
endif()

# CMakeLists.txt file to build the SDKlib library.
#
# It can be used to build a standalone library or to be included via add_subdirectory.
#
# To include the project in your application use the following:
#    add_subdirectory(path/to/sdk)
#    target_link_libraries(<target> PRIVATE MEGA::SDKlib)
#
# To use it as a standalone library, once compiled and installed:
#    find_package(SDKlib REQUIRED)
#    target_link_libraries(<target> PRIVATE MEGA::SDKlib)
# If you prefer to use pkg-config, use the following instead:
#    pkg_check_modules(SDKlib REQUIRED IMPORTED_TARGET SDKlib)
#    target_link_libraries(<target> PRIVATE PkgConfig::SDKlib)
#

cmake_minimum_required(VERSION 3.18)

# Qt Creator configures VCPKG automatically. Disable it, we may want to use different tripplets, paths...
set(QT_CREATOR_SKIP_VCPKG_SETUP TRUE CACHE BOOL "")

# Main modules location
list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_LIST_DIR}/contrib/cmake/modules)

## Configurable options ##
set(VCPKG_ROOT "" CACHE PATH "If set, it will build and use the VCPKG packages defined in the manifest file")
include(sdklib_options)

# If PROJECT_NAME is not set before project() we are the main project.
if(NOT PROJECT_NAME)
    message(STATUS "[SDKlib] is a top-level project. Install target, examples and tests will be enabled by default.")
    set(SDKLIB_STANDALONE 1)
    option(ENABLE_SDKLIB_EXAMPLES "Example application is built if enabled" ON)
    option(ENABLE_SDKLIB_TESTS "Integration and unit tests are built if enabled" ON)
    if (WIN32)
        option(ENABLE_SDKLIB_WERROR "Enable warnings as errors" ON)
    else()
        option(ENABLE_SDKLIB_WERROR "Enable warnings as errors when building in debug mode." OFF)
    endif()
else()
    message(STATUS "[SDKlib] is building under project [${PROJECT_NAME}] Install target, examples and tests will not be enabled by default.")
    set(SDKLIB_STANDALONE 0)
    option(ENABLE_SDKLIB_EXAMPLES "Example application is built if enabled" OFF)
    option(ENABLE_SDKLIB_TESTS "Integration and unit tests are built if enabled" OFF)
    option(ENABLE_SDKLIB_WERROR "Enable warnings as errors." OFF)
endif()

## General configuration

include(sdklib_variables)

if(NOT PROJECT_NAME)
    if(VCPKG_ROOT)
        # Include VCPKG management tools.
        include(vcpkg_management)
        process_vcpkg_libraries(${CMAKE_CURRENT_LIST_DIR}/contrib/cmake)
    else()
        # For packages with no pkg-config in the system.
        list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_LIST_DIR}/contrib/cmake/modules/packages)
        message(STATUS "Using system dependencies")
    endif()
endif()

# Get SDK library version to use it as the CMake project version.
include(load_sdk_version)
read_sdk_version(MEGA_SDK_VERSION ${CMAKE_CURRENT_LIST_DIR}/include/mega/version.h)

project(SDKlib
    VERSION ${MEGA_SDK_VERSION}
    DESCRIPTION "MEGA SDK Library"
    )

# In-source build not allowed
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
    message(FATAL_ERROR "In-source build is not allowed. Remove CMakeCache.txt and the CMakeFiles directory and set a new binary directory different than the source tree.")
endif()

message(STATUS "Generator: ${CMAKE_GENERATOR}")
if(CMAKE_GENERATOR_PLATFORM)
    message(STATUS "Platform: ${CMAKE_GENERATOR_PLATFORM}")
endif()
if(CMAKE_OSX_ARCHITECTURES)
    message(STATUS "Architectures: ${CMAKE_OSX_ARCHITECTURES}")
endif()
message(STATUS "Target System Processor: ${CMAKE_SYSTEM_PROCESSOR}")
message(STATUS "Host System Processor: ${CMAKE_HOST_SYSTEM_PROCESSOR}")
if (NOT CMAKE_BUILD_TYPE)
    message(STATUS "Generated with config types: ${CMAKE_CONFIGURATION_TYPES}")
else()
    message(STATUS "Build type set to: ${CMAKE_BUILD_TYPE}")
endif()

if(USE_READLINE AND WIN32)
    message(FATAL_ERROR "Readline is not available in Windows builds. Disable USE_READLINE to continue.")
endif()

if(ENABLE_ISOLATED_GFX AND NOT USE_FREEIMAGE)
    message(FATAL_ERROR "USE_FREEIMAGE is required when ENABLE_ISOLATED_GFX is enabled.")
endif()

message(STATUS "Building SDKlib v${PROJECT_VERSION}")

include(GNUInstallDirs) # Values for installation directories. All platforms
include(CMakePackageConfigHelpers) # For the CMake package
include(target_sources_conditional) # To add files to the project without building them
include(target_platform_compile_options) # To add compile options depeding on the platform
include(sdklib_libraries) # Includes a macro to load the dependencies, both the VCPKG or the system ones.

# Load common and per platform configuration for the project
include(configuration)

## Start loading targets
include(sdklib_target)

# Load Qt bindings
if(ENABLE_QT_BINDINGS)
    add_subdirectory(bindings/qt)
endif()

if(ENABLE_ISOLATED_GFX)
    add_subdirectory(tools/gfxworker)
endif()

## Load examples and tests
if(ENABLE_SDKLIB_EXAMPLES)
    add_subdirectory(examples)
endif()

if(ENABLE_SDKLIB_TESTS)
    add_subdirectory(tests)
endif()

## Load FUSE support.
include(src/fuse/CMakeLists.txt)

