
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
# To use it as a standalone library, once compiled and installed:
#    find_package(SDKlib REQUIRED)
#    target_link_libraries(<target> PRIVATE MEGA::SDKlib)
# If you prefer to use pkg-config, use the following instead:
#    pkg_check_modules(SDKlib REQUIRED IMPORTED_TARGET SDKlib)
#    target_link_libraries(<target> PRIVATE PkgConfig::SDKlib)
#
# To include the project in your application use the following:
#    add_subdirectory(path/to/sdk)
#    target_link_libraries(<target> PRIVATE MEGA::SDKlib)
#

cmake_minimum_required(VERSION 3.19)

# Qt Creator configures VCPKG automatically. Disable it, we may want to use different tripplets, paths...
set(QT_CREATOR_SKIP_VCPKG_SETUP TRUE)

## Configurable options ##

set(VCPKG_ROOT "" CACHE PATH "If set, it will build and use the VCPKG packages defined in the manifest file")
option(ENABLE_SYNC "Turns on sync functionality" ON)
option(ENABLE_CHAT "Turns on chat management functionality" OFF)
option(ENABLE_LOG_PERFORMANCE "Faster log message generation" OFF)
option(ENABLE_DRIVE_NOTIFICATIONS "Allows to monitor (external) drives being [dis]connected to the computer" OFF)
if (WIN32)
    option(ENABLE_WERROR "Enable warnings as errors" ON)
else()
    option(ENABLE_WERROR "Enable warnings as errors. This flag only works for Debug builds" OFF)
endif()

option(USE_MEDIAINFO "Used to determine media properties and set those as node attributes" ON)
option(USE_FREEIMAGE "Used to create previews/thumbnails for photos/pictures" ON)
option(USE_FFMPEG "Used to create previews/thumbnails for video files" ON)
option(USE_LIBUV "Includes the library and turns on internal web and ftp server functionality" OFF)
option(USE_PDFIUM "Used to create previews/thumbnails for PDF files" ON)
option(USE_C_ARES "If set, the SDK will manage DNS lookups and ipv4/ipv6 itself, using the c-ares library.  Otherwise we rely on cURL" ON)
if (WIN32 OR IOS)
    option(USE_READLINE "Use the readline library for the console" OFF) # TODO
else()
    option(USE_READLINE "Use the readline library for the console" ON) # TODO
endif()
if (APPLE AND NOT IOS)
    option(USE_OPENSSL "Use the OpenSSL library or a compatible one" OFF) # TODO
else()
    option(USE_OPENSSL "Use the OpenSSL library or a compatible one" ON) # TODO
endif()
option(ENABLE_CODECOVERAGE "Enable code coverage testing support") # TODO

# Old USE_XXX not needed in the SDK library.
# set (USE_ASIO 1 CACHE STRING "Only needed For tcprelay test tool")
# set (USE_LIBRAW 0 CACHE STRING "Just includes the library (used by MEGAsync)")
# set (USE_PCRE 0 CACHE STRING "Can be used by client apps. The SDK does not use it itself anymore")

## No configurable values.
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

