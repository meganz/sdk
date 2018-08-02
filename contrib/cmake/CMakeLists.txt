# This file enables building with cmake on windows  
#
# use cases
#  - generate a visual studio solution and projects, eg: cmake . -G "Visual Studio 15 2017"
#  - or for 64 bit: cmake . -G "Visual Studio 15 2017 Win64"
#  - or set your build options before VS project generation by using the gui, eg:  cmake-gui.exe .
#  - open this CMakeLists.txt as a 'cmake project' with Visual Studio 2017 or beyond, and you can build and test/debug on windows or on Linux from one IDE (eg. a virtualbox linux instance on the same machine)
#       (though it seems in order for it to copy the needed build tree, this file must be copied up one level (to the mega root dir) and opened as a CMake project in VS from there
#  - you can set up to build both 32 bit and 64 bit this way:
#       make subfolders '32' and '64' of this cmake folder
#       execute 'cmake-gui  ..' in each.  Select the cmake folder as the 'source code' folder, and the appropriate 32 or 64 folder as 'where to build the binaries' folder
#       for each cmake-gui configuration, choose the suitable 32/64 bit compiler, and corresponding setting for the build_64_bit variable.
#       then Configure, Generate, Open Project (all buttons in the gui) for each.

# updated to build mostly against dependent libraries that were built with vcpkg
# you can very easily build those with commands like this in your 3rdParty folder:
#git clone https://github.com/Microsoft/vcpkg.git
#cd vcpkg
#.\bootstrap-vcpkg.bat
#.\vcpkg.exe install --triplet x64-windows-static zlib
#.\vcpkg.exe install --triplet x64-windows-static cryptopp
#.\vcpkg.exe install --triplet x64-windows-static libsodium  (x86 for windows, x64 for linux fail currently)
#.\vcpkg.exe install --triplet x64-windows-static openssl
#.\vcpkg.exe install --triplet x64-windows-static c-ares
#.\vcpkg.exe install --triplet x64-windows-static curl    
#.\vcpkg.exe install --triplet x64-windows-static sqlite3
#.\vcpkg.exe install --triplet x64-windows-static libevent

cmake_minimum_required(VERSION 3.8)

#set this to 1 if building 64 bit with eg. cmake . -G "Visual Studio 15 2017 Win64"
#set this to 0 if building 32 bit with eg. cmake . -G "Visual Studio 15 2017"
set (build_64_bit 1 CACHE TYPE BOOL)

#indicate which dependent libraries to use in the build
set (USE_CRYPTOPP 1 CACHE TYPE BOOL)
set (USE_OPENSSL 0 CACHE TYPE BOOL)
set (OPENSSL_IS_BORINGSSL 0 CACHE TYPE BOOL)
set (USE_CURL 0 CACHE TYPE BOOL)
set (USE_SQLITE 1 CACHE TYPE BOOL)
set (USE_MEDIAINFO 1 CACHE TYPE BOOL)
set (USE_FREEIMAGE 1 CACHE TYPE BOOL)
set (USE_SODIUM 1 CACHE TYPE BOOL)
set (ENABLE_SYNC 1 CACHE TYPE BOOL)
set (ENABLE_CHAT 1 CACHE TYPE BOOL)

if (WIN32)
    set (NO_READLINE 1 CACHE TYPE BOOL)
else(WIN32)
    set(NO_READLINE 0)
endif(WIN32)

if(build_64_bit)
    project (MegaSDK64 LANGUAGES CXX C)
else(build_64_bit)
    project (MegaSDK32 LANGUAGES CXX C)
endif(build_64_bit)

IF(WIN32)
    set(MegaDir "${CMAKE_CURRENT_LIST_DIR}/../..")
    if ("${Mega3rdPartyDir}" STREQUAL "")
        set(Mega3rdPartyDir "${MegaDir}/../3rdparty")
    endif()
ELSE(WIN32)
    set(MegaDir "${CMAKE_CURRENT_LIST_DIR}")
ENDIF(WIN32)

IF(WIN32)
    if(build_64_bit)
        set(vcpkg_dir "${Mega3rdPartyDir}/vcpkg/installed/x64-windows-static")
    else(build_64_bit)
        set(vcpkg_dir "${Mega3rdPartyDir}/vcpkg/installed/x86-windows-static")
    endif(build_64_bit)
