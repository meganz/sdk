## SDKlib target ##
add_library(SDKlib)
add_library(MEGA::SDKlib ALIAS SDKlib)

set(SDKLIB_PUB_HEADERS
    include/megaapi.h
)

set(SDKLIB_HEADERS
    include/mega.h
    include/megaapi_impl.h
    include/megautils.h
    include/mega/transferslot.h
    include/mega/scoped_helpers.h
    include/mega/traits.h
    include/mega/scoped_timer.h
    include/mega/command.h
    include/mega/thread.h
    include/mega/json.h
    include/mega/base64.h
    include/mega/mega_utf8proc.h
    include/mega/gfx.h
    include/mega/proxy.h
    include/mega/crypto/sodium.h
    include/mega/crypto/cryptopp.h
    include/mega/http.h
    include/mega/useralerts.h
    include/mega/pendingcontactrequest.h
    include/mega/textchat.h
    include/mega/megaapp.h
    include/mega/console.h
    include/mega/user.h
    include/mega/mega_evt_queue.h
    include/mega/db.h
    include/mega/megaclient.h
    include/mega/autocomplete.h
    include/mega/serialize64.h
    include/mega/nodemanager.h
    include/mega/setandelement.h
    include/mega/mega_ccronexpr.h
    include/mega/testhooks.h
    include/mega/share.h
    include/mega/mega_dict-src.h
    include/mega/gfx/GfxProcCG.h
    include/mega/gfx/freeimage.h
    include/mega/gfx/gfx_pdfium.h
    include/mega/gfx/external.h
    include/mega/pubkeyaction.h
    include/mega/mega_http_parser.h
    include/mega/waiter.h
    include/mega/db/sqlite.h
    include/mega/types.h
    include/mega/filefingerprint.h
    include/mega/filesystem.h
    include/mega/backofftimer.h
    include/mega/raid.h
    include/mega/raidproxy.h
    include/mega/logging.h
    include/mega/file.h
    include/mega/sync.h
    include/mega/syncfilter.h
    include/mega/heartbeats.h
    include/mega/utils.h
    include/mega/account.h
    include/mega/transfer.h
    include/mega/transferstats.h
    include/mega/config-android.h
    include/mega/treeproc.h
    include/mega/arguments.h
    include/mega/attrmap.h
    include/mega/sharenodekeys.h
    include/mega/request.h
    include/mega/mega_zxcvbn.h
    include/mega/fileattributefetch.h
    include/mega/version.h
    include/mega/node.h
    include/mega/mediafileattribute.h
    include/mega/process.h
    include/mega/mega_csv.h
    include/mega/name_collision.h
    include/mega/name_id.h
    include/mega/pwm_file_parser.h
    include/mega/user_attribute.h
    include/mega/user_attribute_definition.h
    include/mega/user_attribute_manager.h
    include/mega/user_attribute_types.h
)

set(SDKLIB_SOURCES
    src/megaapi.cpp
    src/megaapi_impl.cpp
    src/arguments.cpp
    src/attrmap.cpp
    src/autocomplete.cpp
    src/backofftimer.cpp
    src/base64.cpp
    src/command.cpp
    src/commands.cpp
    src/db.cpp
    src/file.cpp
    src/fileattributefetch.cpp
    src/filefingerprint.cpp
    src/filesystem.cpp
    src/gfx.cpp
    src/gfx/external.cpp
    src/gfx/freeimage.cpp
    src/gfx/gfx_pdfium.cpp
    src/http.cpp
    src/json.cpp
    src/logging.cpp
    src/mediafileattribute.cpp
    src/mega_ccronexpr.cpp
    src/mega_http_parser.cpp
    src/mega_utf8proc.cpp
    src/mega_zxcvbn.cpp
    src/megaclient.cpp
    src/node.cpp
    src/pendingcontactrequest.cpp
    src/textchat.cpp
    src/proxy.cpp
    src/pubkeyaction.cpp
    src/raid.cpp
    src/raidproxy.cpp
    src/request.cpp
    src/serialize64.cpp
    src/nodemanager.cpp
    src/setandelement.cpp
    src/share.cpp
    src/sharenodekeys.cpp
    src/sync.cpp
    src/syncfilter.cpp
    src/heartbeats.cpp
    src/testhooks.cpp
    src/transfer.cpp
    src/transferslot.cpp
    src/transferstats.cpp
    src/treeproc.cpp
    src/user.cpp
    src/useralerts.cpp
    src/utils.cpp
    src/waiterbase.cpp
    src/crypto/cryptopp.cpp
    src/crypto/sodium.cpp
    src/db/sqlite.cpp
    src/process.cpp
    src/name_collision.cpp
    src/pwm_file_parser.cpp
    src/user_attribute.cpp
    src/user_attribute_definition.cpp
    src/user_attribute_manager.cpp
    src/megautils.cpp
)