## TODO: If  VCPKG_ROOT path is in CMAKE_TOOLCHAIN_FILE the upper project should be in charge of building libraries.
if(VCPKG_ROOT AND NOT CMAKE_TOOLCHAIN_FILE)
    # Use internal VCPKG tools
    set(VCPKG_BOOTSTRAP_OPTIONS "-disableMetrics")
    list(APPEND VCPKG_OVERLAY_TRIPLETS "${CMAKE_CURRENT_LIST_DIR}/contrib/cmake/vcpkg_overlay_triplets")
    list(APPEND VCPKG_OVERLAY_PORTS "${CMAKE_CURRENT_LIST_DIR}/contrib/cmake/vcpkg_overlay_ports")

    if(NOT VCPKG_TARGET_TRIPLET)
        # Try to guess the triplet if it not set yet.
        if(APPLE AND NOT IOS)
            set(VCPKG_TARGET_TRIPLET "arm64-osx-mega")
        elseif(WIN32)
            set(VCPKG_TARGET_TRIPLET "x64-windows-mega")
        else()
            set(VCPKG_TARGET_TRIPLET "x64-linux")
        endif()
    endif()

    # Load the VCPKG toolchain
    set(CMAKE_TOOLCHAIN_FILE ${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake)
    message(STATUS "Using VCPKG dependencies. VCPKG base path: ${VCPKG_ROOT} and tripplet ${VCPKG_TARGET_TRIPLET}")
else()
    message(STATUS "Using system dependencies")
endif()

# Get SDK library version
include(${CMAKE_CURRENT_LIST_DIR}/contrib/cmake/load_sdk_version.cmake)
read_sdk_version(MEGA_SDK_VERSION)

project(SDKlib
    VERSION ${MEGA_SDK_VERSION}
    DESCRIPTION "MEGA SDK Library"
    )

# In-source build not allowed
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
    message(FATAL_ERROR "In-source build is not allowed. Remove CMakeCache.txt and the CMakeFiles directory and set a new binary directory out of the source tree.")
endif()

# Detect if building standalone or nested in another project
if(NOT CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)
    message(STATUS "[${PROJECT_NAME}] is building under project [${CMAKE_PROJECT_NAME}] Install target will NOT be available.")
    set(SDKLIB_INSTALL_LIB 0)
else()
    message(STATUS "[${PROJECT_NAME}] is a top-level project. Install target will be available.")
    set(SDKLIB_INSTALL_LIB 1)
endif()

if (NOT CMAKE_BUILD_TYPE)
    message(STATUS "Generated with config types: ${CMAKE_CONFIGURATION_TYPES}")
else()
    message(STATUS "Build type set to: ${CMAKE_BUILD_TYPE}")
endif()

if(USE_READLINE AND WIN32)
    message(FATAL_ERROR "Readline is not available in Windows builds. Disable USE_READLINE to continue.")
endif()

## General configuration
message(STATUS "Building SDKlib v${PROJECT_VERSION}")

include(GNUInstallDirs) # Values for installation directories. All platforms
include(CMakePackageConfigHelpers) # For the CMake package
include(${CMAKE_CURRENT_LIST_DIR}/contrib/cmake/target_sources_conditional.cmake) # To add files to the project without building them
find_package(PkgConfig REQUIRED) # For libraries not using find_package

# Use folders in the IDE.
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

if (WIN32)
    # Build the SDKlib with C++17 for Windows
    # set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /Zc:__cplusplus /std:c++17")
    # add_definitions( -DNOMINMAX )
    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)

    # Enable build with multiple processes.
    set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /MP")
    set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /MP")

    # Create a separated PDB file with debug symbols.
    set (CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /Zi")
    set (CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} /Zi")

    add_compile_definitions(HAVE_CONFIG_H) # To include the config.h file in Windows builds

else()
    # The SDK library should be built with C++11
    set(CMAKE_CXX_STANDARD 11)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)

    include(CheckIncludeFile)
    include(CheckFunctionExists)
    check_include_file(inttypes.h HAVE_INTTYPES_H)
    check_include_file(dirent.h HAVE_DIRENT_H)
    check_include_file(glob.h HAVE_GLOB_H)
    check_function_exists(aio_write, HAVE_AIO_RT)
endif()

# TODO Review MEGA_LINK_DYNAMIC_CRT
set(MEGA_LINK_DYNAMIC_CRT ON)

# Define debug symbols covering all platforms.
add_compile_definitions(
    $<$<CONFIG:DEBUG>:DEBUG>
    $<$<CONFIG:DEBUG>:_DEBUG>
)

## SDKlib target ##

add_library(SDKlib)
add_library(MEGA::SDKlib ALIAS SDKlib)

set(SDKLIB_PUB_HEADERS
    ${CMAKE_CURRENT_LIST_DIR}/include/megaapi.h
)