ENDIF(WIN32)

if (NOT CMAKE_BUILD_TYPE)
    message("Generated with config types: ${CMAKE_CONFIGURATION_TYPES}")
else(NOT CMAKE_BUILD_TYPE)
    message("CMAKE_BUILD_TYPE is ${CMAKE_BUILD_TYPE}")
endif(NOT CMAKE_BUILD_TYPE)

#windows projects usually need _DEBUG and/or DEBUG set rather than NDEBUG not set
set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -D_DEBUG -DDEBUG")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -D_DEBUG -DDEBUG")

# accurate __cplusplus macro for vc++, selecting c++17 here for windows builds though the MEGA SDK library must build for older c++ standards also
if(WIN32)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /Zc:__cplusplus /std:c++17")
endif()

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
ENDIF(WIN32)


function(ImportStaticLibrary libName includeDir lib32debug lib32release lib64debug lib64release)
    add_library(${libName} STATIC IMPORTED)
    set_property(TARGET ${libName} PROPERTY INTERFACE_INCLUDE_DIRECTORIES ${includeDir})
    if(build_64_bit)
        set_property(TARGET ${libName} PROPERTY IMPORTED_LOCATION_DEBUG ${lib64debug})
        set_property(TARGET ${libName} PROPERTY IMPORTED_LOCATION_RELEASE  ${lib64release})
    else(build_64_bit)
        set_property(TARGET ${libName} PROPERTY IMPORTED_LOCATION_DEBUG ${lib32debug})
        set_property(TARGET ${libName} PROPERTY IMPORTED_LOCATION_RELEASE  ${lib32release})
    endif(build_64_bit)
endfunction(ImportStaticLibrary)

function(ImportVcpkgLibrary libName includeDir libdebug librelease)
    add_library(${libName} STATIC IMPORTED)
    set_property(TARGET ${libName} PROPERTY INTERFACE_INCLUDE_DIRECTORIES ${includeDir})
    set_property(TARGET ${libName} PROPERTY IMPORTED_LOCATION_DEBUG ${libdebug})
    set_property(TARGET ${libName} PROPERTY IMPORTED_LOCATION_RELEASE  ${librelease})
endfunction(ImportVcpkgLibrary)

function(ImportHeaderLibrary libName includeDir)
    add_library(${libName} INTERFACE IMPORTED)
    set_property(TARGET ${libName} PROPERTY INTERFACE_INCLUDE_DIRECTORIES ${includeDir})
endfunction(ImportHeaderLibrary)

