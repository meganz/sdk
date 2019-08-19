# This file enables building with cmake
#
# use cases
#  - generate a visual studio solution and projects, eg: cmake . -G "Visual Studio 15 2017"
#  - or for 64 bit: cmake . -G "Visual Studio 15 2017 Win64"
#  - or set your build options before VS project generation by using the gui, eg:  cmake-gui.exe .
#  - you can set up to build both 32 bit and 64 bit this way:
#       make subfolders '32' and '64' of this cmake folder
#       execute 'cmake-gui  ..' in each.  Select the cmake folder as the 'source code' folder, and the appropriate 32 or 64 folder as 'where to build the binaries' folder
#       for each cmake-gui configuration, choose the suitable 32/64 bit compiler, and corresponding setting for the build_64_bit variable.
#       then Configure, Generate, Open Project (all buttons in the gui) for each.

# check the ./build3rdParty.cmd script in this folder for how to (relatively) easily build the dependencies, typically in a 3rdParty folder outside of the SDK repo.

cmake_minimum_required(VERSION 3.7.2)
project(MegaSDK)

#indicate which dependent libraries to use in the build
set (USE_CRYPTOPP 1 CACHE STRING "")
set (USE_OPENSSL 1 CACHE STRING "")
set (USE_CURL 1 CACHE STRING "")
set (USE_SQLITE 1 CACHE STRING "")
set (USE_MEDIAINFO 1 CACHE STRING "")
set (USE_FREEIMAGE 1 CACHE STRING "")
set (USE_SODIUM 0 CACHE STRING "")
set (ENABLE_SYNC 1 CACHE STRING "")
set (ENABLE_CHAT 0 CACHE STRING "")
set (HAVE_FFMPEG 1 CACHE STRING "")
set (USE_WEBRTC 0 CACHE STRING "")
set (USE_LIBUV 0 CACHE STRING "")
set (USE_QT 0 CACHE STRING "")
set (USE_PDFIUM 0 CACHE STRING "")

if (USE_QT)
    set( USE_CPPTHREAD 0)
else()
    set( USE_CPPTHREAD 1)
ENDIF()

if (WIN32)
    set (NO_READLINE 1 CACHE STRING "")
    set (UNCHECKED_ITERATORS 0 CACHE STRING "")
    set (USE_PREBUILT_3RDPARTY 0 CACHE STRING "")
    set (HAVE_LIBUV ${USE_LIBUV})
    IF (USE_WEBRTC)
        IF ("${WebRtcDir}" STREQUAL "")
            SET (WebRtcDir "${Mega3rdPartyDir}/libwebrtc/build32debug")
        ENDIF()
    ENDIF()
else(WIN32)
    set(NO_READLINE 0)
    set(USE_PREBUILT_3RDPARTY 0)
    set(HAVE_LIBUV 0)
endif(WIN32)

if (USE_WEBRTC)
    if ("${WebRtcDir}" STREQUAL "")
        SET(WebRtcDir ${Mega3rdPartyDir}/libwebrtc/build32debug)
    endif()
endif()

if (ENABLE_CHAT AND NOT USE_SODIUM)
    message(FATAL_ERROR "ENABLE_CHAT (${ENABLE_CHAT}) requires USE_SODIUM (${USE_SODIUM})")
endif()

if (WIN32)
    if("${CMAKE_GENERATOR}" MATCHES "(Win64|IA64)")
        SET(build_64_bit 1)
    elseif(CMAKE_SIZEOF_VOID_P EQUAL 4)
        SET(build_64_bit 0)
    endif()
else()
    set (build_64_bit 1 CACHE TYPE BOOL)
endif()

if ("${MEGA_PROJECT_NAME}" STREQUAL "")
    set(MEGA_PROJECT_NAME "MegaSDK" CACHE STRING "")
endif()

if(build_64_bit)
    project ("${MEGA_PROJECT_NAME}64" LANGUAGES CXX C)
else(build_64_bit)
    project ("${MEGA_PROJECT_NAME}32" LANGUAGES CXX C)
endif(build_64_bit)

IF(WIN32)
    set(MegaDir "${CMAKE_CURRENT_LIST_DIR}/../..")
    if ("${Mega3rdPartyDir}" STREQUAL "")
        set(Mega3rdPartyDir "${MegaDir}/../3rdParty" CACHE STRING "")
    endif()
ELSE(WIN32)
    set(MegaDir "${CMAKE_CURRENT_LIST_DIR}/../..")
    if ("${Mega3rdPartyDir}" STREQUAL "")
        set (Mega3rdPartyDir "${MegaDir}/../3rdParty/")
    endif()
ENDIF(WIN32)