set(SDKLIB_HEADERS
    ${CMAKE_CURRENT_LIST_DIR}/include/mega.h
    ${CMAKE_CURRENT_LIST_DIR}/include/megaapi_impl.h
    ${CMAKE_CURRENT_LIST_DIR}/include/mega/transferslot.h
    ${CMAKE_CURRENT_LIST_DIR}/include/mega/thread/posixthread.h
    ${CMAKE_CURRENT_LIST_DIR}/include/mega/thread/libuvthread.h
    ${CMAKE_CURRENT_LIST_DIR}/include/mega/command.h
    ${CMAKE_CURRENT_LIST_DIR}/include/mega/thread.h
    ${CMAKE_CURRENT_LIST_DIR}/include/mega/json.h
    ${CMAKE_CURRENT_LIST_DIR}/include/mega/base64.h
    ${CMAKE_CURRENT_LIST_DIR}/include/mega/mega_utf8proc.h
    ${CMAKE_CURRENT_LIST_DIR}/include/mega/gfx.h
    ${CMAKE_CURRENT_LIST_DIR}/include/mega/proxy.h
    ${CMAKE_CURRENT_LIST_DIR}/include/mega/crypto/sodium.h
    ${CMAKE_CURRENT_LIST_DIR}/include/mega/crypto/cryptopp.h
    ${CMAKE_CURRENT_LIST_DIR}/include/mega/http.h
    ${CMAKE_CURRENT_LIST_DIR}/include/mega/useralerts.h
    ${CMAKE_CURRENT_LIST_DIR}/include/mega/pendingcontactrequest.h
    ${CMAKE_CURRENT_LIST_DIR}/include/mega/textchat.h
    ${CMAKE_CURRENT_LIST_DIR}/include/mega/megaapp.h
    ${CMAKE_CURRENT_LIST_DIR}/include/mega/console.h
    ${CMAKE_CURRENT_LIST_DIR}/include/mega/user.h
    ${CMAKE_CURRENT_LIST_DIR}/include/mega/mega_evt_queue.h
    ${CMAKE_CURRENT_LIST_DIR}/include/mega/db.h
    ${CMAKE_CURRENT_LIST_DIR}/include/mega/megaclient.h
    ${CMAKE_CURRENT_LIST_DIR}/include/mega/autocomplete.h
    ${CMAKE_CURRENT_LIST_DIR}/include/mega/serialize64.h
    ${CMAKE_CURRENT_LIST_DIR}/include/mega/nodemanager.h
    ${CMAKE_CURRENT_LIST_DIR}/include/mega/setandelement.h
    ${CMAKE_CURRENT_LIST_DIR}/include/mega/mega_ccronexpr.h
    ${CMAKE_CURRENT_LIST_DIR}/include/mega/testhooks.h
    ${CMAKE_CURRENT_LIST_DIR}/include/mega/share.h
    ${CMAKE_CURRENT_LIST_DIR}/include/mega/mega_dict-src.h
    ${CMAKE_CURRENT_LIST_DIR}/include/mega/gfx/GfxProcCG.h
    ${CMAKE_CURRENT_LIST_DIR}/include/mega/gfx/freeimage.h
    ${CMAKE_CURRENT_LIST_DIR}/include/mega/gfx/gfx_pdfium.h
    ${CMAKE_CURRENT_LIST_DIR}/include/mega/gfx/external.h
    ${CMAKE_CURRENT_LIST_DIR}/include/mega/pubkeyaction.h
    ${CMAKE_CURRENT_LIST_DIR}/include/mega/mega_http_parser.h
    ${CMAKE_CURRENT_LIST_DIR}/include/mega/waiter.h
    ${CMAKE_CURRENT_LIST_DIR}/include/mega/db/sqlite.h
    ${CMAKE_CURRENT_LIST_DIR}/include/mega/types.h
    ${CMAKE_CURRENT_LIST_DIR}/include/mega/filefingerprint.h
    ${CMAKE_CURRENT_LIST_DIR}/include/mega/filesystem.h
    ${CMAKE_CURRENT_LIST_DIR}/include/mega/backofftimer.h
    ${CMAKE_CURRENT_LIST_DIR}/include/mega/raid.h
    ${CMAKE_CURRENT_LIST_DIR}/include/mega/logging.h
    ${CMAKE_CURRENT_LIST_DIR}/include/mega/file.h
    ${CMAKE_CURRENT_LIST_DIR}/include/mega/sync.h
    ${CMAKE_CURRENT_LIST_DIR}/include/mega/heartbeats.h
    ${CMAKE_CURRENT_LIST_DIR}/include/mega/utils.h
    ${CMAKE_CURRENT_LIST_DIR}/include/mega/account.h
    ${CMAKE_CURRENT_LIST_DIR}/include/mega/transfer.h
    ${CMAKE_CURRENT_LIST_DIR}/include/mega/config-android.h
    ${CMAKE_CURRENT_LIST_DIR}/include/mega/treeproc.h
    ${CMAKE_CURRENT_LIST_DIR}/include/mega/attrmap.h
    ${CMAKE_CURRENT_LIST_DIR}/include/mega/sharenodekeys.h
    ${CMAKE_CURRENT_LIST_DIR}/include/mega/request.h
    ${CMAKE_CURRENT_LIST_DIR}/include/mega/mega_zxcvbn.h
    ${CMAKE_CURRENT_LIST_DIR}/include/mega/fileattributefetch.h
    ${CMAKE_CURRENT_LIST_DIR}/include/mega/version.h
    ${CMAKE_CURRENT_LIST_DIR}/include/mega/node.h
    ${CMAKE_CURRENT_LIST_DIR}/include/mega/mediafileattribute.h
)