IF(WIN32)

    IF(USE_CRYPTOPP)
    ImportVcpkgLibrary(cryptopp        "${vcpkg_dir}/include" "${vcpkg_dir}/debug/lib/cryptopp-static.lib"  "${vcpkg_dir}/lib/cryptopp-static.lib")
    ENDIF(USE_CRYPTOPP)

    IF(USE_SODIUM)
        if (build_64_bit)
            ImportVcpkgLibrary(sodium          "${vcpkg_dir}/include" "${vcpkg_dir}/debug/lib/libsodium.lib"  "${vcpkg_dir}/lib/libsodium.lib")
        else()
            ImportStaticLibrary(sodium          "${Mega3rdPartyDir}/libsodium-1.0.15/src/libsodium/include"
                                                "${Mega3rdPartyDir}/libsodium-1.0.15/Build/Debug/Win32/libsodium.lib" 
                                                "${Mega3rdPartyDir}/libsodium-1.0.15/Build/Release/Win32/libsodium.lib" 
                                                "${Mega3rdPartyDir}/libsodium-1.0.15/Build/Debug/x64/libsodium.lib" 
                                                "${Mega3rdPartyDir}/libsodium-1.0.15/Build/Release/x64/libsodium.lib")
        endif()
    ENDIF(USE_SODIUM)

    IF(USE_CURL)
        ImportVcpkgLibrary(curl        "${vcpkg_dir}/include" "${vcpkg_dir}/debug/lib/libcurl.lib"  "${vcpkg_dir}/lib/libcurl.lib")
        ImportVcpkgLibrary(cares       "${vcpkg_dir}/include" "${vcpkg_dir}/debug/lib/cares.lib" "${vcpkg_dir}/lib/cares.lib")
    ENDIF(USE_CURL)

    IF(USE_OPENSSL)
        ImportVcpkgLibrary(ssl         "${vcpkg_dir}/include" "${vcpkg_dir}/debug/lib/ssleay32.lib" "${vcpkg_dir}/lib/ssleay32.lib")
        ImportVcpkgLibrary(sslcrypto   "${vcpkg_dir}/include" "${vcpkg_dir}/debug/lib/libeay32.lib" "${vcpkg_dir}/lib/libeay32.lib")
    ENDIF(USE_OPENSSL)
    
    ImportVcpkgLibrary(zlib            "${vcpkg_dir}/include" "${vcpkg_dir}/debug/lib/zlibd.lib" "${vcpkg_dir}/lib/zlib.lib") 

    ImportStaticLibrary(gtest           "${Mega3rdPartyDir}/googletest/googletest/include"
                                        "${Mega3rdPartyDir}/googletest/googletest/msvc/2010/gtest/Win32-Debug/gtestd.lib" 
                                        "${Mega3rdPartyDir}/googletest/googletest/msvc/2010/gtest/Win32-Release/gtest.lib"
                                        "${Mega3rdPartyDir}/googletest/googletest/msvc/2010/gtest/x64-Debug/gtestd.lib" 
                                        "${Mega3rdPartyDir}/googletest/googletest/msvc/2010/gtest/x64-Release/gtest.lib")

    IF(USE_MEDIAINFO)
        ImportStaticLibrary(mediainfo   "${Mega3rdPartyDir}/MediaInfoLib-mw/Source"
                                        "${Mega3rdPartyDir}/MediaInfoLib-mw/Project/MSVC2017/Win32/Debug/MediaInfo-Static.lib" 
                                        "${Mega3rdPartyDir}/MediaInfoLib-mw/Project/MSVC2017/Win32/Release/MediaInfo-Static.lib"
                                        "${Mega3rdPartyDir}/MediaInfoLib-mw/Project/MSVC2017/x64/Debug/MediaInfo-Static.lib"
                                        "${Mega3rdPartyDir}/MediaInfoLib-mw/Project/MSVC2017/x64/Release/MediaInfo-Static.lib")

        ImportStaticLibrary(zen         "${Mega3rdPartyDir}/ZenLib/Source"
                                        "${Mega3rdPartyDir}/MediaInfoLib-mw/Project/MSVC2017/Win32/Debug/ZenLib.lib"
                                        "${Mega3rdPartyDir}/MediaInfoLib-mw/Project/MSVC2017/Win32/Release/ZenLib.lib"
                                        "${Mega3rdPartyDir}/MediaInfoLib-mw/Project/MSVC2017/x64/Debug/ZenLib.lib"
                                        "${Mega3rdPartyDir}/MediaInfoLib-mw/Project/MSVC2017/x64/Release/ZenLib.lib")
    ENDIF(USE_MEDIAINFO)

    IF(USE_FREEIMAGE)
        ImportStaticLibrary(freeimage   "${Mega3rdPartyDir}/FreeImage3170/FreeImage/Source"
                                        "${Mega3rdPartyDir}/FreeImage3170/FreeImage/Source/FreeImageLib/Win32/Debug/FreeImageLib.lib"
                                        "${Mega3rdPartyDir}/FreeImage3170/FreeImage/Source/FreeImageLib/Win32/Release/FreeImageLib.lib"
                                        "${Mega3rdPartyDir}/FreeImage3170/FreeImage/Source/FreeImageLib/x64/Debug/FreeImageLib.lib"
                                        "${Mega3rdPartyDir}/FreeImage3170/FreeImage/Source/FreeImageLib/x64/Release/FreeImageLib.lib")

    ENDIF(USE_FREEIMAGE)


    IF(USE_SQLITE)
        ImportVcpkgLibrary(sqlite3          "${vcpkg_dir}/include" "${vcpkg_dir}/debug/lib/sqlite3.lib" "${vcpkg_dir}/lib/sqlite3.lib")

 #       add_library(sqlite3 INTERFACE IMPORTED)
 #       set_property(TARGET sqlite3 PROPERTY INTERFACE_INCLUDE_DIRECTORIES "${Mega3rdPartyDir}/sqlite-3.20.1")
    ENDIF(USE_SQLITE)

    add_definitions(-D_CRT_SECURE_NO_WARNINGS -DCURL_STATICLIB -DCARES_STATICLIB -DWIN32_LEAN_AND_MEAN -DUNICODE -DSODIUM_STATIC -DPCRE_STATICWIN32 -D_CONSOLE)
    SET(Mega_PlatformSpecificIncludes ${MegaDir}/include/mega/$<IF:${USE_CURL},wincurl,win32>)
    SET(Mega_PlatformSpecificLibs ws2_32 winhttp Shlwapi)

    #get_target_property(sqlite3dir sqlite3 INTERFACE_INCLUDE_DIRECTORIES)
    SET(Mega_PlatformSpecificFiles ${MegaDir}/src/win32/console.cpp 
    ${MegaDir}/src/win32/autocomplete.cpp 
    ${MegaDir}/src/win32/consolewaiter.cpp 
    ${MegaDir}/src/win32/fs.cpp 
    $<IF:${USE_CURL},${MegaDir}/src/posix/net.cpp,${MegaDir}/src/win32/net.cpp>
    ${MegaDir}/src/win32/waiter.cpp 
    ${MegaDir}/src/thread/win32thread.cpp  
    #${sqlite3dir}/sqlite3.c 
    )


