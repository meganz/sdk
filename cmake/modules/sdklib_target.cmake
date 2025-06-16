## SDKlib target ##
add_library(SDKlib)
add_library(MEGA::SDKlib ALIAS SDKlib)

# Convenience.
set(SDKLIB_COMMON_INC include/mega/common)
set(SDKLIB_COMMON_SRC src/common)

set(SDKLIB_COMMON_HEADERS
    ${SDKLIB_COMMON_INC}/activity_monitor.h
    ${SDKLIB_COMMON_INC}/activity_monitor_forward.h
    ${SDKLIB_COMMON_INC}/badge.h
    ${SDKLIB_COMMON_INC}/badge_forward.h
    ${SDKLIB_COMMON_INC}/client.h
    ${SDKLIB_COMMON_INC}/client_adapter.h
    ${SDKLIB_COMMON_INC}/client_callbacks.h
    ${SDKLIB_COMMON_INC}/client_forward.h
    ${SDKLIB_COMMON_INC}/database.h
    ${SDKLIB_COMMON_INC}/database_builder.h
    ${SDKLIB_COMMON_INC}/database_forward.h
    ${SDKLIB_COMMON_INC}/database_utilities.h
    ${SDKLIB_COMMON_INC}/deciseconds.h
    ${SDKLIB_COMMON_INC}/directory.h
    ${SDKLIB_COMMON_INC}/error_or.h
    ${SDKLIB_COMMON_INC}/error_or_forward.h
    ${SDKLIB_COMMON_INC}/expected.h
    ${SDKLIB_COMMON_INC}/expected_forward.h
    ${SDKLIB_COMMON_INC}/lock.h
    ${SDKLIB_COMMON_INC}/lock_forward.h
    ${SDKLIB_COMMON_INC}/lockable.h
    ${SDKLIB_COMMON_INC}/lockable_forward.h
    ${SDKLIB_COMMON_INC}/logger.h
    ${SDKLIB_COMMON_INC}/logger_forward.h
    ${SDKLIB_COMMON_INC}/logging.h
    ${SDKLIB_COMMON_INC}/node_event.h
    ${SDKLIB_COMMON_INC}/node_event_forward.h
    ${SDKLIB_COMMON_INC}/node_event_observer.h
    ${SDKLIB_COMMON_INC}/node_event_observer_forward.h
    ${SDKLIB_COMMON_INC}/node_event_queue.h
    ${SDKLIB_COMMON_INC}/node_event_queue_forward.h
    ${SDKLIB_COMMON_INC}/node_event_type.h
    ${SDKLIB_COMMON_INC}/node_event_type_forward.h
    ${SDKLIB_COMMON_INC}/node_info.h
    ${SDKLIB_COMMON_INC}/node_info_forward.h
    ${SDKLIB_COMMON_INC}/normalized_path.h
    ${SDKLIB_COMMON_INC}/normalized_path_forward.h
    ${SDKLIB_COMMON_INC}/partial_download.h
    ${SDKLIB_COMMON_INC}/partial_download_callback.h
    ${SDKLIB_COMMON_INC}/partial_download_callback_forward.h
    ${SDKLIB_COMMON_INC}/partial_download_forward.h
    ${SDKLIB_COMMON_INC}/pending_callbacks.h
    ${SDKLIB_COMMON_INC}/query.h
    ${SDKLIB_COMMON_INC}/query_forward.h
    ${SDKLIB_COMMON_INC}/scoped_query.h
    ${SDKLIB_COMMON_INC}/scoped_query_forward.h
    ${SDKLIB_COMMON_INC}/serialization_traits.h
    ${SDKLIB_COMMON_INC}/serialization_traits_forward.h
    ${SDKLIB_COMMON_INC}/shared_mutex.h
    ${SDKLIB_COMMON_INC}/shared_mutex_forward.h
    ${SDKLIB_COMMON_INC}/status_flag.h
    ${SDKLIB_COMMON_INC}/subsystem_logger.h
    ${SDKLIB_COMMON_INC}/task_executor.h
    ${SDKLIB_COMMON_INC}/task_executor_flags.h
    ${SDKLIB_COMMON_INC}/task_executor_flags_forward.h
    ${SDKLIB_COMMON_INC}/task_executor_forward.h
    ${SDKLIB_COMMON_INC}/task_queue.h
    ${SDKLIB_COMMON_INC}/task_queue_forward.h
    ${SDKLIB_COMMON_INC}/transaction.h
    ${SDKLIB_COMMON_INC}/transaction_forward.h
    ${SDKLIB_COMMON_INC}/type_traits.h
    ${SDKLIB_COMMON_INC}/unexpected.h
    ${SDKLIB_COMMON_INC}/unexpected_forward.h
    ${SDKLIB_COMMON_INC}/upload.h
    ${SDKLIB_COMMON_INC}/upload_callbacks.h
    ${SDKLIB_COMMON_INC}/upload_forward.h
    ${SDKLIB_COMMON_INC}/utility.h
)