set(SDKLIB_SOURCES
    ${CMAKE_CURRENT_LIST_DIR}/src/megaapi.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/megaapi_impl.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/attrmap.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/autocomplete.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/backofftimer.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/base64.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/command.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/commands.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/db.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/file.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/fileattributefetch.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/filefingerprint.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/filesystem.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/gfx.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/gfx/external.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/gfx/freeimage.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/gfx/gfx_pdfium.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/http.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/json.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/logging.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/mediafileattribute.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/mega_ccronexpr.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/mega_http_parser.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/mega_utf8proc.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/mega_zxcvbn.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/megaclient.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/node.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/pendingcontactrequest.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/textchat.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/proxy.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/pubkeyaction.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/raid.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/request.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/serialize64.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/nodemanager.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/setandelement.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/share.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/sharenodekeys.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/sync.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/heartbeats.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/testhooks.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/transfer.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/transferslot.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/treeproc.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/user.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/useralerts.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/utils.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/waiterbase.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/crypto/cryptopp.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/crypto/sodium.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/db/sqlite.cpp
)

target_sources(SDKlib
    PRIVATE ${SDKLIB_HEADERS} ${SDKLIB_SOURCES} ${SDKLIB_PUB_HEADERS}
)

## Add generated config.h file
target_sources(SDKlib
    PRIVATE ${PROJECT_BINARY_DIR}/mega/config.h
)

# Files by platform and/or feature
# Files should appear only once.
# If the FLAG is not true for a file, it will be added as non-buildable source despite then the file is added again as a buildable one.
target_sources_conditional(SDKlib
    FLAG WIN32
    PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/include/mega/win32/megafs.h
    ${CMAKE_CURRENT_LIST_DIR}/include/mega/win32/megaconsolewaiter.h
    ${CMAKE_CURRENT_LIST_DIR}/include/mega/win32/megaconsole.h
    ${CMAKE_CURRENT_LIST_DIR}/include/mega/win32/megawaiter.h

    ${CMAKE_CURRENT_LIST_DIR}/src/win32/console.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/win32/consolewaiter.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/win32/fs.cpp
    # ${CMAKE_CURRENT_LIST_DIR}/src/win32/net.cpp # when not using curl. # TODO Remove support for winhttp?
    ${CMAKE_CURRENT_LIST_DIR}/src/win32/waiter.cpp
)

target_sources_conditional(SDKlib
    FLAG APPLE
    PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/include/mega/osx/osxutils.h
    ${CMAKE_CURRENT_LIST_DIR}/include/mega/osx/megafs.h

    ${CMAKE_CURRENT_LIST_DIR}/src/osx/osxutils.mm
    ${CMAKE_CURRENT_LIST_DIR}/src/osx/fs.cpp
)

target_sources_conditional(SDKlib
    FLAG NOT APPLE AND NOT WIN32
    PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/src/posix/console.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/posix/consolewaiter.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/posix/fs.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/posix/waiter.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/thread/posixthread.cpp
)

target_sources_conditional(SDKlib
    FLAG NOT APPLE
    PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/src/posix/net.cpp # Windows when not using CURL and Linux always.
)