# currently supported scenarios for getting 3rd party libraries:
#  - prebuilt libraries, in a folder from mega    (set USE_PREBUILT_3RDPARTY and prebulit_dir (if the default does not suit))
#  - vcpkg built dependencies (there are some scripts that do most of that)  (do not set USE_PREBUILT_3RDPARTY, and set vcpkg_dir if the default does not suit, also set )
#  - libraries from the system, installed via eg. apt-get on linux

if (USE_PREBUILT_3RDPARTY)
    if ("${prebuilt_dir}" STREQUAL "")
        SET (prebuilt_dir ${Mega3rdPartyDir} CACHE STRING "Location of prebuilt 3rd party folder from MEGA.nz")
    ENDIF()
else (USE_PREBUILT_3RDPARTY)
    IF(WIN32)
        # assuming VCPKG at this point if we are on windows
        if (UNCHECKED_ITERATORS)
            if(build_64_bit)
                set(VCPKG_TRIPLET "x64-windows-static-uncheckediterators" CACHE STRING "")
            else(build_64_bit)
                set(VCPKG_TRIPLET "x86-windows-static-uncheckediterators" CACHE STRING "")
            endif(build_64_bit)
        else() 
            if(build_64_bit)
                set(VCPKG_TRIPLET "x64-windows-static" CACHE STRING "")
            else(build_64_bit)
                set(VCPKG_TRIPLET "x86-windows-static" CACHE STRING "")
            endif(build_64_bit)
        endif()
        set(vcpkg_dir "${Mega3rdPartyDir}/vcpkg/installed/${VCPKG_TRIPLET}")
    ELSE(WIN32)
        # case for linux.  VCPKG or system/usr libraries
        set (USE_THIRDPARTY_FROM_VCPKG 0 CACHE STRING "")
        if (USE_THIRDPARTY_FROM_VCPKG)
            set(VCPKG_TRIPLET "x64-linux" CACHE STRING "")
            set(vcpkg_dir "${Mega3rdPartyDir}/vcpkg/installed/${VCPKG_TRIPLET}")
            include_directories( "${vcpkg_dir}/include" )
            link_directories( "${vcpkg_dir}/lib" )
        else()
            include_directories( "${Mega3rdPartyDir}/include" )
            link_directories( "${Mega3rdPartyDir}/lib" )
        endif()
    ENDIF(WIN32)
endif()

if (NOT CMAKE_BUILD_TYPE)
    message("Generated with config types: ${CMAKE_CONFIGURATION_TYPES}")
else(NOT CMAKE_BUILD_TYPE)
    message("CMAKE_BUILD_TYPE is ${CMAKE_BUILD_TYPE}")
endif(NOT CMAKE_BUILD_TYPE)

#windows projects usually need _DEBUG and/or DEBUG set rather than NDEBUG not set
set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -D_DEBUG -DDEBUG")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -D_DEBUG -DDEBUG")

if (WIN32)
    # node deletion in debug under VC++ is pretty slow without this.  However libraries we depend on need to be built with the same setting or linking fails 
    # (hence the build3rdParty script using the xNN-windows-static-uncheckediterators triplets)
    if (UNCHECKED_ITERATORS AND NOT USE_PREBUILT_3RDPARTY)
        set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -D_ITERATOR_DEBUG_LEVEL=0" )
    endif()

    # accurate __cplusplus macro for vc++, selecting c++17 here for windows builds though the MEGA SDK library must build for older c++ standards also
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /Zc:__cplusplus /std:c++17")
    add_definitions( -DNOMINMAX )

    IF (NOT USE_PREBUILT_3RDPARTY)
        #Link against the static C/C++ libraries on windows
        foreach(flag_var
                CMAKE_CXX_FLAGS CMAKE_CXX_FLAGS_DEBUG CMAKE_CXX_FLAGS_RELEASE 
                CMAKE_C_FLAGS CMAKE_C_FLAGS_DEBUG CMAKE_C_FLAGS_RELEASE
                CMAKE_CXX_FLAGS_MINSIZEREL CMAKE_CXX_FLAGS_RELWITHDEBINFO)
           if(${flag_var} MATCHES "/MD")
              string(REGEX REPLACE "/MD" "/MT" ${flag_var} "${${flag_var}}")
           endif(${flag_var} MATCHES "/MD")
        endforeach(flag_var)
    ENDIF()

    set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /MP")
    set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /MP")

    # for inet_ntoa (which is available in XP)
    add_definitions( -D_WINSOCK_DEPRECATED_NO_WARNINGS )
ENDIF(WIN32)