target_sources(SDKlib
    PRIVATE
    ${SDKLIB_HEADERS}
    ${SDKLIB_SOURCES}
    ${SDKLIB_PUB_HEADERS}
    ${PROJECT_BINARY_DIR}/mega/config.h # Generated config.h file
)

# Files by platform and/or feature
# Files should appear only once.
# If the FLAG is not true for a file, it will be added as non-buildable source despite then the file is added again as a buildable one.
target_sources_conditional(SDKlib
    FLAG WIN32
    PRIVATE
    include/mega/win32/megafs.h
    include/mega/win32/megaconsolewaiter.h
    include/mega/win32/megaconsole.h
    include/mega/win32/megawaiter.h
    include/mega/win32/megasys.h
    include/mega/win32/meganet.h # it includes posix/meganet.h

    src/win32/fs.cpp
    src/win32/consolewaiter.cpp
    src/win32/console.cpp
    src/win32/waiter.cpp
    src/win32/net.cpp # it includes posix/net.cpp
)

target_sources_conditional(SDKlib
    FLAG WIN32 AND ENABLE_ISOLATED_GFX
    PRIVATE
    include/mega/win32/gfx/worker/comms.h
    include/mega/win32/gfx/worker/comms_client.h
    src/win32/gfx/worker/comms.cpp
    src/win32/gfx/worker/comms_client.cpp
)

target_sources_conditional(SDKlib
    FLAG UNIX AND ENABLE_ISOLATED_GFX
    PRIVATE
    include/mega/posix/gfx/worker/comms.h
    include/mega/posix/gfx/worker/comms_client.h
    include/mega/posix/gfx/worker/socket_utils.h
    src/posix/gfx/worker/comms.cpp
    src/posix/gfx/worker/comms_client.cpp
    src/posix/gfx/worker/socket_utils.cpp
)

target_sources_conditional(SDKlib
    FLAG ENABLE_ISOLATED_GFX
    PRIVATE
    include/mega/gfx/isolatedprocess.h
    include/mega/gfx/worker/tasks.h
    include/mega/gfx/worker/commands.h
    include/mega/gfx/worker/comms.h
    include/mega/gfx/worker/command_serializer.h
    include/mega/gfx/worker/client.h
    include/mega/gfx/worker/comms_client_common.h
    include/mega/gfx/worker/comms_client.h
    src/gfx/isolatedprocess.cpp
    src/gfx/worker/client.cpp
    src/gfx/worker/commands.cpp
    src/gfx/worker/command_serializer.cpp
)

target_sources_conditional(SDKlib
    FLAG UNIX
    PRIVATE
    include/mega/posix/megawaiter.h
    include/mega/thread/posixthread.h
    include/mega/posix/megaconsole.h
    include/mega/posix/megafs.h
    include/mega/posix/megaconsolewaiter.h
    include/mega/posix/meganet.h
    include/mega/posix/megasys.h

    src/posix/waiter.cpp
    src/thread/posixthread.cpp
    src/posix/console.cpp
    src/posix/fs.cpp
    src/posix/consolewaiter.cpp
    src/posix/net.cpp
)

