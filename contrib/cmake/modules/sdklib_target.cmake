## SDKlib target ##
add_library(SDKlib)
add_library(MEGA::SDKlib ALIAS SDKlib)

set(SDKLIB_PUB_HEADERS
    include/megaapi.h
)

set(SDKLIB_HEADERS
    include/mega.h
    include/megaapi_impl.h
    include/mega/transferslot.h
    include/mega/thread/libuvthread.h
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
    include/mega/logging.h
    include/mega/file.h
    include/mega/sync.h
    include/mega/syncfilter.h
    include/mega/heartbeats.h
    include/mega/utils.h
    include/mega/account.h
    include/mega/transfer.h
    include/mega/config-android.h
    include/mega/treeproc.h
    include/mega/attrmap.h
    include/mega/sharenodekeys.h
    include/mega/request.h
    include/mega/mega_zxcvbn.h
    include/mega/fileattributefetch.h
    include/mega/version.h
    include/mega/node.h
    include/mega/mediafileattribute.h
    include/mega/process.h
)

set(SDKLIB_SOURCES
    src/megaapi.cpp
    src/megaapi_impl.cpp
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
    src/treeproc.cpp
    src/user.cpp
    src/useralerts.cpp
    src/utils.cpp
    src/waiterbase.cpp
    src/crypto/cryptopp.cpp
    src/crypto/sodium.cpp
    src/db/sqlite.cpp
    src/process.cpp
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
    FLAG WIN32 AND USE_CURL
    PRIVATE
    include/mega/wincurl/megafs.h # win32/megafs.h
    include/mega/wincurl/megaconsolewaiter.h # win32/megaconsolewaiter.h
    include/mega/wincurl/megaconsole.h # win32/megaconsole.h
    include/mega/wincurl/megawaiter.h # win32/megawaiter.h
    include/mega/wincurl/meganet.h # posix/meganet.h
)

target_sources_conditional(SDKlib
    FLAG WIN32 AND NOT USE_CURL
    PRIVATE
    include/mega/win32/megafs.h
    include/mega/win32/megaconsolewaiter.h
    include/mega/win32/megaconsole.h
    include/mega/win32/megawaiter.h
    include/mega/win32/meganet.h
)

target_sources_conditional(SDKlib
    FLAG WIN32
    PRIVATE
    src/win32/console.cpp
    src/win32/consolewaiter.cpp
    src/win32/fs.cpp
    # src/win32/net.cpp # when not using curl. # TODO Remove support for winhttp?
    src/win32/waiter.cpp
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

    src/posix/waiter.cpp
    src/thread/posixthread.cpp
    src/posix/console.cpp
    src/posix/fs.cpp
    src/posix/consolewaiter.cpp
)

target_sources_conditional(SDKlib
    FLAG UNIX OR (WIN32 AND USE_CURL)
    PRIVATE
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
    PRIVATE # Internal and private headers
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_BINARY_DIR}
        $<$<BOOL:${APPLE}>:${CMAKE_CURRENT_SOURCE_DIR}/include/mega/osx>
        $<$<BOOL:${WIN32}>:${CMAKE_CURRENT_SOURCE_DIR}/include/mega/$<IF:${USE_CURL},wincurl,win32>>
        $<$<BOOL:${UNIX}>:${CMAKE_CURRENT_SOURCE_DIR}/include/mega/posix>
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include> # For the top level projects.
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}> # For the external projects.
    )

if (WIN32)
    target_compile_definitions(SDKlib
        PRIVATE
        HAVE_CONFIG_H # To include the config.h file in Windows builds
        _CRT_SECURE_NO_WARNINGS # warning in mega_ccronexpr
        $<$<BOOL:${USE_CPPTHREAD}>:USE_CPPTHREAD>
        UNICODE
        NOMINMAX # TODO Fix locally
    )

    # Increase number of sections in .obj files. (megaapi_impl.cpp, Sync_test.cpp, ...)
    target_compile_options(SDKlib PRIVATE /bigobj)

endif()

set_target_properties(SDKlib PROPERTIES
    VERSION ${PROJECT_VERSION}
    DEBUG_POSTFIX "d"
)

## Load and link needed libraries for the SDKlib target ##

# Load 3rd parties
load_sdklib_libraries()

# System libraries
# TODO SET(Mega_PlatformSpecificLibs ${Mega_PlatformSpecificLibs} pthread z dl termcap) Linux
# TODO SET(Mega_PlatformSpecificLibs ${Mega_PlatformSpecificLibs} crypto rt stdc++fs) Linux
if((NOT (WIN32 OR APPLE)) AND CMAKE_CXX_STANDARD LESS_EQUAL 17)
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
            "-framework Cocoa"
            "-framework SystemConfiguration"
            "-framework DiskArbitration"
            "-framework CoreFoundation"
        )
    endif()
endif()

## Adjust compilation flags for warnigns and errors ##
if(WIN32)
    if(ENABLE_SDKLIB_WERROR)
        target_compile_options(SDKlib PRIVATE /WX)
    endif()

    target_compile_options(SDKlib
        PRIVATE
        /W4
        /wd4201 # nameless struct/union (nonstandard)
        /wd4100 # unreferenced formal parameter
        /wd4706 # assignment within conditional
        /wd4458 # identifier hides class member
        /wd4324 # structure was padded due to alignment specifier (common in Sodium)
        /wd4456 # declaration hides previous local declaration
        /wd4266 # derived class did not override all overloads of a virtual function
    )
    #TODO: remove some of those gradually.  also consider: /wd4503 /wd4996 /wd4702

else()

    target_compile_options(SDKlib
        PRIVATE
        $<$<CONFIG:Debug>:-ggdb3>
        -Wall         -Wextra
        -Wconversion  -Wno-unused-parameter
    )

    if(ENABLE_SDKLIB_WERROR AND CMAKE_BUILD_TYPE STREQUAL "Debug")
        target_compile_options(SDKlib PRIVATE -Werror)
        # Warnings which should not be promoted to errors, but still appear as warnings
        target_compile_options(SDKlib PRIVATE -Wno-error=deprecated-declarations)
        if(APPLE)
            target_compile_options(SDKlib
                PRIVATE
                -Wno-sign-conversion                 -Wno-overloaded-virtual
                -Wno-inconsistent-missing-override   -Wno-unused-variable
                -Wno-unused-private-field            -Wno-string-conversion
                -Wno-unused-lambda-capture           -Wno-implicit-int-conversion
                -Wno-shorten-64-to-32                -Wno-unused-value
                -Wno-unqualified-std-cast-call
            )
        endif()
    endif()

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