include(CheckIncludeFile)
include(CheckFunctionExists)
check_include_file(inttypes.h HAVE_INTTYPES_H)
check_include_file(dirent.h HAVE_DIRENT_H)
check_include_file(uv.h HAVE_LIBUV)
check_function_exists(aio_write, HAVE_AIO_RT)
check_function_exists(fdopendir, HAVE_FDOPENDIR)


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
    if(USE_PREBUILT_3RDPARTY)
    
        ImportVcpkgLibrary(z               "${prebuilt_dir}/include" "${prebuilt_dir}/libs/x32d/zlibstat.lib" "${prebuilt_dir}/libs/x32/zlibstat.lib") 	
        
        IF(USE_CRYPTOPP)
            ImportVcpkgLibrary(cryptopp        "${prebuilt_dir}/include" "${prebuilt_dir}/libs/x32d/cryptopp.lib"  "${prebuilt_dir}/libs/x32/cryptopp.lib")
        ENDIF(USE_CRYPTOPP)

        IF(USE_CURL)
            ImportVcpkgLibrary(curl        "${prebuilt_dir}/include" "${prebuilt_dir}/libs/x32d/libcurl.lib"  "${prebuilt_dir}/libs/x32/libcurl.lib")
            ImportVcpkgLibrary(cares       "${prebuilt_dir}/include/cares" "${prebuilt_dir}/libs/x32d/cares.lib" "${prebuilt_dir}/libs/x32/cares.lib")
        ENDIF(USE_CURL)
        
        IF(USE_SODIUM)
            ImportVcpkgLibrary(sodium      "${prebuilt_dir}/include" "${prebuilt_dir}/libs/x32d/sodium.lib"  "${prebuilt_dir}/libs/x32/sodium.lib")
        ENDIF(USE_SODIUM)
        
        IF(USE_OPENSSL)
            ImportVcpkgLibrary(ssl         "${prebuilt_dir}/include" "${prebuilt_dir}/libs/x32d/ssleay32.lib" "${prebuilt_dir}/libs/x32/ssleay32.lib")
            ImportVcpkgLibrary(crypto      "${prebuilt_dir}/include" "${prebuilt_dir}/libs/x32d/libeay32.lib" "${prebuilt_dir}/libs/x32/libeay32.lib")
        ENDIF(USE_OPENSSL)
        
        IF(USE_MEDIAINFO)
            ImportVcpkgLibrary(mediainfo    "${prebuilt_dir}/include" "${prebuilt_dir}/libs/x32d/MediaInfo.lib" "${prebuilt_dir}/libs/x32/MediaInfo.lib")
            ImportVcpkgLibrary(zen      	"${prebuilt_dir}/include" "${prebuilt_dir}/libs/x32d/ZenLib.lib" "${prebuilt_dir}/libs/x32/ZenLib.lib")
        ENDIF(USE_MEDIAINFO)
        
        IF(HAVE_FFMPEG)
            ImportVcpkgLibrary(ffmpeg_avformat "${prebuilt_dir}/include/ffmpeg" "${prebuilt_dir}/libs/x32d/avformat.lib" "${prebuilt_dir}/libs/x32/avformat.lib")
            ImportVcpkgLibrary(ffmpeg_avutil "${prebuilt_dir}/include/ffmpeg" "${prebuilt_dir}/libs/x32d/avutil.lib" "${prebuilt_dir}/libs/x32/avutil.lib")
            ImportVcpkgLibrary(ffmpeg_avcodec "${prebuilt_dir}/include/ffmpeg" "${prebuilt_dir}/libs/x32d/avcodec.lib" "${prebuilt_dir}/libs/x32/avcodec.lib")
            ImportVcpkgLibrary(ffmpeg_avfilter "${prebuilt_dir}/include/ffmpeg" "${prebuilt_dir}/libs/x32d/avfilter.lib" "${prebuilt_dir}/libs/x32/avfilter.lib")
            ImportVcpkgLibrary(ffmpeg_avdevice "${prebuilt_dir}/include/ffmpeg" "${prebuilt_dir}/libs/x32d/avdevice.lib" "${prebuilt_dir}/libs/x32/avdevice.lib")
            ImportVcpkgLibrary(ffmpeg_swscale  "${prebuilt_dir}/include/ffmpeg" "${prebuilt_dir}/libs/x32d/swscale.lib"  "${prebuilt_dir}/libs/x32/swscale.lib")
            ImportVcpkgLibrary(ffmpeg_swresample "${prebuilt_dir}/include/ffmpeg" "${prebuilt_dir}/libs/x32d/swresample.lib"  "${prebuilt_dir}/libs/x32/swresample.lib")
        ENDIF(HAVE_FFMPEG)
        
        IF(USE_LIBUV)
            ImportVcpkgLibrary(uv       "${prebuilt_dir}/include/libuv" "${prebuilt_dir}/libs/x32d/libuv.lib" "${prebuilt_dir}/libs/x32/libuv.lib")
            SET(HAVE_LIBUV 1)
        ENDIF(USE_LIBUV)

        IF(USE_PDFIUM)
            ImportVcpkgLibrary(pdfium       "${prebuilt_dir}/include" "${prebuilt_dir}/libs/x32d/pdfium.lib" "${prebuilt_dir}/libs/x32/pdfium.lib")
        ENDIF(USE_PDFIUM)
        
        ImportVcpkgLibrary(gtest           "${prebuilt_dir}/include" "${prebuilt_dir}/libs/x32d/gtestd.lib" "${prebuilt_dir}/libs/x32/gtest.lib")

    else(USE_PREBUILT_3RDPARTY)

        IF(USE_CRYPTOPP)
            ImportVcpkgLibrary(cryptopp        "${vcpkg_dir}/include" "${vcpkg_dir}/debug/lib/cryptopp-static.lib"  "${vcpkg_dir}/lib/cryptopp-static.lib")
        ENDIF(USE_CRYPTOPP)

        IF(USE_SODIUM)
            ImportVcpkgLibrary(sodium          "${vcpkg_dir}/include" "${vcpkg_dir}/debug/lib/libsodium.lib"  "${vcpkg_dir}/lib/libsodium.lib")
        ENDIF(USE_SODIUM)

        IF(USE_CURL)
            IF(USE_WEBRTC)
                ImportVcpkgLibrary(curl         "${Mega3rdPartyDir}/curl/include" "${Mega3rdPartyDir}/curl/build32/lib/Debug/libcurl-d.lib" "${Mega3rdPartyDir}/curl/build32/lib/Release/libcurl.lib")
            ELSE()
                ImportVcpkgLibrary(curl        "${vcpkg_dir}/include" "${vcpkg_dir}/debug/lib/libcurl.lib"  "${vcpkg_dir}/lib/libcurl.lib")
            ENDIF()
        ENDIF()
        IF(USE_CURL)
            ImportVcpkgLibrary(cares       "${vcpkg_dir}/include" "${vcpkg_dir}/debug/lib/cares.lib" "${vcpkg_dir}/lib/cares.lib")
        ENDIF()

        IF(USE_OPENSSL)
            IF(USE_WEBRTC)
                include_directories( "${WebRtcDir}/webrtc/src/third_party/boringssl/src/include" )
            ELSE()
                ImportVcpkgLibrary(ssl         "${vcpkg_dir}/include"  "${vcpkg_dir}/debug/lib/ssleay32.lib" "${vcpkg_dir}/lib/ssleay32.lib")
                ImportVcpkgLibrary(crypto      "${vcpkg_dir}/include" "${vcpkg_dir}/debug/lib/libeay32.lib" "${vcpkg_dir}/lib/libeay32.lib")
            ENDIF()
        ENDIF()

        IF(USE_WEBRTC)
            ImportVcpkgLibrary(webrtc "${WebRtcDir}/include" "${WebRtcDir}/lib/webrtc.lib" "${WebRtcDir}/lib/webrtc.lib")
            add_definitions( -DWEBRTC_WIN )
        ENDIF()
    
        ImportVcpkgLibrary(z               "${vcpkg_dir}/include" "${vcpkg_dir}/debug/lib/zlibd.lib" "${vcpkg_dir}/lib/zlib.lib") 

        ImportVcpkgLibrary(gtest           "${vcpkg_dir}/include" "${vcpkg_dir}/debug/lib/gtestd.lib" "${vcpkg_dir}/lib/gtest.lib") 

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
            ImportVcpkgLibrary(freeimage    "${vcpkg_dir}/include" "${vcpkg_dir}/debug/lib/FreeImaged.lib"  "${vcpkg_dir}/lib/FreeImage.lib")
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
            ImportVcpkgLibrary(freeimage_lcms2    "${vcpkg_dir}/include" "${vcpkg_dir}/debug/lib/lcms2d.lib"  "${vcpkg_dir}/lib/lcms2.lib")
            ImportVcpkgLibrary(freeimage_raw    "${vcpkg_dir}/include" "${vcpkg_dir}/debug/lib/rawd.lib"  "${vcpkg_dir}/lib/raw.lib")
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
            ImportVcpkgLibrary(ffmpeg_swscale  "${vcpkg_dir}/include" "${vcpkg_dir}/debug/lib/swscale.lib"  "${vcpkg_dir}/lib/swscale.lib")
            ImportVcpkgLibrary(ffmpeg_swresample "${vcpkg_dir}/include" "${vcpkg_dir}/debug/lib/swresample.lib"  "${vcpkg_dir}/lib/swresample.lib")
        ENDIF(HAVE_FFMPEG)

        IF(USE_SQLITE)
            ImportVcpkgLibrary(sqlite3          "${vcpkg_dir}/include" "${vcpkg_dir}/debug/lib/sqlite3.lib" "${vcpkg_dir}/lib/sqlite3.lib")
        ENDIF(USE_SQLITE)

        IF(USE_LIBUV)
            ImportVcpkgLibrary(uv       "${vcpkg_dir}/include" "${vcpkg_dir}/debug/lib/libuv.lib" "${vcpkg_dir}/lib/libuv.lib")
            SET(HAVE_LIBUV 1)
        ENDIF(USE_LIBUV)

    endif(USE_PREBUILT_3RDPARTY)
    
    add_definitions(-D_CRT_SECURE_NO_WARNINGS -DCURL_STATICLIB -DCARES_STATICLIB -DWIN32_LEAN_AND_MEAN -DUNICODE -DSODIUM_STATIC -DPCRE_STATICWIN32 -D_CONSOLE )
    SET(Mega_PlatformSpecificIncludes ${MegaDir}/include/mega/$<IF:${USE_CURL},wincurl,win32>)
    SET(Mega_PlatformSpecificLibs ws2_32 winhttp Shlwapi Secur32.lib  $<${USE_WEBRTC}:Wldap32.lib> )

    SET(Mega_PlatformSpecificFiles ${MegaDir}/src/win32/console.cpp 
    ${MegaDir}/src/win32/consolewaiter.cpp 
    ${MegaDir}/src/win32/fs.cpp 
    $<IF:${USE_CURL},${MegaDir}/src/posix/net.cpp,${MegaDir}/src/win32/net.cpp>
    ${MegaDir}/src/win32/waiter.cpp 
    $<${USE_CPPTHREAD}:${MegaDir}/src/thread/cppthread.cpp>
    $<${USE_QT}:${MegaDir}/src/thread/qtthread.cpp >
    )