ELSE(WIN32)

    add_definitions(-DUSE_PTHREAD )
    SET(Mega_PlatformSpecificFiles ${MegaDir}/src/posix/console.cpp ${MegaDir}/src/posix/consolewaiter.cpp ${MegaDir}/src/posix/fs.cpp ${MegaDir}/src/posix/net.cpp ${MegaDir}/src/posix/waiter.cpp ${MegaDir}/src/thread/posixthread.cpp )
    SET(Mega_PlatformSpecificIncludes ${MegaDir}/include/mega/posix)
    SET(Mega_PlatformSpecificLibs crypto pthread rt z)

ENDIF(WIN32)


SET(Mega_CryptoFiles ${MegaDir}/src/crypto/cryptopp.cpp ${MegaDir}/src/crypto/sodium.cpp)
SET(Mega_DbFiles ${MegaDir}/src/db/sqlite.cpp ${MegaDir}/src/db/sqlite.cpp )
SET(Mega_GfxFiles ${MegaDir}/src/gfx/external.cpp ${MegaDir}/src/gfx/freeimage.cpp ) 

add_library(Mega STATIC
            ${MegaDir}/src/attrmap.cpp 
            ${MegaDir}/src/backofftimer.cpp 
            ${MegaDir}/src/base64.cpp 
            ${MegaDir}/src/command.cpp 
            ${MegaDir}/src/commands.cpp 
            ${MegaDir}/src/db.cpp 
            ${MegaDir}/src/file.cpp 
            ${MegaDir}/src/fileattributefetch.cpp 
            ${MegaDir}/src/filefingerprint.cpp 
            ${MegaDir}/src/filesystem.cpp 
            ${MegaDir}/src/gfx.cpp 
            ${MegaDir}/src/http.cpp 
            ${MegaDir}/src/json.cpp 
            ${MegaDir}/src/logging.cpp 
            ${MegaDir}/src/mediafileattribute.cpp 
            ${MegaDir}/src/mega_ccronexpr.cpp 
            ${MegaDir}/src/mega_http_parser.cpp 
            ${MegaDir}/src/mega_utf8proc.cpp 
            ${MegaDir}/src/mega_zxcvbn.cpp 
            ${MegaDir}/src/megaapi.cpp 
            ${MegaDir}/src/megaapi_impl.cpp 
            ${MegaDir}/src/megaclient.cpp 
            ${MegaDir}/src/node.cpp 
            ${MegaDir}/src/pendingcontactrequest.cpp 
            ${MegaDir}/src/proxy.cpp 
            ${MegaDir}/src/pubkeyaction.cpp 
            ${MegaDir}/src/request.cpp
            ${MegaDir}/src/serialize64.cpp 
            ${MegaDir}/src/share.cpp 
            ${MegaDir}/src/sharenodekeys.cpp 
            ${MegaDir}/src/sync.cpp 
            ${MegaDir}/src/transfer.cpp 
            ${MegaDir}/src/transferslot.cpp 
            ${MegaDir}/src/treeproc.cpp 
            ${MegaDir}/src/user.cpp 
            ${MegaDir}/src/utils.cpp 
            ${MegaDir}/src/waiterbase.cpp 
            ${Mega_PlatformSpecificFiles} ${Mega_CryptoFiles} ${Mega_DbFiles} ${Mega_GfxFiles}  )

