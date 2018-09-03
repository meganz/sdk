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

cmake_minimum_required(VERSION 3.7.2)

#set this to 1 if building 64 bit with eg. cmake . -G "Visual Studio 15 2017 Win64"
#set this to 0 if building 32 bit with eg. cmake . -G "Visual Studio 15 2017"
set (build_64_bit 1 CACHE TYPE BOOL)

#indicate which dependent libraries to use in the build
set (USE_CRYPTOPP 1 CACHE TYPE BOOL)
set (USE_OPENSSL 1 CACHE TYPE BOOL)
set (OPENSSL_IS_BORINGSSL 0 CACHE TYPE BOOL)
set (USE_CURL 1 CACHE TYPE BOOL)
set (USE_SQLITE 1 CACHE TYPE BOOL)
set (USE_MEDIAINFO 1 CACHE TYPE BOOL)
set (USE_FREEIMAGE 1 CACHE TYPE BOOL)
set (USE_SODIUM 0 CACHE TYPE BOOL)
set (ENABLE_SYNC 1 CACHE TYPE BOOL)
set (ENABLE_CHAT 1 CACHE TYPE BOOL)
set (HAVE_FFMPEG 1 CACHE TYPE BOOL)

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
        set(Mega3rdPartyDir "${MegaDir}/../3pNew/3rdparty")
    endif()
ELSE(WIN32)
    set(MegaDir "${CMAKE_CURRENT_LIST_DIR}/../..")
    set(Mega3rdPartyDir "${MegaDir}/../3rdParty/vcpkg/installed/x64-linux")
    include_directories( "${MegaDir}/include" "${Mega3rdPartyDir}/include" )
    link_directories( "${Mega3rdPartyDir}/lib" )
ENDIF(WIN32)

IF(WIN32)
    if(build_64_bit)
        set(vcpkg_dir "${Mega3rdPartyDir}/vcpkg/installed/x64-windows-static-uncheckediterators")
    else(build_64_bit)
        set(vcpkg_dir "${Mega3rdPartyDir}/vcpkg/installed/x86-windows-static-uncheckediterators")
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

# node deletion in debug under VC++ is pretty slow without this.  However libraries we depend on need to be built with the same setting or linking fails 
# (hence the build3rdParty script using the xNN-windows-static-uncheckediterators triplets)
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -D_ITERATOR_DEBUG_LEVEL=0" )


# accurate __cplusplus macro for vc++, selecting c++17 here for windows builds though the MEGA SDK library must build for older c++ standards also
if(WIN32)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /Zc:__cplusplus /std:c++17")
add_definitions( -DNOMINMAX -DUSE_CPPTHREAD )  # also don't allow min and max macros from windows.h, use the std functions like on linux/g++
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