ELSE(WIN32)

    add_definitions(-DUSE_PTHREAD )

    check_include_file(glob.h HAVE_GLOB_H)
    if (HAVE_GLOB_H)
        set(GLOB_H_FOUND 1)
    else()
        set(GLOB_H_FOUND 0)   #some versions on some platforms leave it undefined if not found
    endif()

    SET(Mega_PlatformSpecificFiles $<$<NOT:${GLOB_H_FOUND}>:${MegaDir}/src/mega_glob.c> ${MegaDir}/src/posix/console.cpp ${MegaDir}/src/posix/consolewaiter.cpp ${MegaDir}/src/posix/fs.cpp ${MegaDir}/src/posix/net.cpp ${MegaDir}/src/posix/waiter.cpp ${MegaDir}/src/thread/posixthread.cpp )
    IF(APPLE)
        SET(Mega_PlatformSpecificFiles ${Mega_PlatformSpecificFiles} ${MegaDir}/src/osx/osxutils.mm )
    ENDIF()
    SET(Mega_PlatformSpecificIncludes ${MegaDir}/include/mega/posix)
    SET(Mega_PlatformSpecificLibs crypto pthread z dl termcap)
    IF(APPLE)
        SET(Mega_PlatformSpecificLibs ${Mega_PlatformSpecificLibs} "-framework Cocoa -framework SystemConfiguration")
    ELSE()
        SET(Mega_PlatformSpecificLibs ${Mega_PlatformSpecificLibs} rt stdc++fs)
    ENDIF()
    IF(USE_WEBRTC)
        ImportVcpkgLibrary(webrtc "${WebRtcDir}/include" "${WebRtcDir}/lib/webrtc.lib" "${WebRtcDir}/lib/webrtc.lib")
        add_definitions( -DWEBRTC_POSIX )
    ENDIF()