target_sources_conditional(SDKlib
    FLAG APPLE
    PRIVATE
    include/mega/osx/osxutils.h
    include/mega/osx/megafs.h

    src/osx/osxutils.mm
    src/osx/fs.cpp
)

target_sources_conditional(SDKlib
    FLAG ENABLE_DRIVE_NOTIFICATIONS
    PRIVATE
    include/mega/drivenotify.h
    src/drivenotify.cpp
)

target_sources_conditional(SDKlib
    FLAG ENABLE_DRIVE_NOTIFICATIONS AND WIN32
    PRIVATE
    include/mega/win32/drivenotifywin.h
    src/win32/drivenotifywin.cpp
)

target_sources_conditional(SDKlib
    FLAG ENABLE_DRIVE_NOTIFICATIONS AND APPLE
    PRIVATE
    include/mega/osx/drivenotifyosx.h
    src/osx/drivenotifyosx.cpp
)

target_sources_conditional(SDKlib
    FLAG ENABLE_DRIVE_NOTIFICATIONS AND NOT (APPLE OR WIN32)
    PRIVATE
    include/mega/posix/drivenotifyposix.h
    src/posix/drivenotifyposix.cpp
)

target_sources_conditional(SDKlib
    FLAG USE_CPPTHREAD
    PRIVATE
    include/mega/thread/cppthread.h
    src/thread/cppthread.cpp
)

target_sources_conditional(SDKlib
    FLAG NOT HAVE_GLOB_H AND NOT WIN32
    PRIVATE
    include/mega/mega_glob.h
    src/mega_glob.c
)

target_sources_conditional(SDKlib
    FLAG USE_LIBUV
    PRIVATE
    include/mega/mega_evt_tls.h
    src/mega_evt_tls.cpp
)

# Include directories
target_include_directories(SDKlib
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include> # For the top level projects.
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}> # For the external projects.
#    PRIVATE # TODO: Private for SDK core
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
        $<$<BOOL:${APPLE}>:${CMAKE_CURRENT_SOURCE_DIR}/include/mega/osx>
        $<$<BOOL:${WIN32}>:${CMAKE_CURRENT_SOURCE_DIR}/include/mega/win32>
        $<$<BOOL:${UNIX}>:${CMAKE_CURRENT_SOURCE_DIR}/include/mega/posix>
    )

if (WIN32)
    target_compile_definitions(SDKlib
        PUBLIC # TODO: Private for SDK core
            HAVE_CONFIG_H # To include the config.h file in Windows builds
        PRIVATE
            _CRT_SECURE_NO_WARNINGS # warning in mega_ccronexpr
            $<$<BOOL:${USE_CPPTHREAD}>:USE_CPPTHREAD>
            UNICODE
            # Disable warning C4996: 'inet_ntoa': Use inet_ntop() or InetNtop() instead or define
            # _WINSOCK_DEPRECATED_NO_WARNINGS to disable deprecated API warnings
            _WINSOCK_DEPRECATED_NO_WARNINGS
    )

    # Increase number of sections in .obj files. (megaapi_impl.cpp, Sync_test.cpp, ...)
    target_compile_options(SDKlib PRIVATE /bigobj)

endif()

# Needed for megaapi.h. The top level projects usually don't include the config.h in the SDK, so needed SDK definitions are not available in that context.
# Only the ones used in megaapi.h are listed below.
target_compile_definitions(SDKlib
    PUBLIC
    $<$<BOOL:${ENABLE_LOG_PERFORMANCE}>:ENABLE_LOG_PERFORMANCE>
    $<$<BOOL:${ENABLE_CHAT}>:ENABLE_CHAT>
    $<$<BOOL:${ENABLE_SYNC}>:ENABLE_SYNC>
    $<$<BOOL:${USE_LIBUV}>:HAVE_LIBUV>
    $<$<PLATFORM_ID:iOS>:USE_IOS>
)

set_target_properties(SDKlib PROPERTIES
    VERSION ${PROJECT_VERSION}
    DEBUG_POSTFIX "d"
)