target_include_directories(Mega PRIVATE ${MegaDir}/include ${Mega_PlatformSpecificIncludes})
target_include_directories(Mega PUBLIC ${MegaDir}/include ${Mega_PlatformSpecificIncludes})
target_link_libraries(Mega PUBLIC zlib 
                $<${USE_CRYPTOPP}:cryptopp> 
                $<${USE_SODIUM}:sodium> 
                $<${USE_OPENSSL}:ssl> $<${USE_OPENSSL}:sslcrypto> 
                $<${USE_CURL}:curl>  $<${USE_CURL}:cares> 
                $<${USE_SQLITE}:sqlite3> 
                $<${USE_MEDIAINFO}:mediainfo> $<${USE_MEDIAINFO}:zen> 
                $<${USE_FREEIMAGE}:freeimage> 
                ${Mega_PlatformSpecificLibs})
target_compile_definitions(Mega PUBLIC 
                $<${USE_MEDIAINFO}:USE_MEDIAINFO> 
                $<${USE_FREEIMAGE}:USE_FREEIMAGE> $<${USE_FREEIMAGE}:FREEIMAGE_LIB>  
                $<${USE_SQLITE}:USE_SQLITE> 
                $<${USE_CRYPTOPP}:USE_CRYPTOPP> 
                $<${USE_OPENSSL}:USE_OPENSSL> 
                $<${USE_CURL}:USE_CURL> 
                $<${USE_SODIUM}:USE_SODIUM>
                $<${ENABLE_SYNC}:ENABLE_SYNC> 
                $<${ENABLE_CHAT}:ENABLE_CHAT> 
                $<${NO_READLINE}:NO_READLINE> )

if(WIN32)
target_link_libraries(Mega PUBLIC crypt32.lib)
endif(WIN32)

#test apps
add_executable(test_sdk             ${MegaDir}/tests/sdk_test.cpp 
                                    ${MegaDir}/tests/sdktests.cpp)
add_executable(test_misc            ${MegaDir}/tests/tests.cpp 
                                    ${MegaDir}/tests/paycrypt_test.cpp 
                                    ${MegaDir}/tests/crypto_test.cpp)
add_executable(test_purge_account   ${MegaDir}/tests/purge_account.cpp)
add_executable(test_sync            ${MegaDir}/tests/synctests.cpp)

target_compile_definitions(test_sdk PRIVATE _SILENCE_TR1_NAMESPACE_DEPRECATION_WARNING)
target_compile_definitions(test_misc PRIVATE _SILENCE_TR1_NAMESPACE_DEPRECATION_WARNING)
target_compile_definitions(test_purge_account PRIVATE _SILENCE_TR1_NAMESPACE_DEPRECATION_WARNING)
target_compile_definitions(test_sync PRIVATE _SILENCE_TR1_NAMESPACE_DEPRECATION_WARNING)
target_link_libraries(test_sdk gtest Mega )
target_link_libraries(test_misc gtest Mega )
target_link_libraries(test_purge_account gtest Mega )
target_link_libraries(test_sync gtest Mega )

#test apps need this file or tests fail
configure_file("${MegaDir}/logo.png" logo.png COPYONLY)

# actual apps

add_executable(megacli ${MegaDir}/examples/megacli.cpp)
target_link_libraries(megacli Mega)
if (NOT NO_READLINE)
    target_link_libraries(megacli readline)
endif (NOT NO_READLINE)

add_executable(megasimplesync ${MegaDir}/examples/megasimplesync.cpp)
target_link_libraries(megasimplesync Mega )

if(WIN32)
add_executable(testmega "${MegaDir}/examples/win32/testmega/main.cpp")
target_link_libraries(testmega Mega )
endif(WIN32)

#enable_testing()
#add_test(NAME SdkTestStreaming COMMAND test_sdk "--gtest_filter=\"*Streaming*\"")
#add_test(NAME SdkTestAll COMMAND test_sdk )