ENDIF(WIN32)

configure_file ("${MegaDir}/contrib/cmake/config.h.in" "${MegaDir}/include/mega/config.h" )


SET(Mega_CryptoFiles ${MegaDir}/src/crypto/cryptopp.cpp ${MegaDir}/src/crypto/sodium.cpp)
SET(Mega_DbFiles ${MegaDir}/src/db/sqlite.cpp ${MegaDir}/src/db/sqlite.cpp )
SET(Mega_GfxFiles ${MegaDir}/src/gfx/external.cpp ${MegaDir}/src/gfx/freeimage.cpp ) 

add_library(Mega STATIC
            ${MegaDir}/include/megaapi.h
            ${MegaDir}/include/megaapi_impl.h
            ${MegaDir}/include/mega/osx/osxutils.h
            ${MegaDir}/include/mega/transferslot.h
            ${MegaDir}/include/mega/thread/qtthread.h
            ${MegaDir}/include/mega/thread/win32thread.h
            ${MegaDir}/include/mega/thread/cppthread.h
            ${MegaDir}/include/mega/thread/posixthread.h
            ${MegaDir}/include/mega/thread/libuvthread.h
            ${MegaDir}/include/mega/command.h
            ${MegaDir}/include/mega/config.h
            ${MegaDir}/include/mega/thread.h
            ${MegaDir}/include/mega/json.h
            ${MegaDir}/include/mega/base64.h
            ${MegaDir}/include/mega/wp8/megafs.h
            ${MegaDir}/include/mega/wp8/meganet.h
            ${MegaDir}/include/mega/wp8/megaconsolewaiter.h
            ${MegaDir}/include/mega/wp8/megasys.h
            ${MegaDir}/include/mega/wp8/megaconsole.h
            ${MegaDir}/include/mega/wp8/megawaiter.h
            ${MegaDir}/include/mega/mega_utf8proc.h
            ${MegaDir}/include/mega/gfx.h
            ${MegaDir}/include/mega/proxy.h
            ${MegaDir}/include/mega/crypto/sodium.h
            ${MegaDir}/include/mega/crypto/cryptopp.h
            ${MegaDir}/include/mega/http.h
            ${MegaDir}/include/mega/useralerts.h
            ${MegaDir}/include/mega/pendingcontactrequest.h
            ${MegaDir}/include/mega/megaapp.h
            ${MegaDir}/include/mega/wincurl/megafs.h
            ${MegaDir}/include/mega/wincurl/meganet.h
            ${MegaDir}/include/mega/wincurl/megaconsolewaiter.h
            ${MegaDir}/include/mega/wincurl/megaconsole.h
            ${MegaDir}/include/mega/wincurl/megawaiter.h
            ${MegaDir}/include/mega/console.h
            ${MegaDir}/include/mega/user.h
            ${MegaDir}/include/mega/mega_evt_queue.h
            ${MegaDir}/include/mega/mega_evt_tls.h
            ${MegaDir}/include/mega/db.h
            ${MegaDir}/include/mega/megaclient.h
            ${MegaDir}/include/mega/autocomplete.h
            ${MegaDir}/include/mega/serialize64.h
            ${MegaDir}/include/mega/posix/megafs.h
            ${MegaDir}/include/mega/posix/meganet.h
            ${MegaDir}/include/mega/posix/megaconsolewaiter.h
            ${MegaDir}/include/mega/posix/megasys.h
            ${MegaDir}/include/mega/posix/megaconsole.h
            ${MegaDir}/include/mega/posix/megawaiter.h
            ${MegaDir}/include/mega/mega_ccronexpr.h
            ${MegaDir}/include/mega/testhooks.h
            ${MegaDir}/include/mega/share.h
            ${MegaDir}/include/mega/win32/megafs.h
            ${MegaDir}/include/mega/win32/meganet.h
            ${MegaDir}/include/mega/win32/megaconsolewaiter.h
            ${MegaDir}/include/mega/win32/megasys.h
            ${MegaDir}/include/mega/win32/megaconsole.h
            ${MegaDir}/include/mega/win32/megawaiter.h
            ${MegaDir}/include/mega/mega_dict-src.h
            ${MegaDir}/include/mega/gfx/GfxProcCG.h
            ${MegaDir}/include/mega/gfx/qt.h
            ${MegaDir}/include/mega/gfx/freeimage.h
            ${MegaDir}/include/mega/gfx/external.h
            ${MegaDir}/include/mega/pubkeyaction.h
            ${MegaDir}/include/mega/mega_http_parser.h
            ${MegaDir}/include/mega/waiter.h
            ${MegaDir}/include/mega/db/sqlite.h
            ${MegaDir}/include/mega/db/bdb.h
            ${MegaDir}/include/mega/types.h
            ${MegaDir}/include/mega/filefingerprint.h
            ${MegaDir}/include/mega/filesystem.h
            ${MegaDir}/include/mega/backofftimer.h
            ${MegaDir}/include/mega/raid.h
            ${MegaDir}/include/mega/logging.h
            ${MegaDir}/include/mega/file.h
            ${MegaDir}/include/mega/sync.h
            ${MegaDir}/include/mega/utils.h
            ${MegaDir}/include/mega/account.h
            ${MegaDir}/include/mega/transfer.h
            ${MegaDir}/include/mega/config-android.h
            ${MegaDir}/include/mega/treeproc.h
            ${MegaDir}/include/mega/attrmap.h
            ${MegaDir}/include/mega/sharenodekeys.h
            ${MegaDir}/include/mega/request.h
            ${MegaDir}/include/mega/mega_zxcvbn.h
            ${MegaDir}/include/mega/fileattributefetch.h
            ${MegaDir}/include/mega/version.h
            ${MegaDir}/include/mega/node.h
            ${MegaDir}/include/mega/mediafileattribute.h
            ${MegaDir}/include/mega/mega_glob.h
            ${MegaDir}/include/mega.h
            ${MegaDir}/src/attrmap.cpp 
            ${MegaDir}/src/autocomplete.cpp 
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
            ${MegaDir}/src/raid.cpp 
            ${MegaDir}/src/request.cpp
            ${MegaDir}/src/serialize64.cpp 
            ${MegaDir}/src/share.cpp 
            ${MegaDir}/src/sharenodekeys.cpp 
            ${MegaDir}/src/sync.cpp 
            ${MegaDir}/src/testhooks.cpp 
            ${MegaDir}/src/transfer.cpp 
            ${MegaDir}/src/transferslot.cpp 
            ${MegaDir}/src/treeproc.cpp 
            ${MegaDir}/src/user.cpp 
            ${MegaDir}/src/useralerts.cpp 
            ${MegaDir}/src/utils.cpp 
            ${MegaDir}/src/waiterbase.cpp 
            ${Mega_PlatformSpecificFiles} ${Mega_CryptoFiles} ${Mega_DbFiles} ${Mega_GfxFiles}  
            $<${USE_PREBUILT_3RDPARTY}:${prebuilt_dir}/libs/sqlite3.c>  
            $<${USE_LIBUV}:${MegaDir}/src/mega_evt_tls.cpp> 
            $<${USE_QT}:${MegaDir}/src/gfx/qt.cpp>)

