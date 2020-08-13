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

if (CMAKE_HOST_APPLE)
    set (USE_OPENSSL 0 CACHE STRING "")
else()
    set (USE_OPENSSL 1 CACHE STRING "")
ENDIF()

set (USE_ASIO 1 CACHE STRING "")
set (USE_CURL 1 CACHE STRING "")
set (USE_SQLITE 1 CACHE STRING "")
set (USE_MEDIAINFO 1 CACHE STRING "")
set (USE_FREEIMAGE 1 CACHE STRING "")
set (USE_SODIUM 1 CACHE STRING "")
set (ENABLE_SYNC 1 CACHE STRING "")
set (ENABLE_CHAT 0 CACHE STRING "")
set (ENABLE_LOG_PERFORMANCE 0 CACHE STRING "")
set (HAVE_FFMPEG 1 CACHE STRING "")
set (USE_WEBRTC 0 CACHE STRING "")
set (USE_LIBUV 0 CACHE STRING "")
set (USE_QT 0 CACHE STRING "")
set (USE_PDFIUM 0 CACHE STRING "")
set (USE_LIBRAW 0 CACHE STRING "")

if (USE_QT)
    set( USE_CPPTHREAD 0)
else()
    set( USE_CPPTHREAD 1)
endif()

set (HAVE_LIBUV ${USE_LIBUV})
set (HAVE_LIBRAW ${USE_LIBRAW})

if (WIN32 AND NOT USE_PREBUILT_3RDPARTY)
    set (USE_THIRDPARTY_FROM_VCPKG 1 CACHE STRING "")
else()
    set (USE_THIRDPARTY_FROM_VCPKG 0 CACHE STRING "")
endif()

message(STATUS " Using 3rd party from vcpkg = ${USE_THIRDPARTY_FROM_VCPKG}")

if (WIN32)
    set (NO_READLINE 1 CACHE STRING "")
    set (UNCHECKED_ITERATORS 0 CACHE STRING "")
    set (USE_PREBUILT_3RDPARTY 0 CACHE STRING "")
    IF (USE_WEBRTC)
        IF ("${WebRtcDir}" STREQUAL "")
            SET (WebRtcDir "${Mega3rdPartyDir}/libwebrtc/build32debug")
        ENDIF()
    ENDIF()
else(WIN32)
    set(NO_READLINE 0)
    set(USE_PREBUILT_3RDPARTY 0 CACHE STRING "")
endif(WIN32)

if (USE_WEBRTC)
    if ("${WebRtcDir}" STREQUAL "")
        SET(WebRtcDir ${Mega3rdPartyDir}/libwebrtc/build32debug)
    endif()
endif()

set (MEGA_LINK_DYNAMIC_CRT 1 CACHE STRING "")

if (ENABLE_CHAT AND NOT USE_SODIUM)
    message(FATAL_ERROR "ENABLE_CHAT (${ENABLE_CHAT}) requires USE_SODIUM (${USE_SODIUM})")
endif()

if (WIN32)
    message(STATUS "CMAKE_GENERATOR is ${CMAKE_GENERATOR}")
    message(STATUS "CMAKE_GENERATOR_PLATFORM is ${CMAKE_GENERATOR_PLATFORM}")
    if (("${CMAKE_GENERATOR_PLATFORM}" MATCHES "(Win64|IA64|x64)") OR
        ("${CMAKE_GENERATOR}" MATCHES "(Win64|IA64|x64)"))
        SET(build_64_bit 1)
        message(STATUS "Building 64 bit")
    elseif(CMAKE_SIZEOF_VOID_P EQUAL 4)
        SET(build_64_bit 0)
        message(STATUS "Building 32 bit")
    elseif("${CMAKE_CXX_COMPILER}" MATCHES "x64/cl.exe" )
        SET(build_64_bit 1)
        message(STATUS "Building 64 bit")
    elseif("${CMAKE_CXX_COMPILER}" MATCHES "x86/cl.exe" )
        SET(build_64_bit 0)
        message(STATUS "Building 32 bit")
    else()
        message(STATUS "CMAKE_SIZEOF_VOID_P ${CMAKE_SIZEOF_VOID_P}")
        message(STATUS "CMAKE_CXX_COMPILER ${CMAKE_CXX_COMPILER}")
        message(FATAL_ERROR "Can't tell if we should build 32 bit or 64 bit")
    endif()
else()
    set (build_64_bit 1 CACHE STRING "Build for a 64 bit target")
    message(STATUS "Building 64 bit")
endif()

if(APPLE)
    set(CMAKE_CXX_STANDARD 11)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -isysroot /Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk" )
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
        set (Mega3rdPartyDir "${MegaDir}/../3rdParty/" CACHE STRING "")
    endif()
ENDIF(WIN32)