target_sources_conditional(SDKlib
    FLAG ENABLE_DRIVE_NOTIFICATIONS
    PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/include/mega/drivenotify.h
    ${CMAKE_CURRENT_LIST_DIR}/src/drivenotify.cpp
)

target_sources_conditional(SDKlib
    FLAG ENABLE_DRIVE_NOTIFICATIONS AND WIN32
    PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/include/mega/win32/drivenotifywin.h
    ${CMAKE_CURRENT_LIST_DIR}/src/win32/drivenotifywin.cpp
)

target_sources_conditional(SDKlib
    FLAG ENABLE_DRIVE_NOTIFICATIONS AND APPLE
    PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/include/mega/osx/drivenotifyosx.h
    ${CMAKE_CURRENT_LIST_DIR}/src/osx/drivenotifyosx.cpp
)

target_sources_conditional(SDKlib
    FLAG ENABLE_DRIVE_NOTIFICATIONS AND NOT (APPLE OR WIN32)
    PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/include/mega/posix/drivenotifyposix.h
    ${CMAKE_CURRENT_LIST_DIR}/src/posix/drivenotifyposix.cpp
)

target_sources_conditional(SDKlib
    FLAG USE_CPPTHREAD
    PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/include/mega/thread/cppthread.h
    ${CMAKE_CURRENT_LIST_DIR}/src/thread/cppthread.cpp
)

target_sources_conditional(SDKlib
    FLAG NOT HAVE_GLOB_H AND NOT WIN32
    PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/include/mega/mega_glob.h
    ${CMAKE_CURRENT_LIST_DIR}/src/mega_glob.c
)

target_sources_conditional(SDKlib
    FLAG USE_LIBUV
    PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/include/mega/mega_evt_tls.h
    ${CMAKE_CURRENT_LIST_DIR}/src/mega_evt_tls.cpp
)

# Include directories
target_include_directories(SDKlib
    PRIVATE # Internal and private headers
        ${CMAKE_CURRENT_LIST_DIR}/include
        ${CMAKE_CURRENT_BINARY_DIR}
        $<$<BOOL:${APPLE}>:${CMAKE_CURRENT_LIST_DIR}/include/mega/osx>
        $<$<NOT:$<OR:$<BOOL:${WIN32}>,$<BOOL:${APPLE}>>>:${CMAKE_CURRENT_LIST_DIR}/include/mega/posix>
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/include> # For the top level projects.
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}> # For the external projects.
    )

set_target_properties(SDKlib PROPERTIES
    VERSION ${PROJECT_VERSION}
    DEBUG_POSTFIX "d"
)

## Load and link needed libraries for the SDKlib target ##
if(VCPKG_ROOT)
    find_package(cryptopp CONFIG REQUIRED)
    target_link_libraries(SDKlib PRIVATE cryptopp::cryptopp)

    find_package(unofficial-sodium REQUIRED)
    target_link_libraries(SDKlib PRIVATE unofficial-sodium::sodium unofficial-sodium::sodium_config_public)

    find_package(unofficial-sqlite3 REQUIRED)
    target_link_libraries(SDKlib PRIVATE unofficial::sqlite3::sqlite3)

    find_package(CURL REQUIRED)
    target_link_libraries(SDKlib PRIVATE CURL::libcurl)

    if(USE_MEDIAINFO)
        find_package(MediaInfoLib REQUIRED)
        target_link_libraries(SDKlib PRIVATE mediainfo)
    endif()

    if(USE_FFMPEG)
        find_package(FFMPEG REQUIRED)
        target_include_directories(SDKlib PRIVATE ${FFMPEG_INCLUDE_DIRS})
        target_link_directories(SDKlib PRIVATE ${FFMPEG_LIBRARY_DIRS})
        target_link_libraries(SDKlib PRIVATE ${FFMPEG_LIBRARIES})
        set(HAVE_FFMPEG 1)
    endif()

    if(USE_LIBUV)
        find_package(libuv REQUIRED)
        target_link_libraries(SDKlib PRIVATE $<IF:$<TARGET_EXISTS:libuv::uv_a>,libuv::uv_a,libuv::uv>)
        set(HAVE_LIBUV 1)
    endif()

    if(USE_PDFIUM)
        find_package(pdfium REQUIRED)
        target_link_libraries(SDKlib PRIVATE PDFIUM::pdfium)
        set(HAVE_PDFIUM 1)
    endif()

    if(USE_C_ARES)
        find_package(c-ares REQUIRED)
        target_link_libraries(SDKlib PRIVATE c-ares::cares)
        set(MEGA_USE_C_ARES 1)
    endif()

    if(USE_READLINE)
        pkg_check_modules(readline REQUIRED IMPORTED_TARGET readline)
        target_link_libraries(SDKlib PRIVATE PkgConfig::readline)
    else()
        set(NO_READLINE 1)
    endif()

