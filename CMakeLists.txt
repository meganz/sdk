cmake_minimum_required(VERSION 3.6)
project (mega_sdk)

#set (CMAKE_CXX_STANDARD 11)

string(TOLOWER ${CMAKE_BUILD_TYPE} buildType)
IF (buildType STREQUAL "debug")
    add_definitions(-DDEBUG -D_DEBUG)
ENDIF()

IF(WIN32)
    #Link against the static C/C++ libraries on windows
    foreach(flag_var
            CMAKE_CXX_FLAGS CMAKE_CXX_FLAGS_DEBUG CMAKE_CXX_FLAGS_RELEASE 
            CMAKE_C_FLAGS CMAKE_C_FLAGS_DEBUG CMAKE_C_FLAGS_RELEASE
            CMAKE_CXX_FLAGS_MINSIZEREL CMAKE_CXX_FLAGS_RELWITHDEBINFO)
       if(${flag_var} MATCHES "/MD")
          string(REGEX REPLACE "/MD" "/MT" ${flag_var} "${${flag_var}}")
       endif(${flag_var} MATCHES "/MD")
    endforeach(flag_var)
ELSE(WIN32)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=gnu++11 -D_GLIBCXX_USE_CXX11_ABI=1") 
ENDIF(WIN32)

IF(WIN32)

    add_library(cryptopp STATIC IMPORTED)
    set_property(TARGET cryptopp PROPERTY INTERFACE_INCLUDE_DIRECTORIES "C:\\Users\\MATTW\\source\\repos\\3rdparty\\cryptopp565\\include")
    set_property(TARGET cryptopp PROPERTY IMPORTED_LOCATION "C:\\Users\\MATTW\\source\\repos\\3rdparty\\cryptopp565\\Win32\\Output\\Debug\\cryptlib.lib")

    add_library(curl STATIC IMPORTED)
    set_property(TARGET curl PROPERTY INTERFACE_INCLUDE_DIRECTORIES "C:\\Users\\MATTW\\source\\repos\\3rdparty\\curl-7.56.0\\include")
    set_property(TARGET curl PROPERTY IMPORTED_LOCATION "C:\\Users\\MATTW\\source\\repos\\3rdparty\\curl-7.56.0\\builds\\libcurl-vc-x86-debug-static-ssl-static-cares-static-zlib-static-ipv6-sspi\\lib\\libcurl_a_debug.lib")

    add_library(cares STATIC IMPORTED)
    set_property(TARGET cares PROPERTY INTERFACE_INCLUDE_DIRECTORIES "C:\\Users\\MATTW\\source\\repos\\3rdparty\\c-ares-1.13.0\\include")
    set_property(TARGET cares PROPERTY IMPORTED_LOCATION "C:\\Users\\MATTW\\source\\repos\\3rdparty\\c-ares-1.13.0\\msvc140\\cares\\lib-debug\\libcaresd.lib")

    add_library(ssl STATIC IMPORTED)
    set_property(TARGET ssl PROPERTY INTERFACE_INCLUDE_DIRECTORIES "C:\\Users\\MATTW\\source\\repos\\3rdparty\\openssl-1.1.0f\\include")
    set_property(TARGET ssl PROPERTY IMPORTED_LOCATION "C:\\Users\\MATTW\\source\\repos\\3rdparty\\openssl-1.1.0f\\libssl.lib")

    add_library(zlib STATIC IMPORTED)
    set_property(TARGET zlib PROPERTY INTERFACE_INCLUDE_DIRECTORIES "C:\\Users\\MATTW\\source\\repos\\3rdparty\\zlib-1.2.11")
    set_property(TARGET zlib PROPERTY IMPORTED_LOCATION "/home/mattw/repo/libz.a")

    add_library(gtest STATIC IMPORTED)
    set_property(TARGET gtest PROPERTY INTERFACE_INCLUDE_DIRECTORIES "C:\\Users\\MATTW\\source\\repos\\3rdparty\\googletest\\googletest\\include")
    set_property(TARGET gtest PROPERTY IMPORTED_LOCATION "C:\\Users\\MATTW\\source\\repos\\sdk2\\examples\\win32\\testmega\\MEGA\\Win32-Debug\\gtestd.lib")

    add_library(mediainfo STATIC IMPORTED)
    set_property(TARGET mediainfo PROPERTY INTERFACE_INCLUDE_DIRECTORIES "C:\\Users\\MATTW\\source\\repos\\3rdparty\\mediainfolib\\Source")
    set_property(TARGET mediainfo PROPERTY IMPORTED_LOCATION "C:\\Users\\MATTW\\source\\repos\\3rdparty\\MediaInfoLib\\Project\\MSVC2017\\Win32\\Debug\\MediaInfo-Static.lib")

    add_library(zen STATIC IMPORTED)
    set_property(TARGET zen PROPERTY INTERFACE_INCLUDE_DIRECTORIES "C:\\Users\\MATTW\\source\\repos\\3rdparty\\ZenLib\\Source")
    set_property(TARGET zen PROPERTY IMPORTED_LOCATION "C:\\Users\\MATTW\\source\\repos\\3rdparty\\ZenLib\\Project\\MSVC2017\\Win32\\Debug\\ZenLib.lib")

    add_library(sqlite3 STATIC "C:/Users/MATTW/source/repos/3rdparty/sqlite-3.20.1/sqlite3.c")
    set_property(TARGET sqlite3 PROPERTY INTERFACE_INCLUDE_DIRECTORIES "C:\\Users\\MATTW\\source\\repos\\3rdparty\\sqlite-3.20.1")


    add_definitions(-D_CRT_SECURE_NO_WARNINGS -DCURL_STATICLIB -DCARES_STATICLIB -DWIN32_LEAN_AND_MEAN -DUNICODE -DSODIUM_STATIC -DPCRE_STATICWIN32 -D_CONSOLE)
    SET(Mega_PlatformSpecificFiles src/win32/console.cpp src/win32/consolewaiter.cpp src/win32/fs.cpp src/posix/net.cpp src/win32/waiter.cpp src/thread/win32thread.cpp )
    SET(Mega_PlatformSpecificIncludes include/mega/wincurl)
    SET(Mega_PlatformSpecificLibs ws2_32 winhttp Shlwapi)

