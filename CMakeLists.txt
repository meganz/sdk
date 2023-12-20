
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

## Configurable options ##

set(VCPKG_ROOT "" CACHE PATH "If set, it will build and use the VCPKG packages defined in the manifest file")
option(ENABLE_SYNC "Turns on sync functionality" ON)
option(ENABLE_CHAT "Turns on chat management functionality" OFF)
option(ENABLE_LOG_PERFORMANCE "Faster log message generation" OFF)
option(ENABLE_DRIVE_NOTIFICATIONS "Allows to monitor (external) drives being [dis]connected to the computer" OFF)
option(USE_MEDIAINFO "Used to determine media properties and set those as node attributes" ON)
option(USE_FREEIMAGE "Used to create previews/thumbnails for photos/pictures" ON)
option(USE_FFMPEG "Used to create previews/thumbnails for video files" ON)
option(USE_LIBUV "Includes the library and turns on internal web and ftp server functionality" OFF)
option(USE_PDFIUM "Used to create previews/thumbnails for PDF files" ON)
option(USE_C_ARES "If set, the SDK will manage DNS lookups and ipv4/ipv6 itself, using the c-ares library.  Otherwise we rely on cURL" ON)
if (WIN32 OR IOS)
    option(USE_READLINE "Use the readline library for the console" OFF)
else()
    option(USE_READLINE "Use the readline library for the console" ON)
endif()
if (APPLE AND NOT IOS)
    option(USE_OPENSSL "Use the OpenSSL library or a compatible one" OFF)
else()
    option(USE_OPENSSL "Use the OpenSSL library or a compatible one" ON)
endif()

# Old USE_XXX not needed in the SDK library.
# set (USE_ASIO 1 CACHE STRING "Only needed For tcprelay test tool")
# set (USE_LIBRAW 0 CACHE STRING "Just includes the library (used by MEGAsync)")
# set (USE_PCRE 0 CACHE STRING "Can be used by client apps. The SDK does not use it itself anymore")

## General configuration
# Main modules location
list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_LIST_DIR}/contrib/cmake/modules)

# No configurable values.
set(USE_CURL 1)
set(USE_SQLITE 1)
set(USE_SODIUM 1)
set(USE_CRYPTOPP 1)

if (NOT WIN32)
    set(USE_PTHREAD 1)
    set(USE_CPPTHREAD 0)
else()
    set(USE_CPPTHREAD 1)
endif()

if(VCPKG_ROOT)
    # Include VCPKG management tools.
    include(vcpkg_management)
    process_vcpkg_libraries()
else()
    # For packages with no pkg-config in the system.
    list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_LIST_DIR}/contrib/cmake/modules/packages)
    message(STATUS "Using system dependencies")
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

# Detect if building standalone or nested in another project
if(NOT CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)
    message(STATUS "[${PROJECT_NAME}] is building under project [${CMAKE_PROJECT_NAME}] Install target will NOT be available.")
    set(SDKLIB_STANDALONE 0)
    option(ENABLE_EXAMPLES "Example application is built if enabled" OFF)
    option(ENABLE_TESTS "Integration and unit tests are built if enabled" OFF)
    option(ENABLE_SDKLIB_WERROR "Enable warnings as errors." OFF)
else()
    message(STATUS "[${PROJECT_NAME}] is a top-level project. Install target will be available.")
    set(SDKLIB_STANDALONE 1)
    option(ENABLE_EXAMPLES "Example application is built if enabled" ON)
    option(ENABLE_TESTS "Integration and unit tests are built if enabled" ON)
    if (WIN32)
        option(ENABLE_SDKLIB_WERROR "Enable warnings as errors" ON)
    else()
        option(ENABLE_SDKLIB_WERROR "Enable warnings as errors when building in debug mode." OFF)
    endif()
endif()

message(STATUS "Generator: ${CMAKE_GENERATOR}")
if(CMAKE_GENERATOR_PLATFORM)
    message(STATUS "Platform: ${CMAKE_GENERATOR_PLATFORM}")
endif()
if(CMAKE_OSX_ARCHITECTURES)
    message(STATUS "Architectures: ${CMAKE_OSX_ARCHITECTURES}")
endif()
if (NOT CMAKE_BUILD_TYPE)
    message(STATUS "Generated with config types: ${CMAKE_CONFIGURATION_TYPES}")
else()
    message(STATUS "Build type set to: ${CMAKE_BUILD_TYPE}")
endif()

if(USE_READLINE AND WIN32)
    message(FATAL_ERROR "Readline is not available in Windows builds. Disable USE_READLINE to continue.")
endif()

message(STATUS "Building SDKlib v${PROJECT_VERSION}")

include(GNUInstallDirs) # Values for installation directories. All platforms
include(CMakePackageConfigHelpers) # For the CMake package
include(target_sources_conditional) # To add files to the project without building them
include(sdklib_libraries) # Includes a macro to load the dependencies, both the VCPKG or the system ones.

# Load common and per platform configuration for the project
include(configuration)

## Start loading targets
include(sdklib_target)

## Load examples and tests
if(ENABLE_EXAMPLES)
    #add_subdirectory(examples)
endif()

if(ENABLE_TESTS)
    #add_subdirectory(tests)
endif()