else() # No VCPKG usage. Use pkg-config
    pkg_check_modules(cryptopp REQUIRED IMPORTED_TARGET libcrypto++)
    target_link_libraries(SDKlib PRIVATE PkgConfig::cryptopp)

    pkg_check_modules(sodium REQUIRED IMPORTED_TARGET libsodium)
    target_link_libraries(SDKlib PRIVATE PkgConfig::sodium)

    pkg_check_modules(sqlite3 REQUIRED IMPORTED_TARGET sqlite3)
    target_link_libraries(SDKlib PRIVATE PkgConfig::sqlite3)

    pkg_check_modules(curl REQUIRED IMPORTED_TARGET libcurl)
    target_link_libraries(SDKlib PRIVATE PkgConfig::curl)

    if(USE_MEDIAINFO)
        pkg_check_modules(mediainfo REQUIRED IMPORTED_TARGET libmediainfo)
        target_link_libraries(SDKlib PRIVATE PkgConfig::mediainfo)
    endif()

    if(USE_FFMPEG)
        pkg_check_modules(ffmpeg REQUIRED IMPORTED_TARGET libavformat libavutil libavcodec libavfilter libavdevice libswscale libswresample)
        target_link_libraries(SDKlib PRIVATE PkgConfig::ffmpeg)
        set(HAVE_FFMPEG 1)
    endif()

    if(USE_LIBUV)
        pkg_check_modules(uv REQUIRED IMPORTED_TARGET libuv)
        target_link_libraries(SDKlib PRIVATE PkgConfig::uv)
        set(HAVE_LIBUV 1)
    endif()

    if(USE_PDFIUM)
        pkg_check_modules(pdfium REQUIRED IMPORTED_TARGET pdfium)
        target_link_libraries(SDKlib PRIVATE PkgConfig::pdfium)
        set(HAVE_PDFIUM 1)
    endif()

    if(USE_C_ARES)
        pkg_check_modules(cares REQUIRED IMPORTED_TARGET libcares)
        target_link_libraries(SDKlib PRIVATE PkgConfig::cares)
        set(MEGA_USE_C_ARES 1)
    endif()

    if(USE_READLINE)
        pkg_check_modules(readline REQUIRED IMPORTED_TARGET readline)
        target_link_libraries(SDKlib PRIVATE PkgConfig::readline)
    else()
        set(NO_READLINE 1)
    endif()

endif()

if(ENABLE_DRIVE_NOTIFICATIONS)
    if(WIN32)
        target_link_libraries(SDKlib PRIVATE wbemuuid)
    elseif(APPLE)
        target_link_libraries(SDKlib PRIVATE "-framework DiskArbitration -framework CoreFoundation")
    else() # Linux
        target_link_libraries(SDKlib PRIVATE udev)
    endif()
    set(USE_DRIVE_NOTIFICATIONS 1)
endif()

# System libraries
if(WIN32)
    target_link_libraries(SDKlib PRIVATE
        ws2_32 winhttp Shlwapi Secur32.lib crypt32.lib
        $<$<BOOL:${USE_CURL}>:Wldap32.lib>
        $<$<BOOL:${USE_LIBUV}>:Kernel32.lib Iphlpapi.lib Userenv.lib Psapi.lib>
        $<$<BOOL:${USE_FFMPEG}>:Mfplat.lib mfuuid.lib strmiids.lib>
        $<$<BOOL:${ENABLE_DRIVE_NOTIFICATIONS}>:wbemuuid>
    )