# Since we offer some Qt support in the SDK, set up those dependencies here
if (USE_QT)

    #For convenience, added some default qt paths if none is specified by CMAKE_PREFIX_PATH
    if (CMAKE_HOST_WIN32 AND (NOT DEFINED CMAKE_PREFIX_PATH) )
        if (build_64_bit)
            set (QT_BASE_PATH "C:/Qt/Qt5.6.3_64/5.6.3/msvc2015" CACHE STRING "Specify your QT install folder if it is nonstandard")
            set(CMAKE_PREFIX_PATH "${QT_BASE_PATH};${CMAKE_PREFIX_PATH}") 
        else()
            set (QT_BASE_PATH "C:/Qt/Qt5.6.3/5.6.3/msvc2015" CACHE STRING "Specify your QT install folder if it is nonstandard")
            set(CMAKE_PREFIX_PATH "${QT_BASE_PATH};${CMAKE_PREFIX_PATH}")
        ENDIF()
    ENDIF()

    message(STATUS "CMAKE_PREFIX_PATH (should contain the QT install path): ${CMAKE_PREFIX_PATH}")

    foreach (PKG IN LISTS MEGA_QT_REQUIRED_COMPONENTS) 
        message(STATUS "Finding Qt package ${PKG}")
        find_package(Qt5 COMPONENTS ${PKG} REQUIRED)
    endforeach(PKG)

    # get qt version
    if ("${QT_QMAKE_EXECUTABLE}" STREQUAL "")
        set(QT_QMAKE_EXECUTABLE "${QT_DIR}/bin/qmake.exe")
    endif()

    execute_process(COMMAND ${QT_QMAKE_EXECUTABLE} -query QT_VERSION OUTPUT_VARIABLE QT_VERSION OUTPUT_STRIP_TRAILING_WHITESPACE)
    message(STATUS "Using QT ${QT_VERSION} from ${Qt5_DIR} ${QT_DIR}")

endif(USE_QT)

# currently supported scenarios for getting 3rd party libraries:
#  - prebuilt libraries, in a folder from mega    (set USE_PREBUILT_3RDPARTY and prebulit_dir (if the default does not suit))
#  - vcpkg built dependencies (there are some scripts that do most of that)  (do not set USE_PREBUILT_3RDPARTY, and set vcpkg_dir if the default does not suit, also set )
#  - libraries from the system, installed via eg. apt-get on linux

if (USE_PREBUILT_3RDPARTY)
    if ("${prebuilt_dir}" STREQUAL "")
        SET (prebuilt_dir ${Mega3rdPartyDir} CACHE STRING "Location of prebuilt 3rd party folder from MEGA.nz")
    ENDIF()

    include_directories( "${Mega3rdPartyDir}/include" )
    include_directories( "${Mega3rdPartyDir}/include/curl" )
    include_directories( "${Mega3rdPartyDir}/include/libsodium" )
    include_directories( "${Mega3rdPartyDir}/include/cares" )
    include_directories( "${Mega3rdPartyDir}/include/mediainfo" )
    include_directories( "${Mega3rdPartyDir}/include/zenlib" )
    include_directories( "${Mega3rdPartyDir}/include/pdfium" )
    include_directories( "${Mega3rdPartyDir}/include/zlib" )
    include_directories( "${Mega3rdPartyDir}/include/libuv" )
    include_directories( "${Mega3rdPartyDir}/include/ffmpeg" )

    link_directories( "${Mega3rdPartyDir}/libs" )

elseif (USE_THIRDPARTY_FROM_VCPKG) #vcpkg or system

    #determine VCPKG triplet
    IF(WIN32)
        if (UNCHECKED_ITERATORS)
            if(build_64_bit)
                set(VCPKG_TRIPLET "x64-windows-mega-uncheckediterators" CACHE STRING "")
            else(build_64_bit)
                set(VCPKG_TRIPLET "x86-windows-mega-uncheckediterators" CACHE STRING "")
            endif(build_64_bit)
        else() 
            if(build_64_bit)
                set(VCPKG_TRIPLET "x64-windows-mega" CACHE STRING "")
            else(build_64_bit)
                set(VCPKG_TRIPLET "x86-windows-mega" CACHE STRING "")
            endif(build_64_bit)
        endif()

    ELSE(WIN32)
         IF(CMAKE_HOST_APPLE)
            set(VCPKG_TRIPLET "x64-osx" CACHE STRING "")
        ELSE(CMAKE_HOST_APPLE)
            set(VCPKG_TRIPLET "x64-linux" CACHE STRING "")
        endif(CMAKE_HOST_APPLE)
    ENDIF(WIN32)

    set(vcpkg_dir "${Mega3rdPartyDir}/vcpkg/installed/${VCPKG_TRIPLET}")
endif()