set(SDKLIB_COMMON_SOURCES
    ${SDKLIB_COMMON_SRC}/activity_monitor.cpp
    ${SDKLIB_COMMON_SRC}/client.cpp
    ${SDKLIB_COMMON_SRC}/client_adapter.cpp
    ${SDKLIB_COMMON_SRC}/database.cpp
    ${SDKLIB_COMMON_SRC}/database_builder.cpp
    ${SDKLIB_COMMON_SRC}/directory.cpp
    ${SDKLIB_COMMON_SRC}/logger.cpp
    ${SDKLIB_COMMON_SRC}/node_event_type.cpp
    ${SDKLIB_COMMON_SRC}/normalized_path.cpp
    ${SDKLIB_COMMON_SRC}/pending_callbacks.cpp
    ${SDKLIB_COMMON_SRC}/query.cpp
    ${SDKLIB_COMMON_SRC}/scoped_query.cpp
    ${SDKLIB_COMMON_SRC}/shared_mutex.cpp
    ${SDKLIB_COMMON_SRC}/subsystem_logger.cpp
    ${SDKLIB_COMMON_SRC}/task_executor.cpp
    ${SDKLIB_COMMON_SRC}/task_queue.cpp
    ${SDKLIB_COMMON_SRC}/transaction.cpp
    ${SDKLIB_COMMON_SRC}/upload.cpp
    ${SDKLIB_COMMON_SRC}/utility.cpp
)

# Assume the sync engine isn't being built.
set(CLIENT_ADAPTER_FILE client_adapter_without_sync)

# Sync engine is being built.
if (ENABLE_SYNC)
    set(CLIENT_ADAPTER_FILE client_adapter_with_sync)
endif()

# Add sync-specific client adapter sources.
set(SDKLIB_COMMON_SOURCES
    ${SDKLIB_COMMON_SOURCES}
    ${SDKLIB_COMMON_SRC}/${CLIENT_ADAPTER_FILE}.cpp
)

# Cleanup.
unset(CLIENT_ADAPTER_FILE)

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
    include/mega/canceller.h
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
    include/mega/localpath.h
    include/mega/filesystem.h
    include/mega/backofftimer.h
    include/mega/raid.h
    include/mega/raidproxy.h
    include/mega/logging.h
    include/mega/file.h
    include/mega/sync.h
    include/mega/syncfilter.h
    include/mega/syncinternals/syncinternals_logging.h
    include/mega/syncinternals/syncinternals.h
    include/mega/syncinternals/synciuploadthrottlingmanager.h
    include/mega/syncinternals/syncuploadthrottlingfile.h
    include/mega/syncinternals/syncuploadthrottlingmanager.h
    include/mega/heartbeats.h
    include/mega/utils.h
    include/mega/hashcash.h
    include/mega/utils_optional.h
    include/mega/account.h
    include/mega/transfer.h
    include/mega/transferstats.h
    include/mega/totp.h
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
    include/mega/tlv.h
    include/mega/dns_lookup_pseudomessage.h
    include/mega/network_connectivity_test.h
    include/mega/network_connectivity_test_helpers.h
    include/mega/udp_socket.h
    include/mega/udp_socket_tester.h
    include/mega/user_attribute.h
    include/mega/user_attribute_definition.h
    include/mega/user_attribute_manager.h
    include/mega/user_attribute_types.h
    include/mega/log_level.h
    include/mega/log_level_forward.h
    include/mega/overloaded.h

    # megaapi_impl related headers
    include/impl/share.h

    ${SDKLIB_COMMON_HEADERS}
)