function(ImportVcpkgLibrary libName includeDir)
    # supports alternating debug/release lib names for libraries that have more than one .lib
    # (however it seems the IMPORTED_LLOCAION only supports a single one, so it only supports one debug and one release (for now))
    add_library(${libName} STATIC IMPORTED)
    set_property(TARGET ${libName} PROPERTY INTERFACE_INCLUDE_DIRECTORIES ${includeDir})
    set (d 1)
    foreach(libfile ${ARGN})
        if (d EQUAL 1)
            set_property(TARGET ${libName} APPEND PROPERTY IMPORTED_LOCATION_DEBUG ${libfile})
            set(d 2)
        else()
            set_property(TARGET ${libName} APPEND PROPERTY IMPORTED_LOCATION_RELEASE  ${libfile})
            set(d 1)
        endif()
    endforeach(libfile)
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
        ImportVcpkgLibrary(sodium          "${vcpkg_dir}/include" "${vcpkg_dir}/debug/lib/libsodium.lib"  "${vcpkg_dir}/lib/libsodium.lib")
    ENDIF(USE_SODIUM)

    IF(USE_CURL)
        ImportVcpkgLibrary(curl        "${vcpkg_dir}/include" "${vcpkg_dir}/debug/lib/libcurl.lib"  "${vcpkg_dir}/lib/libcurl.lib")
        ImportVcpkgLibrary(cares       "${vcpkg_dir}/include" "${vcpkg_dir}/debug/lib/cares.lib" "${vcpkg_dir}/lib/cares.lib")
    ENDIF(USE_CURL)

    IF(USE_OPENSSL)
        ImportVcpkgLibrary(ssl         "${vcpkg_dir}/include" "${vcpkg_dir}/debug/lib/ssleay32.lib" "${vcpkg_dir}/lib/ssleay32.lib")
        ImportVcpkgLibrary(crypto      "${vcpkg_dir}/include" "${vcpkg_dir}/debug/lib/libeay32.lib" "${vcpkg_dir}/lib/libeay32.lib")
    ENDIF(USE_OPENSSL)
    
    ImportVcpkgLibrary(z               "${vcpkg_dir}/include" "${vcpkg_dir}/debug/lib/zlibd.lib" "${vcpkg_dir}/lib/zlib.lib") 

    ImportVcpkgLibrary(gtest           "${vcpkg_dir}/include" "${vcpkg_dir}/debug/lib/manual-link/gtestd.lib" "${vcpkg_dir}/lib/manual-link/gtest.lib") 

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
        ImportVcpkgLibrary(freeimage    "${vcpkg_dir}/include" "${vcpkg_dir}/debug/lib/FreeImaged.lib"  "${vcpkg_dir}/lib/FreeImaged.lib")
        ImportVcpkgLibrary(freeimage_Iex "${vcpkg_dir}/include" "${vcpkg_dir}/debug/lib/Iex-2_2.lib"  "${vcpkg_dir}/lib/Iex-2_2.lib")
        ImportVcpkgLibrary(freeimage_IexMath "${vcpkg_dir}/include" "${vcpkg_dir}/debug/lib/IexMath-2_2.lib"  "${vcpkg_dir}/lib/IexMath-2_2.lib")
        ImportVcpkgLibrary(freeimage_IlmImf "${vcpkg_dir}/include" "${vcpkg_dir}/debug/lib/IlmImf-2_2.lib"  "${vcpkg_dir}/lib/IlmImf-2_2.lib")
        ImportVcpkgLibrary(freeimage_IlmImfUtil "${vcpkg_dir}/include" "${vcpkg_dir}/debug/lib/IlmImfUtil-2_2.lib"  "${vcpkg_dir}/lib/IlmImfUtil-2_2.lib")
        ImportVcpkgLibrary(freeimage_IlmThread "${vcpkg_dir}/include" "${vcpkg_dir}/debug/lib/IlmThread-2_2.lib"  "${vcpkg_dir}/lib/IlmThread-2_2.lib")
        ImportVcpkgLibrary(freeimage_jpeg "${vcpkg_dir}/include" "${vcpkg_dir}/debug/lib/jpeg.lib"  "${vcpkg_dir}/lib/jpeg.lib")
        ImportVcpkgLibrary(freeimage_turbojpeg "${vcpkg_dir}/include" "${vcpkg_dir}/debug/lib/turbojpeg.lib"  "${vcpkg_dir}/lib/turbojpeg.lib")
        ImportVcpkgLibrary(freeimage_jpegxr "${vcpkg_dir}/include" "${vcpkg_dir}/debug/lib/jpegxrd.lib"  "${vcpkg_dir}/lib/jpegxr.lib")
        ImportVcpkgLibrary(freeimage_jxrglue "${vcpkg_dir}/include" "${vcpkg_dir}/debug/lib/jxrglued.lib"  "${vcpkg_dir}/lib/jxrglue.lib")
        ImportVcpkgLibrary(freeimage_openjp2 "${vcpkg_dir}/include" "${vcpkg_dir}/debug/lib/openjp2.lib"  "${vcpkg_dir}/lib/openjp2.lib")
        ImportVcpkgLibrary(freeimage_half "${vcpkg_dir}/include" "${vcpkg_dir}/debug/lib/half.lib"  "${vcpkg_dir}/lib/half.lib")
        ImportVcpkgLibrary(freeimage_jasper "${vcpkg_dir}/include" "${vcpkg_dir}/debug/lib/jasper.lib"  "${vcpkg_dir}/lib/jasper.lib")
        ImportVcpkgLibrary(freeimage_libpng "${vcpkg_dir}/include" "${vcpkg_dir}/debug/lib/libpng16d.lib"  "${vcpkg_dir}/lib/libpng16.lib")
        ImportVcpkgLibrary(freeimage_lzma    "${vcpkg_dir}/include" "${vcpkg_dir}/debug/lib/lzma.lib"  "${vcpkg_dir}/lib/lzma.lib")
        ImportVcpkgLibrary(freeimage_lcms2    "${vcpkg_dir}/include" "${vcpkg_dir}/debug/lib/lcms2d.lib"  "${vcpkg_dir}/lib/lcms.lib")
        ImportVcpkgLibrary(freeimage_raw    "${vcpkg_dir}/include" "${vcpkg_dir}/debug/lib/rawd.lib"  "${vcpkg_dir}/lib/raw.lib")
        ImportVcpkgLibrary(freeimage_swscale  "${vcpkg_dir}/include" "${vcpkg_dir}/debug/lib/swscale.lib"  "${vcpkg_dir}/lib/swscale.lib")
        ImportVcpkgLibrary(freeimage_swresample "${vcpkg_dir}/include" "${vcpkg_dir}/debug/lib/swresample.lib"  "${vcpkg_dir}/lib/swresample.lib")
        ImportVcpkgLibrary(freeimage_tiff    "${vcpkg_dir}/include" "${vcpkg_dir}/debug/lib/tiffd.lib"  "${vcpkg_dir}/lib/tiff.lib")
        ImportVcpkgLibrary(freeimage_tiffxx    "${vcpkg_dir}/include" "${vcpkg_dir}/debug/lib/tiffxxd.lib"  "${vcpkg_dir}/lib/tiffxx.lib")
        ImportVcpkgLibrary(freeimage_webp    "${vcpkg_dir}/include" "${vcpkg_dir}/debug/lib/webpd.lib"  "${vcpkg_dir}/lib/webp.lib")
        ImportVcpkgLibrary(freeimage_webpdecoder    "${vcpkg_dir}/include" "${vcpkg_dir}/debug/lib/webpdecoderd.lib"  "${vcpkg_dir}/lib/webpdecoder.lib")
        ImportVcpkgLibrary(freeimage_webpdemux    "${vcpkg_dir}/include" "${vcpkg_dir}/debug/lib/webpdemuxd.lib"  "${vcpkg_dir}/lib/webpdemux.lib")
        ImportVcpkgLibrary(freeimage_webpmux    "${vcpkg_dir}/include" "${vcpkg_dir}/debug/lib/webpmuxd.lib"  "${vcpkg_dir}/lib/webpmux.lib")
    ENDIF(USE_FREEIMAGE)

    IF(HAVE_FFMPEG)
        ImportVcpkgLibrary(ffmpeg_avformat "${vcpkg_dir}/include" "${vcpkg_dir}/debug/lib/avformat.lib" "${vcpkg_dir}/lib/avformat.lib")
        ImportVcpkgLibrary(ffmpeg_avutil "${vcpkg_dir}/include" "${vcpkg_dir}/debug/lib/avutil.lib" "${vcpkg_dir}/lib/avutil.lib")
        ImportVcpkgLibrary(ffmpeg_avcodec "${vcpkg_dir}/include" "${vcpkg_dir}/debug/lib/avcodec.lib" "${vcpkg_dir}/lib/avcodec.lib")
        ImportVcpkgLibrary(ffmpeg_avfilter "${vcpkg_dir}/include" "${vcpkg_dir}/debug/lib/avfilter.lib" "${vcpkg_dir}/lib/avfilter.lib")
        ImportVcpkgLibrary(ffmpeg_avdevice "${vcpkg_dir}/include" "${vcpkg_dir}/debug/lib/avdevice.lib" "${vcpkg_dir}/lib/avdevice.lib")
    ENDIF(HAVE_FFMPEG)

    IF(USE_SQLITE)
        ImportVcpkgLibrary(sqlite3          "${vcpkg_dir}/include" "${vcpkg_dir}/debug/lib/sqlite3.lib" "${vcpkg_dir}/lib/sqlite3.lib")
    ENDIF(USE_SQLITE)

    add_definitions(-D_CRT_SECURE_NO_WARNINGS -DCURL_STATICLIB -DCARES_STATICLIB -DWIN32_LEAN_AND_MEAN -DUNICODE -DSODIUM_STATIC -DPCRE_STATICWIN32 -D_CONSOLE)
    SET(Mega_PlatformSpecificIncludes ${MegaDir}/include/mega/$<IF:${USE_CURL},wincurl,win32>)
    SET(Mega_PlatformSpecificLibs ws2_32 winhttp Shlwapi Secur32.lib)

    SET(Mega_PlatformSpecificFiles ${MegaDir}/src/win32/console.cpp 
    ${MegaDir}/src/win32/autocomplete.cpp 
    ${MegaDir}/src/win32/consolewaiter.cpp 
    ${MegaDir}/src/win32/fs.cpp 
    $<IF:${USE_CURL},${MegaDir}/src/posix/net.cpp,${MegaDir}/src/win32/net.cpp>
    ${MegaDir}/src/win32/waiter.cpp 
    ${MegaDir}/src/thread/cppthread.cpp   
    )