if(ENABLE_JAVA_BINDINGS OR ENABLE_PYTHON_BINDINGS OR ENABLE_PHP_BINDINGS)
    set_target_properties(SDKlib PROPERTIES
        POSITION_INDEPENDENT_CODE ON
    )
endif()

## Load and link needed libraries for the SDKlib target ##

# Load 3rd parties
load_sdklib_libraries()

# System libraries
if((NOT (WIN32 OR APPLE OR ANDROID)) AND CMAKE_CXX_STANDARD LESS_EQUAL 17)
    # Needed for std::experimental::filesystem
    # Needed for c++17 and std::filesystem for some compilers. Not needed starting in gcc9, but harmless.
    target_link_libraries(SDKlib PRIVATE stdc++fs)
endif()

if(ENABLE_DRIVE_NOTIFICATIONS)
    if(WIN32)
        target_link_libraries(SDKlib PRIVATE wbemuuid)
    elseif(NOT APPLE) # Linux
        target_link_libraries(SDKlib PRIVATE udev)
    endif()
    set(USE_DRIVE_NOTIFICATIONS 1)
endif()

if(WIN32)
    target_link_libraries(SDKlib PRIVATE
        ws2_32 winhttp Shlwapi Secur32.lib crypt32.lib Wldap32.lib
        $<$<BOOL:${USE_LIBUV}>:Kernel32.lib Iphlpapi.lib Userenv.lib Psapi.lib>
        $<$<BOOL:${USE_FFMPEG}>:Mfplat.lib mfuuid.lib strmiids.lib>
        $<$<BOOL:${ENABLE_DRIVE_NOTIFICATIONS}>:wbemuuid>
    )
else()
    if(APPLE)
        target_link_libraries(SDKlib PRIVATE
            "-framework CoreServices"
            "-framework Cocoa"
            "-framework SystemConfiguration"
            "-framework DiskArbitration"
            "-framework CoreFoundation"
        )
    endif()
endif()

## Adjust compilation flags for warnings and errors ##
target_platform_compile_options(
    TARGET SDKlib
    WINDOWS /W4
            /wd4201 # nameless struct/union (nonstandard)
            /wd4100 # unreferenced formal parameter
            /wd4706 # assignment within conditional
            /wd4458 # identifier hides class member
            /wd4324 # structure was padded due to alignment specifier (common in Sodium)
            /wd4456 # declaration hides previous local declaration
            /wd4266 # derived class did not override all overloads of a virtual function
            /we4800 # Implicit conversion from 'type' to bool. Possible information loss
            #TODO: remove some of those gradually.  also consider: /wd4503 /wd4996 /wd4702
    UNIX $<$<CONFIG:Debug>:-ggdb3> -Wall -Wextra -Wconversion -Wno-unused-parameter
)

if(ENABLE_SDKLIB_WERROR)
    target_platform_compile_options(
        TARGET SDKlib
        WINDOWS /WX
        UNIX  $<$<CONFIG:Debug>: -Werror
                                 -Wno-error=deprecated-declarations> # Kept as a warning, do not promote to error.
        APPLE $<$<CONFIG:Debug>: -Wno-sign-conversion                 -Wno-overloaded-virtual
                                 -Wno-inconsistent-missing-override
                                 -Wno-string-conversion
                                 -Wno-implicit-int-conversion
                                 -Wno-shorten-64-to-32                -Wno-unused-value
                                 -Wno-unqualified-std-cast-call>
    )
endif()

## Create config files ##
configure_file(
    contrib/cmake/config.h.in
    ${PROJECT_BINARY_DIR}/mega/config.h
    )

configure_package_config_file(
    contrib/cmake/modules/Config.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/sdklibConfig.cmake
    INSTALL_DESTINATION cmake
    )

set(SDKLIB_NAME "SDKlib")
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  set(SDKLIB_NAME "${SDKLIB_NAME}d")
endif()

configure_file(
    contrib/cmake/modules/sdklib.pc.in
    ${CMAKE_CURRENT_BINARY_DIR}/sdklib.pc @ONLY
    )

## Installation ##
if(SDKLIB_STANDALONE)

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