set(SDKLIB_SOURCES
    src/megaapi.cpp
    src/megaapi_impl.cpp
    src/megaapi_impl_sync.cpp
    src/arguments.cpp
    src/attrmap.cpp
    src/autocomplete.cpp
    src/backofftimer.cpp
    src/base64.cpp
    src/canceller.cpp
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
    src/hashcash.cpp
    src/http.cpp
    src/json.cpp
    src/logging.cpp
    src/localpath.cpp
    src/mediafileattribute.cpp
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
    src/syncinternals/syncinternals.cpp
    src/syncinternals/syncuploadthrottlingfile.cpp
    src/syncinternals/syncuploadthrottlingmanager.cpp
    src/heartbeats.cpp
    src/testhooks.cpp
    src/transfer.cpp
    src/transferslot.cpp
    src/transferstats.cpp
    src/treeproc.cpp
    src/totp.cpp
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
    src/tlv.cpp
    src/dns_lookup_pseudomessage.cpp
    src/network_connectivity_test.cpp
    src/udp_socket.cpp
    src/udp_socket_tester.cpp
    src/user_attribute.cpp
    src/user_attribute_definition.cpp
    src/user_attribute_manager.cpp
    src/megautils.cpp
    src/log_level.cpp

    # megaapi_impl related sources
    src/impl/share.cpp

    ${SDKLIB_COMMON_SOURCES}
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
    FLAG ANDROID
    PRIVATE
    include/mega/android/androidFileSystem.h
    include/mega/android/megafs.h
    src/android/androidFileSystem.cpp
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
        PRIVATE
            _CRT_SECURE_NO_WARNINGS # warning in ccronexpr
            $<$<BOOL:${USE_CPPTHREAD}>:USE_CPPTHREAD>
            UNICODE
            # Disable warning C4996: 'inet_ntoa': Use inet_ntop() or InetNtop() instead or define
            # _WINSOCK_DEPRECATED_NO_WARNINGS to disable deprecated API warnings
            _WINSOCK_DEPRECATED_NO_WARNINGS
    )

    # Increase number of sections in .obj files. (megaapi_impl.cpp, Sync_test.cpp, ...)
    target_compile_options(SDKlib PRIVATE /bigobj)

endif()

target_compile_definitions(SDKlib
    PUBLIC
    $<$<BOOL:${ENABLE_LOG_PERFORMANCE}>:ENABLE_LOG_PERFORMANCE>
    $<$<BOOL:${ENABLE_CHAT}>:ENABLE_CHAT>
    $<$<BOOL:${ENABLE_SYNC}>:ENABLE_SYNC>
    $<$<BOOL:${USE_LIBUV}>:HAVE_LIBUV>
    $<$<PLATFORM_ID:iOS>:USE_IOS>
    $<$<PLATFORM_ID:Android>:USE_POLL>
    $<$<PLATFORM_ID:Android>:USE_INOTIFY>
    $<$<PLATFORM_ID:Android>:HAVE_SDK_CONFIG_H>
)

set_target_properties(SDKlib PROPERTIES
    VERSION ${PROJECT_VERSION}
    DEBUG_POSTFIX "d"
)

if(ENABLE_JAVA_BINDINGS)
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
    UNIX $<$<CONFIG:Debug>:-ggdb3> -Wall -Wextra -Wconversion
)

if(ENABLE_SDKLIB_WERROR)
    target_platform_compile_options(
        TARGET SDKlib
        WINDOWS /WX
        UNIX  $<$<CONFIG:Debug>: -Werror
                                 -Wno-error=deprecated-declarations> # Kept as a warning, do not promote to error.
    )
    if(WIN32)
        set_source_files_properties(
            src/mega_ccronexpr.cpp
            src/mega_zxcvbn.cpp
            PROPERTIES
            COMPILE_FLAGS "/wd4456" # declaration hides previous local declaration
        )
    endif()
    if(APPLE)
        set_source_files_properties(
            src/mega_http_parser.cpp
            src/mega_utf8proc.cpp
            src/mega_zxcvbn.cpp
            PROPERTIES 
            COMPILE_FLAGS "-Wno-sign-conversion"
        )
    endif()
endif()

## Create config files ##
configure_file(
    cmake/config.h.in
    ${PROJECT_BINARY_DIR}/mega/config.h
    )

configure_package_config_file(
    cmake/modules/Config.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/sdklibConfig.cmake
    INSTALL_DESTINATION cmake
    )

set(SDKLIB_NAME "SDKlib")
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  set(SDKLIB_NAME "${SDKLIB_NAME}d")
endif()

configure_file(
    cmake/modules/sdklib.pc.in
    ${CMAKE_CURRENT_BINARY_DIR}/sdklib.pc @ONLY
    )