target_include_directories(Mega PRIVATE ${MegaDir}/include ${Mega_PlatformSpecificIncludes})
target_include_directories(Mega PUBLIC ${MegaDir}/include ${Mega_PlatformSpecificIncludes})
target_link_libraries(Mega PUBLIC z 
                $<${USE_CRYPTOPP}:cryptopp> 
                $<${USE_SODIUM}:sodium> 
                $<$<AND:${USE_OPENSSL},$<NOT:${USE_WEBRTC}>>:ssl> 
                $<$<AND:${USE_OPENSSL},$<NOT:${USE_WEBRTC}>>:crypto> 
                $<${USE_WEBRTC}:webrtc> 
                $<${USE_CURL}:curl>  
                $<${USE_CURL}:cares> 
                $<$<NOT:${USE_PREBUILT_3RDPARTY}>:$<${USE_SQLITE}:sqlite3>>
                $<${USE_MEDIAINFO}:mediainfo> $<${USE_MEDIAINFO}:zen> 
                $<${USE_LIBUV}:uv> 
                $<${USE_FREEIMAGE}:freeimage> $<${USE_FREEIMAGE}:freeimage_IlmImf> $<${USE_FREEIMAGE}:freeimage_IlmImfUtil> $<${USE_FREEIMAGE}:freeimage_IlmThread> $<${USE_FREEIMAGE}:freeimage_Iex> $<${USE_FREEIMAGE}:freeimage_IexMath>
                                              $<${USE_FREEIMAGE}:freeimage_jpeg> $<${USE_FREEIMAGE}:freeimage_turbojpeg> $<${USE_FREEIMAGE}:freeimage_jpegxr> $<${USE_FREEIMAGE}:freeimage_jxrglue> $<${USE_FREEIMAGE}:freeimage_openjp2>
                                              $<${USE_FREEIMAGE}:freeimage_lzma>  $<${USE_FREEIMAGE}:freeimage_lcms2>  $<${USE_FREEIMAGE}:freeimage_raw>
                                              $<${USE_FREEIMAGE}:freeimage_tiff>  $<${USE_FREEIMAGE}:freeimage_tiffxx>  
                                              $<${USE_FREEIMAGE}:freeimage_jasper>  $<${USE_FREEIMAGE}:freeimage_libpng>  $<${USE_FREEIMAGE}:freeimage_half_s>
                                              $<${USE_FREEIMAGE}:freeimage_webp> $<${USE_FREEIMAGE}:freeimage_webpdecoder>  $<${USE_FREEIMAGE}:freeimage_webpdemux>  $<${USE_FREEIMAGE}:freeimage_webpmux>                                               
                $<${HAVE_FFMPEG}:ffmpeg_avformat> $<${HAVE_FFMPEG}:ffmpeg_avcodec> $<${HAVE_FFMPEG}:ffmpeg_avutil> $<${HAVE_FFMPEG}:ffmpeg_avfilter> $<${HAVE_FFMPEG}:ffmpeg_avdevice> $<${HAVE_FFMPEG}:ffmpeg_avdevice > 
                $<${HAVE_FFMPEG}:ffmpeg_swscale>  $<${HAVE_FFMPEG}:ffmpeg_swresample>  
                $<${USE_PDFIUM}:pdfium>
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
                $<${HAVE_FFMPEG}:HAVE_FFMPEG>
                $<${HAVE_LIBUV}:HAVE_LIBUV>
                $<${USE_CPPTHREAD}:USE_CPPTHREAD>
                $<${USE_QT}:USE_QT>
                $<${USE_PDFIUM}:HAVE_PDFIUM>)