ELSE(WIN32)

    add_definitions(-DUSE_PTHREAD )
    SET(Mega_PlatformSpecificFiles ${MegaDir}/src/posix/console.cpp ${MegaDir}/src/posix/consolewaiter.cpp ${MegaDir}/src/posix/fs.cpp ${MegaDir}/src/posix/net.cpp ${MegaDir}/src/posix/waiter.cpp ${MegaDir}/src/thread/posixthread.cpp )
    SET(Mega_PlatformSpecificIncludes ${MegaDir}/include/mega/posix)
    SET(Mega_PlatformSpecificLibs crypto pthread rt z dl termcap stdc++fs)

ENDIF(WIN32)

include(CheckIncludeFile)
include(CheckFunctionExists)
check_include_file(inttypes.h HAVE_INTTYPES_H)
check_include_file(dirent.h HAVE_DIRENT_H)
check_include_file(uv.h HAVE_LIBUV)
check_function_exists(aio_write, HAVE_AIO_RT)
check_function_exists(fdopendir, HAVE_FDOPENDIR)

configure_file ("${MegaDir}/include/mega/config.h.in.cmake" "${MegaDir}/include/mega/config.h" )



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
target_link_libraries(Mega PUBLIC z 
                $<${USE_CRYPTOPP}:cryptopp> 
                $<${USE_SODIUM}:sodium> 
                $<${USE_OPENSSL}:ssl> $<${USE_OPENSSL}:crypto> 
                $<${USE_CURL}:curl>  $<${USE_CURL}:cares> 
                $<${USE_SQLITE}:sqlite3> 
                $<${USE_MEDIAINFO}:mediainfo> $<${USE_MEDIAINFO}:zen> 
                $<${USE_FREEIMAGE}:freeimage> $<${USE_FREEIMAGE}:freeimage_IlmImf> $<${USE_FREEIMAGE}:freeimage_IlmImfUtil> $<${USE_FREEIMAGE}:freeimage_IlmThread> $<${USE_FREEIMAGE}:freeimage_Iex> $<${USE_FREEIMAGE}:freeimage_IexMath>
                                              $<${USE_FREEIMAGE}:freeimage_jpeg> $<${USE_FREEIMAGE}:freeimage_turbojpeg> $<${USE_FREEIMAGE}:freeimage_jpegxr> $<${USE_FREEIMAGE}:freeimage_jxrglue> $<${USE_FREEIMAGE}:freeimage_openjp2>
                                              $<${USE_FREEIMAGE}:freeimage_lzma>  $<${USE_FREEIMAGE}:freeimage_lcms2>  $<${USE_FREEIMAGE}:freeimage_raw>  
                                              $<${USE_FREEIMAGE}:freeimage_tiff>  $<${USE_FREEIMAGE}:freeimage_tiffxx>  
                                              $<${USE_FREEIMAGE}:freeimage_jasper>  $<${USE_FREEIMAGE}:freeimage_libpng>  $<${USE_FREEIMAGE}:freeimage_half>  
                                              $<${USE_FREEIMAGE}:freeimage_webp> $<${USE_FREEIMAGE}:freeimage_webpdecoder>  $<${USE_FREEIMAGE}:freeimage_webpdemux>  $<${USE_FREEIMAGE}:freeimage_webpmux> 
                                              $<${USE_FREEIMAGE}:freeimage_swscale>  $<${USE_FREEIMAGE}:freeimage_swresample>  
                $<${HAVE_FFMPEG}:ffmpeg_avformat> $<${HAVE_FFMPEG}:ffmpeg_avcodec> $<${HAVE_FFMPEG}:ffmpeg_avutil> $<${HAVE_FFMPEG}:ffmpeg_avfilter> $<${HAVE_FFMPEG}:ffmpeg_avdevice> $<${HAVE_FFMPEG}:ffmpeg_avdevice> 
                ${Mega_PlatformSpecificLibs})
target_compile_definitions(Mega PUBLIC 
                $<${USE_MEDIAINFO}:USE_MEDIAINFO> 
                $<${USE_SQLITE}:USE_SQLITE> 
                $<${USE_CRYPTOPP}:USE_CRYPTOPP> 
                $<${USE_OPENSSL}:USE_OPENSSL> 
                $<${USE_CURL}:USE_CURL> 
                $<${USE_SODIUM}:USE_SODIUM>
                $<${ENABLE_SYNC}:ENABLE_SYNC> 
                $<${ENABLE_CHAT}:ENABLE_CHAT> 
                $<${NO_READLINE}:NO_READLINE>
                $<${USE_FREEIMAGE}:USE_FREEIMAGE> 
                $<${HAVE_FFMPEG}:HAVE_FFMPEG> )

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