ELSE(WIN32)

    add_library(mediainfo STATIC IMPORTED)
    set_property(TARGET mediainfo PROPERTY INTERFACE_INCLUDE_DIRECTORIES "/home/mattw/repo/MediaInfoLib/Source/")
    set_property(TARGET mediainfo PROPERTY IMPORTED_LOCATION "/home/mattw/repo/MediaInfoLib/Project/CMake/libmediainfo.a")

    add_library(zen STATIC IMPORTED)
    set_property(TARGET zen PROPERTY INTERFACE_INCLUDE_DIRECTORIES "/home/mattw/repo/ZenLib/Source/")
    set_property(TARGET zen PROPERTY IMPORTED_LOCATION "/home/mattw/repo/MediaInfoLib/Project/CMake/ZenLib/libzen.a")

    add_library(zlib SHARED IMPORTED)
    set_property(TARGET zlib PROPERTY INTERFACE_INCLUDE_DIRECTORIES "/home/mattw/repo/zlib-1.2.11")
    set_property(TARGET zlib PROPERTY IMPORTED_LOCATION "/home/mattw/repo/zlib-1.2.11/libz.so")
    set_property(TARGET zlib PROPERTY IMPORTED_IMPLIB "/home/mattw/repo/zlib-1.2.11/libz.a")

    add_definitions(-DUSE_PTHREAD )
    SET(Mega_PlatformSpecificFiles src/posix/console.cpp src/posix/consolewaiter.cpp src/posix/fs.cpp src/posix/net.cpp src/posix/waiter.cpp src/thread/posixthread.cpp )
    SET(Mega_PlatformSpecificIncludes include/mega/posix)
    SET(Mega_PlatformSpecificLibs crypto pthread rt)

ENDIF(WIN32)

add_definitions(-DUSE_CRYPTOPP -DUSE_CURL -DUSE_SQLITE -DUSE_CRYPTOPP -DUSE_SODIUM -DUSE_CURL -DUSE_OPENSSL -DUSE_MEDIAINFO -DENABLE_SYNC)

SET(Mega_CryptoFiles src/crypto/cryptopp.cpp src/crypto/sodium.cpp)
SET(Mega_DbFiles src/db/sqlite.cpp src/db/sqlite.cpp )
SET(Mega_GfxFiles src/gfx/external.cpp src/gfx/freeimage.cpp ) 

add_library(Mega STATIC
            src/attrmap.cpp src/backofftimer.cpp src/base64.cpp src/command.cpp src/commands.cpp src/db.cpp src/file.cpp src/fileattributefetch.cpp src/filefingerprint.cpp src/filesystem.cpp src/gfx.cpp src/http.cpp src/json.cpp src/logging.cpp  
            src/mediafileattribute.cpp src/mega_http_parser.cpp src/mega_utf8proc.cpp src/mega_zxcvbn.cpp src/megaapi.cpp src/megaapi_impl.cpp src/megaclient.cpp src/node.cpp src/pendingcontactrequest.cpp src/proxy.cpp src/pubkeyaction.cpp src/request.cpp
            src/serialize64.cpp src/share.cpp src/sharenodekeys.cpp src/sync.cpp  src/transfer.cpp src/transferslot.cpp src/treeproc.cpp src/user.cpp src/utils.cpp src/waiterbase.cpp 
            ${Mega_PlatformSpecificFiles} ${Mega_CryptoFiles} ${Mega_DbFiles} ${Mega_GfxFiles}  )

target_include_directories(Mega PRIVATE include ${Mega_PlatformSpecificIncludes})
target_include_directories(Mega INTERFACE include ${Mega_PlatformSpecificIncludes})
target_link_libraries(Mega cryptopp curl cares ssl sqlite3 mediainfo zen zlib ${Mega_PlatformSpecificLibs})

add_executable(sdk_tests tests/sdk_test.cpp tests/sdktests.cpp)
target_link_libraries(sdk_tests gtest Mega )
configure_file(logo.png logo.png COPYONLY)

add_executable(testmega examples/win32/testmega/main.cpp)
target_link_libraries(testmega Mega)


#enable_testing()
#add_test(NAME SdkTest COMMAND sdk_tests )