if(WIN32)
target_link_libraries(Mega PUBLIC crypt32.lib)
endif(WIN32)

OPTION( ENABLE_CODECOVERAGE "Enable code coverage testing support" )

if ( ENABLE_CODECOVERAGE )

    if ( NOT CMAKE_BUILD_TYPE STREQUAL "Debug" )
        message( WARNING "Code coverage results with an optimised (non-Debug) build may be misleading" )
    endif ( NOT CMAKE_BUILD_TYPE STREQUAL "Debug" )

    if ( NOT DEFINED CODECOV_OUTPUTFILE )
        set( CODECOV_OUTPUTFILE cmake_coverage.output )
    endif ( NOT DEFINED CODECOV_OUTPUTFILE )

    if ( NOT DEFINED CODECOV_HTMLOUTPUTDIR )
        set( CODECOV_HTMLOUTPUTDIR coverage_results )
    endif ( NOT DEFINED CODECOV_HTMLOUTPUTDIR )

    if ( CMAKE_COMPILER_IS_GNUCXX OR CMAKE_COMPILER_IS_GNUCXX )
        find_program( CODECOV_GCOV gcov )
        find_program( CODECOV_LCOV lcov )
        find_program( CODECOV_GENHTML genhtml )
        add_definitions( -fprofile-arcs -ftest-coverage )
        link_libraries( gcov )
        set( CMAKE_EXE_LINKER_FLAGS ${CMAKE_EXE_LINKER_FLAGS} --coverage )
        add_custom_target( coverage_init ALL ${CODECOV_LCOV} --base-directory .  --directory ${CMAKE_BINARY_DIR} --output-file ${CODECOV_OUTPUTFILE} --capture --initial )
        add_custom_target( coverage ${CODECOV_LCOV} --base-directory .  --directory ${CMAKE_BINARY_DIR} --output-file ${CODECOV_OUTPUTFILE} --capture COMMAND genhtml -o ${CODECOV_HTMLOUTPUTDIR} ${CODECOV_OUTPUTFILE} )