else()
    if(APPLE)
        target_link_libraries(SDKlib PRIVATE
            "-framework CoreServices"
            "-framework SystemConfiguration -framework Security"
            "-framework Cocoa"
            $<$<BOOL:${USE_FFMPEG}>:-liconv>
        )
    endif()
endif()

# TODO SET(Mega_PlatformSpecificLibs ${Mega_PlatformSpecificLibs} pthread z dl termcap) Linux
# TODO SET(Mega_PlatformSpecificLibs ${Mega_PlatformSpecificLibs} crypto rt stdc++fs) Linux

## Generate config.h ##
configure_file(
    ${CMAKE_CURRENT_LIST_DIR}/contrib/cmake/config.h.in
    ${PROJECT_BINARY_DIR}/mega/config.h
    )

## Adjust compilation flags for warnigns and errors
if(WIN32)
    if (ENABLE_WERROR)
        # Enable warnings as errors in debug mode for Windows
        set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /WX")
    endif()

    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /W4")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /wd4201")  # nameless struct/union (nonstandard)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /wd4100")  # unreferenced formal parameter
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /wd4706")  # assignment within conditional
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /wd4458")  # identifier hides class member
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /wd4324")  # structure was padded due to alignment specifier (common in Sodium)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /wd4456")  # declaration hides previous local declaration
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /wd4266")  # derived class did not override all overloads of a virtual function
    #TODO: remove some of those gradually.  also consider: /wd4503 /wd4996 /wd4702

    # TODO
    set_property(TARGET Mega PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>$<${MEGA_LINK_DYNAMIC_CRT}:DLL>")

else()
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -ggdb3")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wconversion -Wno-unused-parameter")

    if (ENABLE_WERROR)
        if (CMAKE_BUILD_TYPE STREQUAL "Debug" )
            set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Werror")
            # Warnings which should not be promoted to errors, but still appear as warnings
            set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-error=deprecated-declarations")
            if (APPLE)
              set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}
                  -Wno-sign-conversion -Wno-overloaded-virtual
                  -Wno-inconsistent-missing-override -Wno-unused-variable
                  -Wno-unused-private-field -Wno-string-conversion
                  -Wno-unused-lambda-capture -Wno-implicit-int-conversion
                  -Wno-shorten-64-to-32 -Wno-unused-value"
              )
            endif()
        endif()
        message(STATUS, "CMAKE_CXX_FLAGS: ${CMAKE_CXX_FLAGS}")
        message(STATUS, "CMAKE_CXX_FLAGS_DEBUG: ${CMAKE_CXX_FLAGS_DEBUG}")
    endif(ENABLE_WERROR)
endif()

## Create config files ##
configure_package_config_file(${CMAKE_CURRENT_LIST_DIR}/contrib/cmake/Config.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/sdklibConfig.cmake
    INSTALL_DESTINATION cmake
    )

set(SDKLIB_NAME "SDKlib")
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  set(SDKLIB_NAME "${SDKLIB_NAME}d")
endif()

configure_file(
    ${CMAKE_CURRENT_LIST_DIR}/contrib/cmake/sdklib.pc.in
    ${CMAKE_CURRENT_BINARY_DIR}/sdklib.pc @ONLY
    )

## Installation ##
if(SDKLIB_INSTALL_LIB)

    message(STATUS "Current installation path for SDKlib files: ${CMAKE_INSTALL_PREFIX}")

    # Install library: Lib, export targets, pub headers.
    install(TARGETS SDKlib
        EXPORT "sdklibTargets"
        PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
        INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
        )

    # Install export file
    install(EXPORT "sdklibTargets"
        FILE "sdklibTargets.cmake"
        NAMESPACE MEGA::
        DESTINATION cmake
        )

    # Install config files
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/sdklibConfig.cmake DESTINATION cmake)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/sdklib.pc DESTINATION pkgconfig)
    install(FILES ${SDKLIB_PUB_HEADERS} DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

    # Export targets to be used from the build directory.
    export(EXPORT "sdklibTargets"
        FILE "${CMAKE_CURRENT_BINARY_DIR}/cmake/sdklibTargets.cmake"
        NAMESPACE MEGA::
        )

endif()