message(STATUS, " prebuilt_dir = ${Mega3rdPartyDir}")
message(STATUS, " USE_PREBUILT_3RDPARTY = ${USE_PREBUILT_3RDPARTY}")
message(STATUS, " Mega3rdPartyDir = ${Mega3rdPartyDir}")
message(STATUS, " USE_THIRDPARTY_FROM_VCPKG = ${USE_THIRDPARTY_FROM_VCPKG}")
message(STATUS, " vcpkg_dir = ${vcpkg_dir}")


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
        #Link against the static C/C++ libraries on windows. Though, if linking with prebuilt QT we need dynamic CRT
        foreach(flag_var
                CMAKE_CXX_FLAGS CMAKE_CXX_FLAGS_DEBUG CMAKE_CXX_FLAGS_RELEASE 
                CMAKE_C_FLAGS CMAKE_C_FLAGS_DEBUG CMAKE_C_FLAGS_RELEASE
                CMAKE_CXX_FLAGS_MINSIZEREL CMAKE_CXX_FLAGS_RELWITHDEBINFO)
            if (MEGA_LINK_DYNAMIC_CRT)
                if(${flag_var} MATCHES "/MT")
                    string(REGEX REPLACE "/MT" "/MD" ${flag_var} "${${flag_var}}")
                endif()
            else ()
                if(${flag_var} MATCHES "/MD")
                    string(REGEX REPLACE "/MD" "/MT" ${flag_var} "${${flag_var}}")
                endif()
            endif ()
        endforeach(flag_var)
    ENDIF()

    set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /MP")
    set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /MP")
    set (CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /Zi")
    set (CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} /Zi")


    # for inet_ntoa (which is available in XP)
    add_definitions( -D_WINSOCK_DEPRECATED_NO_WARNINGS )
ENDIF(WIN32)

include(CheckIncludeFile)
include(CheckFunctionExists)
check_include_file(inttypes.h HAVE_INTTYPES_H)
check_include_file(dirent.h HAVE_DIRENT_H)
check_include_file(uv.h HAVE_LIBUV)
check_function_exists(aio_write, HAVE_AIO_RT)


function(ImportStaticLibrary libName includeDir lib32debug lib32release lib64debug lib64release)
    # function to import a library with different files for 32/64 & debug/release
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

function(ImportALibrary libName includeDir) #receives also libfileDebug & libfileRelease
    # supports alternating debug/release lib names for libraries that have more than one .lib
    # (however it seems the IMPORTED_LOCATION only supports a single one, so it only supports one debug and one release (for now))
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
endfunction(ImportALibrary)

function(ImportStdVcpkgLibrary libName winDbg win linDbg lin)
    if (WIN32 OR EXISTS ${vcpkg_dir}/lib/${lin}.a)
        add_library(${libName} STATIC IMPORTED)
        message("adding library: " ${vcpkg_dir}/lib/${lin}.a)
        # function that imports vcpkg libraries (receiving different names for win/linux)
        set_property(TARGET ${libName} PROPERTY INTERFACE_INCLUDE_DIRECTORIES ${vcpkg_dir}/include)
        if (WIN32)
            set_property(TARGET ${libName} APPEND PROPERTY IMPORTED_LOCATION  ${vcpkg_dir}/lib/${win}.lib)
            set_property(TARGET ${libName} APPEND PROPERTY IMPORTED_LOCATION_DEBUG ${vcpkg_dir}/debug/lib/${winDbg}.lib)
        else()
                set_property(TARGET ${libName} APPEND PROPERTY IMPORTED_LOCATION  ${vcpkg_dir}/lib/${lin}.a)
                set_property(TARGET ${libName} APPEND PROPERTY IMPORTED_LOCATION_DEBUG ${vcpkg_dir}/debug/lib/${linDbg}.a)
        endif()
    endif()

endfunction(ImportStdVcpkgLibrary)

function(ImportHeaderLibrary libName includeDir)
    add_library(${libName} INTERFACE IMPORTED)
    set_property(TARGET ${libName} PROPERTY INTERFACE_INCLUDE_DIRECTORIES ${includeDir})
endfunction(ImportHeaderLibrary)

IF(WIN32)
if(USE_PREBUILT_3RDPARTY) #TODO: do this explicitly for !Win32? (currently relying on lib path & -lLIBNAME)
    
        ImportALibrary(z               "${prebuilt_dir}/include" "${prebuilt_dir}/libs/x32d/zlibstat.lib" "${prebuilt_dir}/libs/x32/zlibstat.lib")
        
        IF(USE_CRYPTOPP)
            ImportALibrary(cryptopp        "${prebuilt_dir}/include" "${prebuilt_dir}/libs/x32d/cryptopp.lib"  "${prebuilt_dir}/libs/x32/cryptopp.lib")
        ENDIF(USE_CRYPTOPP)

        IF(USE_CURL)
            ImportALibrary(curl        "${prebuilt_dir}/include" "${prebuilt_dir}/libs/x32d/libcurl.lib"  "${prebuilt_dir}/libs/x32/libcurl.lib")
            ImportALibrary(cares       "${prebuilt_dir}/include/cares" "${prebuilt_dir}/libs/x32d/cares.lib" "${prebuilt_dir}/libs/x32/cares.lib")
        ENDIF(USE_CURL)
        
        IF(USE_SODIUM)
            ImportALibrary(sodium      "${prebuilt_dir}/include" "${prebuilt_dir}/libs/x32d/sodium.lib"  "${prebuilt_dir}/libs/x32/sodium.lib")
        ENDIF(USE_SODIUM)
        
        IF(USE_OPENSSL)
            ImportALibrary(ssl         "${prebuilt_dir}/include" "${prebuilt_dir}/libs/x32d/ssleay32.lib" "${prebuilt_dir}/libs/x32/ssleay32.lib")
            ImportALibrary(crypto      "${prebuilt_dir}/include" "${prebuilt_dir}/libs/x32d/libeay32.lib" "${prebuilt_dir}/libs/x32/libeay32.lib")
        ENDIF(USE_OPENSSL)
        
        IF(USE_MEDIAINFO)
            ImportALibrary(mediainfo    "${prebuilt_dir}/include" "${prebuilt_dir}/libs/x32d/MediaInfo.lib" "${prebuilt_dir}/libs/x32/MediaInfo.lib")
            ImportALibrary(zen          "${prebuilt_dir}/include" "${prebuilt_dir}/libs/x32d/ZenLib.lib" "${prebuilt_dir}/libs/x32/ZenLib.lib")
        ENDIF(USE_MEDIAINFO)
        
        IF(HAVE_FFMPEG)
        IF(WIN32)
            ImportALibrary(avutil "${prebuilt_dir}/include/ffmpeg" "${prebuilt_dir}/libs/x32d/avutil.lib" "${prebuilt_dir}/libs/x32/avutil.lib")
            ImportALibrary(avformat "${prebuilt_dir}/include/ffmpeg" "${prebuilt_dir}/libs/x32d/avformat.lib" "${prebuilt_dir}/libs/x32/avformat.lib")
            ImportALibrary(avcodec "${prebuilt_dir}/include/ffmpeg" "${prebuilt_dir}/libs/x32d/avcodec.lib" "${prebuilt_dir}/libs/x32/avcodec.lib")
            ImportALibrary(avfilter "${prebuilt_dir}/include/ffmpeg" "${prebuilt_dir}/libs/x32d/avfilter.lib" "${prebuilt_dir}/libs/x32/avfilter.lib")
            ImportALibrary(avdevice "${prebuilt_dir}/include/ffmpeg" "${prebuilt_dir}/libs/x32d/avdevice.lib" "${prebuilt_dir}/libs/x32/avdevice.lib")
            ImportALibrary(swscale  "${prebuilt_dir}/include/ffmpeg" "${prebuilt_dir}/libs/x32d/swscale.lib"  "${prebuilt_dir}/libs/x32/swscale.lib")
            ImportALibrary(swresample "${prebuilt_dir}/include/ffmpeg" "${prebuilt_dir}/libs/x32d/swresample.lib"  "${prebuilt_dir}/libs/x32/swresample.lib")
        else()
            ImportALibrary(avformat "${prebuilt_dir}/include/ffmpeg" "${prebuilt_dir}/libs/x32d/avformat.lib" "${prebuilt_dir}/libs/x32/avformat.lib")
            ImportALibrary(avutil "${prebuilt_dir}/include/ffmpeg" "${prebuilt_dir}/libs/x32d/avutil.lib" "${prebuilt_dir}/libs/x32/avutil.lib")
            ImportALibrary(avcodec "${prebuilt_dir}/include/ffmpeg" "${prebuilt_dir}/libs/x32d/avcodec.lib" "${prebuilt_dir}/libs/x32/avcodec.lib")
            ImportALibrary(avfilter "${prebuilt_dir}/include/ffmpeg" "${prebuilt_dir}/libs/x32d/avfilter.lib" "${prebuilt_dir}/libs/x32/avfilter.lib")
            ImportALibrary(avdevice "${prebuilt_dir}/include/ffmpeg" "${prebuilt_dir}/libs/x32d/avdevice.lib" "${prebuilt_dir}/libs/x32/avdevice.lib")
            ImportALibrary(swscale  "${prebuilt_dir}/include/ffmpeg" "${prebuilt_dir}/libs/x32d/swscale.lib"  "${prebuilt_dir}/libs/x32/swscale.lib")
            ImportALibrary(swresample "${prebuilt_dir}/include/ffmpeg" "${prebuilt_dir}/libs/x32d/swresample.lib"  "${prebuilt_dir}/libs/x32/swresample.lib")

        endif()
        ENDIF(HAVE_FFMPEG)
        
        IF(USE_LIBUV)
            ImportALibrary(uv       "${prebuilt_dir}/include/libuv" "${prebuilt_dir}/libs/x32d/libuv.lib" "${prebuilt_dir}/libs/x32/libuv.lib")
        ENDIF(USE_LIBUV)

        IF(USE_LIBRAW)
            add_definitions( -DLIBRAW_NODLL)
            ImportALibrary(raw       "${prebuilt_dir}/include/libraw" "${prebuilt_dir}/libs/x32d/libraw.lib" "${prebuilt_dir}/libs/x32/libraw.lib")
        ENDIF(USE_LIBRAW)

        IF(USE_PDFIUM)
            ImportALibrary(pdfium       "${prebuilt_dir}/include" "${prebuilt_dir}/libs/x32d/pdfium.lib" "${prebuilt_dir}/libs/x32/pdfium.lib")
        ENDIF(USE_PDFIUM)
        
        ImportALibrary(gtest           "${prebuilt_dir}/include" "${prebuilt_dir}/libs/x32d/gtestd.lib" "${prebuilt_dir}/libs/x32/gtest.lib")

    endif(USE_PREBUILT_3RDPARTY)
endif(WIN32)

if (USE_THIRDPARTY_FROM_VCPKG)
        IF(USE_CRYPTOPP)
            ImportStdVcpkgLibrary(cryptopp        cryptopp-static cryptopp-static libcryptopp libcryptopp)
        ENDIF(USE_CRYPTOPP)

        IF(USE_SODIUM)
            ImportStdVcpkgLibrary(sodium          libsodium libsodium libsodium libsodium)
        ENDIF(USE_SODIUM)

        ImportStdVcpkgLibrary(z               zlibd zlib libz libz) 

        IF(USE_CURL)
            IF(USE_WEBRTC)
                ImportStdVcpkgLibrary(curl         "${Mega3rdPartyDir}/curl/include" "${Mega3rdPartyDir}/curl/build32/lib/Debug/libcurl-d.lib" "${Mega3rdPartyDir}/curl/build32/lib/Release/libcurl.lib")
            ELSE()
                ImportStdVcpkgLibrary(curl        libcurl-d libcurl libcurl-d libcurl)
                #find_package(CURL CONFIG REQUIRED PATHS "${vcpkg_dir}/share/curl" NO_DEFAULT_PATH )
            ENDIF()
        ENDIF()
        IF(USE_CURL)
            ImportStdVcpkgLibrary(cares       cares cares libcares libcares)
        ENDIF()

        IF(USE_OPENSSL)
            IF(USE_WEBRTC)
                include_directories( "${WebRtcDir}/webrtc/src/third_party/boringssl/src/include" )
            ELSE()
                #ImportStdVcpkgLibrary(ssl         ssleay32 ssleay32 libssl libssl) # prior openssl 1.1.0
                ImportStdVcpkgLibrary(ssl         libssl libssl libssl libssl)
                #ImportStdVcpkgLibrary(crypto      libeay32 libeay32 libcrypto libcrypto) # prior openssl 1.1.0
                ImportStdVcpkgLibrary(crypto      libcrypto libcrypto libcrypto libcrypto)

            ENDIF()
        ENDIF()

        IF(USE_WEBRTC)
            ImportALibrary(webrtc "${WebRtcDir}/include" "${WebRtcDir}/lib/webrtc.lib" "${WebRtcDir}/lib/webrtc.lib")
            add_definitions( -DWEBRTC_WIN )
        ENDIF()
    
        ImportStdVcpkgLibrary(gtest           gtestd gtest libgtestd libgtest) 

        IF(USE_MEDIAINFO)
            IF(CMAKE_HOST_APPLE)
                set(DEBUG_SUFIX _debug)
            endif()
            ImportStdVcpkgLibrary(zen        zend zen libzen${DEBUG_SUFIX} libzen)
            ImportStdVcpkgLibrary(mediainfo  mediainfod mediainfo libmediainfo${DEBUG_SUFIX} libmediainfo)
        ENDIF(USE_MEDIAINFO)

        IF(USE_FREEIMAGE)
            ImportALibrary(freeimage    "${vcpkg_dir}/include" "${vcpkg_dir}/debug/lib/FreeImaged.lib"  "${vcpkg_dir}/lib/FreeImage.lib")
            ImportALibrary(freeimage_Iex "${vcpkg_dir}/include" "${vcpkg_dir}/debug/lib/Iex-2_3_d.lib"  "${vcpkg_dir}/lib/Iex-2_3.lib")
            ImportALibrary(freeimage_IexMath "${vcpkg_dir}/include" "${vcpkg_dir}/debug/lib/IexMath-2_3_d.lib"  "${vcpkg_dir}/lib/IexMath-2_3.lib")
            ImportALibrary(freeimage_IlmImf "${vcpkg_dir}/include" "${vcpkg_dir}/debug/lib/IlmImf-2_3_d.lib"  "${vcpkg_dir}/lib/IlmImf-2_3.lib")
            ImportALibrary(freeimage_IlmImfUtil "${vcpkg_dir}/include" "${vcpkg_dir}/debug/lib/IlmImfUtil-2_3_d.lib"  "${vcpkg_dir}/lib/IlmImfUtil-2_3.lib")
            ImportALibrary(freeimage_IlmThread "${vcpkg_dir}/include" "${vcpkg_dir}/debug/lib/IlmThread-2_3_d.lib"  "${vcpkg_dir}/lib/IlmThread-2_3.lib")
            ImportALibrary(freeimage_jpeg "${vcpkg_dir}/include" "${vcpkg_dir}/debug/lib/jpegd.lib"  "${vcpkg_dir}/lib/jpeg.lib")
            ImportALibrary(freeimage_turbojpeg "${vcpkg_dir}/include" "${vcpkg_dir}/debug/lib/turbojpegd.lib"  "${vcpkg_dir}/lib/turbojpeg.lib")
            ImportALibrary(freeimage_jpegxr "${vcpkg_dir}/include" "${vcpkg_dir}/debug/lib/jpegxrd.lib"  "${vcpkg_dir}/lib/jpegxr.lib")
            ImportALibrary(freeimage_jxrglue "${vcpkg_dir}/include" "${vcpkg_dir}/debug/lib/jxrglued.lib"  "${vcpkg_dir}/lib/jxrglue.lib")
            ImportALibrary(freeimage_openjp2 "${vcpkg_dir}/include" "${vcpkg_dir}/debug/lib/openjp2.lib"  "${vcpkg_dir}/lib/openjp2.lib")
            ImportALibrary(freeimage_half "${vcpkg_dir}/include" "${vcpkg_dir}/debug/lib/half-2_3_d.lib"  "${vcpkg_dir}/lib/half-2_3.lib")
            ImportALibrary(freeimage_jasper "${vcpkg_dir}/include" "${vcpkg_dir}/debug/lib/jasperd.lib"  "${vcpkg_dir}/lib/jasper.lib")
            ImportALibrary(freeimage_libpng "${vcpkg_dir}/include" "${vcpkg_dir}/debug/lib/libpng16d.lib"  "${vcpkg_dir}/lib/libpng16.lib")
            ImportALibrary(freeimage_lzma    "${vcpkg_dir}/include" "${vcpkg_dir}/debug/lib/lzmad.lib"  "${vcpkg_dir}/lib/lzma.lib")
            ImportALibrary(freeimage_lcms2    "${vcpkg_dir}/include" "${vcpkg_dir}/debug/lib/lcmsd.lib"  "${vcpkg_dir}/lib/lcms.lib")
            ImportALibrary(freeimage_raw    "${vcpkg_dir}/include" "${vcpkg_dir}/debug/lib/rawd.lib"  "${vcpkg_dir}/lib/raw.lib")
            ImportALibrary(freeimage_tiff    "${vcpkg_dir}/include" "${vcpkg_dir}/debug/lib/tiffd.lib"  "${vcpkg_dir}/lib/tiff.lib")
            ImportALibrary(freeimage_tiffxx    "${vcpkg_dir}/include" "${vcpkg_dir}/debug/lib/tiffxxd.lib"  "${vcpkg_dir}/lib/tiffxx.lib")
            ImportALibrary(freeimage_webp    "${vcpkg_dir}/include" "${vcpkg_dir}/debug/lib/webpd.lib"  "${vcpkg_dir}/lib/webp.lib")
            ImportALibrary(freeimage_webpdecoder    "${vcpkg_dir}/include" "${vcpkg_dir}/debug/lib/webpdecoderd.lib"  "${vcpkg_dir}/lib/webpdecoder.lib")
            ImportALibrary(freeimage_webpdemux    "${vcpkg_dir}/include" "${vcpkg_dir}/debug/lib/webpdemuxd.lib"  "${vcpkg_dir}/lib/webpdemux.lib")
            ImportALibrary(freeimage_webpmux    "${vcpkg_dir}/include" "${vcpkg_dir}/debug/lib/libwebpmuxd.lib"  "${vcpkg_dir}/lib/libwebpmux.lib")

            set (freeimage_deps freeimage_Iex freeimage_IexMath freeimage_IlmImf freeimage_IlmImfUtil
                freeimage_IlmThread freeimage_jpeg freeimage_turbojpeg freeimage_jpegxr freeimage_jxrglue freeimage_openjp2
                freeimage_half freeimage_jasper freeimage_libpng freeimage_lzma freeimage_lcms2 freeimage_raw freeimage_tiff
                freeimage_tiffxx freeimage_webp freeimage_webpdecoder freeimage_webpdemux freeimage_webpmux )


        ENDIF(USE_FREEIMAGE)

        IF(HAVE_FFMPEG)
            ImportStdVcpkgLibrary(avformat avformat avformat libavformat libavformat)
            ImportStdVcpkgLibrary(avutil avutil avutil libavutil libavutil)
            ImportStdVcpkgLibrary(avcodec avcodec avcodec libavcodec libavcodec)
            ImportStdVcpkgLibrary(avfilter avfilter avfilter libavfilter libavfilter)
            ImportStdVcpkgLibrary(avdevice avdevice avdevice libavdevice libavdevice)
            ImportStdVcpkgLibrary(swscale  swscale swscale libswscale libswscale)
            ImportStdVcpkgLibrary(swresample swresample swresample libswresample libswresample)
        ENDIF(HAVE_FFMPEG)

        IF(USE_SQLITE)
            ImportStdVcpkgLibrary(sqlite3          sqlite3 sqlite3 libsqlite3 libsqlite3)
        ENDIF(USE_SQLITE)

        IF(USE_LIBUV)
            ImportStdVcpkgLibrary(uv       libuv libuv liblibuv liblibuv)
        ENDIF(USE_LIBUV)

        IF(USE_LIBRAW)
            ImportStdVcpkgLibrary(raw                  rawd raw librawd libraw)
            ImportStdVcpkgLibrary(raw_lcms           lcmsd lcms liblcmsd liblcms)

            IF(NOT CMAKE_HOST_APPLE)
            ImportStdVcpkgLibrary(raw_jasper           jasperd jasper libjasperd libjasper)
            ImportStdVcpkgLibrary(raw_libjpeg_turbo    turbojpegd turbojpeg libturbojpeg libturbojpeg)
            SET(raw_deps raw_jasper raw_libjpeg_turbo raw_lcms)
            else()
                SET(raw_deps  raw_lcms)
            endif()

            IF(NOT CMAKE_HOST_APPLE AND NOT CMAKE_HOST_WIN32)
                set(Mega_PlatformSpecificLibs ${Mega_PlatformSpecificLibs} -fopenmp)
            ENDIF()

        ENDIF(USE_LIBRAW)

        IF(USE_PDFIUM)
            ImportStdVcpkgLibrary(pdfium       pdfium pdfium libpdfium libpdfium)

            ImportStdVcpkgLibrary(freetype     freetyped freetype libfreetyped libfreetype)
            ImportStdVcpkgLibrary(icu          icuucd icuuc libicuuc libicuuc)
            ImportStdVcpkgLibrary(lcms         lcmsd lcms liblcmsd liblcms)
            ImportStdVcpkgLibrary(turbojpeg    jpegd jpeg libjpeg libjpeg)
            ImportStdVcpkgLibrary(openjpeg     openjp2 openjp2 libopenjp2 libopenjp2)

            IF (CMAKE_HOST_APPLE)
                ImportStdVcpkgLibrary(bzip2       bz2 bz2 libbz2d libbz2)
                set (pdfium_deps bzip2)
            ELSEIF (CMAKE_HOST_UNIX)
                set(ENV{PKG_CONFIG_PATH} "${vcpkg_dir}/lib/pkgconfig")
                include(FindPkgConfig)
                pkg_check_modules(FTYPECONF freetype2)
                set(freetype_deps ${FTYPECONF_STATIC_LIBRARIES})
            ENDIF(CMAKE_HOST_APPLE)

            set(pdfium_deps  ${pdfium_deps} freetype ${freetype_deps} icu lcms turbojpeg openjpeg)
        ENDIF(USE_PDFIUM)

endif(USE_THIRDPARTY_FROM_VCPKG)

#we must compile with UNICODE defined or our link symbols won't match libmediainfo
# Migration from autotools note: there was a check that determined if libmediainfo was compiled with UNICODE.
set(UNICODE 1)

if(WIN32)
    
    add_definitions(-D_CRT_SECURE_NO_WARNINGS -DCURL_STATICLIB -DCARES_STATICLIB -DWIN32_LEAN_AND_MEAN -DSODIUM_STATIC -DPCRE_STATICWIN32 -D_CONSOLE )
    SET(Mega_PlatformSpecificIncludes ${MegaDir}/include/mega/$<IF:${USE_CURL},wincurl,win32>)
    SET(Mega_PlatformSpecificLibs ${Mega_PlatformSpecificLibs} ws2_32 winhttp Shlwapi Secur32.lib  $<$<OR:${USE_CURL},${USE_WEBRTC}>:Wldap32.lib> )
    IF(USE_LIBUV)
        SET(Mega_PlatformSpecificLibs ${Mega_PlatformSpecificLibs} Kernel32.lib Iphlpapi.lib Userenv.lib Psapi.lib )
    ENDIF(USE_LIBUV)

    SET(Mega_PlatformSpecificFiles ${MegaDir}/src/win32/console.cpp
    ${MegaDir}/src/win32/consolewaiter.cpp 
    ${MegaDir}/src/win32/fs.cpp 
    $<IF:${USE_CURL},${MegaDir}/src/posix/net.cpp,${MegaDir}/src/win32/net.cpp>
    ${MegaDir}/src/win32/waiter.cpp 
    $<${USE_CPPTHREAD}:${MegaDir}/src/thread/cppthread.cpp>
    )

ELSE(WIN32)

    set(USE_PTHREAD 1)

    check_include_file(glob.h HAVE_GLOB_H)
    if (HAVE_GLOB_H)
        set(GLOB_H_FOUND 1)
    else()
        set(GLOB_H_FOUND 0)   #some versions on some platforms leave it undefined if not found
    endif()

    SET(Mega_PlatformSpecificFiles $<$<NOT:${GLOB_H_FOUND}>:${MegaDir}/src/mega_glob.c> ${MegaDir}/src/posix/console.cpp ${MegaDir}/src/posix/consolewaiter.cpp ${MegaDir}/src/posix/fs.cpp ${MegaDir}/src/posix/net.cpp ${MegaDir}/src/posix/waiter.cpp ${MegaDir}/src/thread/posixthread.cpp $<${USE_CPPTHREAD}:${MegaDir}/src/thread/cppthread.cpp> )
    IF(APPLE)
        SET(Mega_PlatformSpecificFiles ${Mega_PlatformSpecificFiles} ${MegaDir}/src/osx/osxutils.mm )
    ENDIF()

    SET(Mega_PlatformSpecificIncludes ${MegaDir}/include/mega/posix)

    SET(Mega_PlatformSpecificLibs ${Mega_PlatformSpecificLibs} pthread z dl termcap)
    IF(APPLE)
        SET(Mega_PlatformSpecificLibs ${Mega_PlatformSpecificLibs} "-framework Cocoa -framework SystemConfiguration -framework Security")
    ELSE()
        SET(Mega_PlatformSpecificLibs ${Mega_PlatformSpecificLibs} crypto rt stdc++fs)
    ENDIF()

    IF(USE_WEBRTC)
        add_definitions( -DWEBRTC_POSIX )
    ENDIF()

ENDIF(WIN32)

configure_file ("${MegaDir}/contrib/cmake/config.h.in" "${MegaDir}/include/mega/config.h" )
add_definitions( -DHAVE_CONFIG_H) #otherwise, it won't be included in Windows build!

SET(Mega_CryptoFiles ${MegaDir}/src/crypto/cryptopp.cpp ${MegaDir}/src/crypto/sodium.cpp)
SET(Mega_DbFiles ${MegaDir}/src/db/sqlite.cpp ${MegaDir}/src/db/sqlite.cpp )
SET(Mega_GfxFiles ${MegaDir}/src/gfx/external.cpp ${MegaDir}/src/gfx/freeimage.cpp ) 

if(WIN32)
    SET(WIN_EXTRA_INCLUDE $<${USE_PREBUILT_3RDPARTY}:${prebuilt_dir}/libs/sqlite3.c>)
endif()

if(CMAKE_HOST_APPLE)
    SET(MACOS_EXTRA_INCLUDE $<${USE_PREBUILT_3RDPARTY}:${prebuilt_dir}/sqlite3.c>)
endif()

add_library(Mega STATIC
            ${MegaDir}/include/megaapi.h
            ${MegaDir}/include/megaapi_impl.h
            ${MegaDir}/include/mega/osx/osxutils.h
            ${MegaDir}/include/mega/transferslot.h
            ${MegaDir}/include/mega/thread/qtthread.h
            ${MegaDir}/include/mega/thread/win32thread.h #TODO:only win32
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
            ${WIN_EXTRA_INCLUDE}
            ${MACOS_EXTRA_INCLUDE}
            $<${USE_LIBUV}:${MegaDir}/src/mega_evt_tls.cpp> 
            $<${USE_QT}:${MegaDir}/src/gfx/qt.cpp>
            $<${USE_QT}:${MegaDir}/src/thread/qtthread.cpp >
            )

target_include_directories(Mega PRIVATE ${MegaDir}/include ${Mega_PlatformSpecificIncludes})
target_include_directories(Mega PUBLIC ${MegaDir}/include ${Mega_PlatformSpecificIncludes})

if (WIN32)
    set(Mega_PlatformSpecificLibs ${Mega_PlatformSpecificLibs}
        $<${HAVE_FFMPEG}:avfilter> $<${HAVE_FFMPEG}:avdevice> $<${HAVE_FFMPEG}:avdevice> $<${HAVE_FFMPEG}:swresample>
        $<${HAVE_FFMPEG}:avformat> $<${HAVE_FFMPEG}:avcodec> $<${HAVE_FFMPEG}:avutil> $<${HAVE_FFMPEG}:swscale>
        )
else()
    set(Mega_PlatformSpecificLibs ${Mega_PlatformSpecificLibs}
        $<${HAVE_FFMPEG}:avformat> $<${HAVE_FFMPEG}:avcodec> $<${HAVE_FFMPEG}:avutil> $<${HAVE_FFMPEG}:swscale>
        )
    if (CMAKE_HOST_APPLE)
        set(Mega_PlatformSpecificLibs ${Mega_PlatformSpecificLibs} $<${HAVE_FFMPEG}:-liconv>)
    endif()
endif()

foreach (PKG IN LISTS MEGA_QT_LINK_LIBRARIES) 
    target_link_libraries(Mega PUBLIC ${PKG} )
endforeach(PKG)

target_link_libraries(Mega PUBLIC
                        $<${USE_CRYPTOPP}:cryptopp>
                        $<${USE_SODIUM}:sodium>
                        $<${USE_WEBRTC}:webrtc>
                        $<${USE_MEDIAINFO}:mediainfo> $<${USE_MEDIAINFO}:zen>
                        $<${USE_CURL}:curl>
                        $<${USE_CURL}:cares>
                        $<$<AND:${USE_OPENSSL},$<NOT:${USE_WEBRTC}>>:ssl>
                        $<$<AND:${USE_OPENSSL},$<NOT:${USE_WEBRTC}>>:crypto>
                        $<$<NOT:${USE_PREBUILT_3RDPARTY}>:$<${USE_SQLITE}:sqlite3>>
                        $<${USE_LIBUV}:uv>
                        $<${USE_LIBRAW}:raw> $<${USE_LIBRAW}:${raw_deps}>
                        $<${USE_FREEIMAGE}:freeimage> $<${USE_FREEIMAGE}:${freeimage_deps}>
                        $<${USE_PDFIUM}:pdfium> $<${USE_PDFIUM}:${pdfium_deps}>
                        z
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
                $<${ENABLE_LOG_PERFORMANCE}:ENABLE_LOG_PERFORMANCE>
                $<${NO_READLINE}:NO_READLINE>
                $<${USE_FREEIMAGE}:USE_FREEIMAGE> 
                $<${HAVE_FFMPEG}:HAVE_FFMPEG>
                $<${HAVE_LIBUV}:HAVE_LIBUV>
                $<${USE_CPPTHREAD}:USE_CPPTHREAD>
                $<${USE_QT}:USE_QT>
                $<${USE_PDFIUM}:HAVE_PDFIUM>)

if (WIN32)
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
    ${MegaDir}/tests/unit/AttrMap_test.cpp
    ${MegaDir}/tests/unit/ChunkMacMap_test.cpp
    ${MegaDir}/tests/unit/Commands_test.cpp
    ${MegaDir}/tests/unit/constants.h
    ${MegaDir}/tests/unit/Crypto_test.cpp
    ${MegaDir}/tests/unit/DefaultedDbTable.h
    ${MegaDir}/tests/unit/DefaultedDirAccess.h
    ${MegaDir}/tests/unit/DefaultedFileAccess.h
    ${MegaDir}/tests/unit/DefaultedFileSystemAccess.h
    ${MegaDir}/tests/unit/FileFingerprint_test.cpp
    ${MegaDir}/tests/unit/File_test.cpp
    ${MegaDir}/tests/unit/FsNode.cpp
    ${MegaDir}/tests/unit/FsNode.h
    ${MegaDir}/tests/unit/Logging_test.cpp
    ${MegaDir}/tests/unit/main.cpp
    ${MegaDir}/tests/unit/MediaProperties_test.cpp
    ${MegaDir}/tests/unit/MegaApi_test.cpp
    ${MegaDir}/tests/unit/NotImplemented.h
    ${MegaDir}/tests/unit/PayCrypter_test.cpp
    ${MegaDir}/tests/unit/PendingContactRequest_test.cpp
    ${MegaDir}/tests/unit/Serialization_test.cpp
    ${MegaDir}/tests/unit/Share_test.cpp
    ${MegaDir}/tests/unit/Sync_test.cpp
    ${MegaDir}/tests/unit/TextChat_test.cpp
    ${MegaDir}/tests/unit/Transfer_test.cpp
    ${MegaDir}/tests/unit/User_test.cpp
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
if(APPLE)
    target_link_libraries(test_integration "-framework Security" )
endif()
target_link_libraries(tool_purge_account gtest Mega )

if (USE_ASIO)
    if (USE_THIRDPARTY_FROM_VCPKG)
        if (EXISTS "${vcpkg_dir}/include/asio.hpp")
            set(HAVE_ASIO 1 CACHE STRING "" FORCE)
        else()
            set(HAVE_ASIO 0 CACHE STRING "" FORCE)
        endif()
    endif()
endif()

if (HAVE_ASIO)
    add_executable(tool_tcprelay "${MegaDir}/tests/tool/tcprelay/main.cpp" "${MegaDir}/tests/tool/tcprelay/tcprelay.cpp")
    target_include_directories(tool_tcprelay PUBLIC "${vcpkg_dir}/installed/include")
    target_compile_definitions(tool_tcprelay PUBLIC -DASIO_STANDALONE)
    target_link_libraries(tool_tcprelay Mega)

    if (WIN32)
        target_compile_definitions(tool_tcprelay PUBLIC -D_WIN32_WINNT=0x601)
        target_link_libraries(tool_tcprelay Ws2_32.lib)
    endif()

    if (NOT NO_READLINE)
        target_link_libraries(tool_tcprelay readline)
    endif()
endif()

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