endif ( CMAKE_COMPILER_IS_GNUCXX )

endif (ENABLE_CODECOVERAGE )

#test apps
add_executable(test_unit
    ${MegaDir}/tests/unit/Commands_test.cpp
    ${MegaDir}/tests/unit/constants.h
    ${MegaDir}/tests/unit/Crypto_test.cpp
    ${MegaDir}/tests/unit/DefaultedDirAccess.h
    ${MegaDir}/tests/unit/DefaultedFileAccess.h
    ${MegaDir}/tests/unit/DefaultedFileSystemAccess.h
    ${MegaDir}/tests/unit/FileFingerprint_test.cpp
    ${MegaDir}/tests/unit/FsNode.cpp
    ${MegaDir}/tests/unit/FsNode.h
    ${MegaDir}/tests/unit/main.cpp
    ${MegaDir}/tests/unit/MegaApi_test.cpp
    ${MegaDir}/tests/unit/NotImplemented.h
    ${MegaDir}/tests/unit/PayCrypter_test.cpp
    ${MegaDir}/tests/unit/Serialization_test.cpp
    ${MegaDir}/tests/unit/Sync_test.cpp
    ${MegaDir}/tests/unit/utils.cpp
    ${MegaDir}/tests/unit/utils.h
    ${MegaDir}/tests/unit/utils_test.cpp
)

add_executable(test_integration
    ${MegaDir}/tests/integration/main.cpp
    ${MegaDir}/tests/integration/SdkTest_test.cpp
    ${MegaDir}/tests/integration/Sync_test.cpp
)

add_executable(tool_purge_account
    ${MegaDir}/tests/tool/purge_account.cpp
)

target_compile_definitions(test_unit PRIVATE _SILENCE_TR1_NAMESPACE_DEPRECATION_WARNING)
target_compile_definitions(test_integration PRIVATE _SILENCE_TR1_NAMESPACE_DEPRECATION_WARNING)
target_compile_definitions(tool_purge_account PRIVATE _SILENCE_TR1_NAMESPACE_DEPRECATION_WARNING)
target_link_libraries(test_unit gtest Mega )
target_link_libraries(test_integration gtest Mega )
target_link_libraries(tool_purge_account gtest Mega )

if(WIN32)
add_executable(tool_tcprelay "${MegaDir}/tests/tool/tcprelay/main.cpp" "${MegaDir}/tests/tool/tcprelay/tcprelay.cpp")
target_include_directories(tool_tcprelay PUBLIC "${Mega3rdPartyDir}/../asio-1.10.6/include")
target_compile_definitions(tool_tcprelay PUBLIC -DASIO_STANDALONE -D_WIN32_WINNT=0x0A00)
target_link_libraries(tool_tcprelay Mega Dnsapi.lib)
endif(WIN32)

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

if (WIN32)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /W4")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /wd4201")  # nameless struct/union (nonstandard)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /wd4100")  # unreferenced formal parameter
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /wd4706")  # assignment within conditional
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /wd4458")  # identifier hides class member
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /wd4324")  # structure was padded due to alignment specifier (common in Sodium)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /wd4456")  # declaration hides previous local declaration
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /wd4266")  # derived class did not override all overloads of a virtual function
    #TODO: remove some of those gradually.  also consider: /wd4503 /wd4996 /wd4702
else()
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wconversion -Wno-unused-parameter")
endif()
