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

if (WIN32)
  cmake_minimum_required(VERSION 3.15)
  cmake_policy(SET CMP0091 NEW)   # for msvc dll/stsatic target
else()
  cmake_minimum_required(VERSION 3.13)
  set(CMAKE_EXPORT_COMPILE_COMMANDS ON) # Useful for make or ninja
  message( "CMAKE_EXPORT_COMPILE_COMMANDS ON")
endif()

project(MegaSDK)

set( CMAKE_EXPORT_COMPILE_COMMANDS ON) # Generates compile_commands.json with the compilers calls for all translations units
set (Mega3rdPartyDir "" CACHE FILEPATH "Prefix path for where third party dependencies are located")

if (CMAKE_HOST_APPLE AND NOT IOS)
    set (USE_OPENSSL 0 CACHE STRING "")
else()
    set (USE_OPENSSL 1 CACHE STRING "Usually Needed")
endif()

set (USE_ASIO 1 CACHE STRING "Only needed For tcprelay test tool")
set (USE_CURL 1 CACHE STRING "Always needed (turning it off would mean using WinHTTP instead but that has not been maintained for a while)")
set (USE_SQLITE 1 CACHE STRING "Needed unless intending a cache-less app")
set (USE_MEDIAINFO 1 CACHE STRING "Used to determine media properties and set those as node attributes")
set (USE_FREEIMAGE 1 CACHE STRING "Used to create previews/thumbnails for photos/pictures")
set (ENABLE_SYNC 1 CACHE STRING "Turns on sync functionality")
set (ENABLE_CHAT 0 CACHE STRING "Turns on chat management functionality")
set (ENABLE_LOG_PERFORMANCE 0 CACHE STRING "Faster log message generation")
set (HAVE_FFMPEG 1 CACHE STRING "Used to create previews/thumbnails for video files")
set (USE_WEBRTC 0 CACHE STRING "Declare that your app will link with WebRTC")
set (USE_LIBUV 0 CACHE STRING "Includes the library and turns on internal web and ftp server functionality")
set (USE_QT 0 CACHE STRING "Declare that your app will link wtih Qt")
set (USE_PDFIUM 1 CACHE STRING "Used to create previews/thumbnails for PDF files")
set (USE_LIBRAW 0 CACHE STRING "Just includes the library (used by MEGAsync)")
set (USE_PCRE 0 CACHE STRING "Can be used by client apps. The SDK does not use it itself anymore")
set (USE_DRIVE_NOTIFICATIONS 0 CACHE STRING "Allows to monitor (external) drives being [dis]connected to the computer")
set (MEGA_USE_C_ARES 1 CACHE STRING "If set, the SDK will manage DNS lookups and ipv4/ipv6 itself, using the c-ares library.  Otherwise we rely on cURL")
set (MEGA_QT_VERSION 5.12.11 CACHE STRING "Qt version installed in c:/Qt")

if (USE_PTHREAD)
    set( USE_CPPTHREAD 0)
else()
    set( USE_CPPTHREAD 1)
endif()

# Sodium is no longer optional. Setting the variable here to saisfy existing preprocessor checks and #cmakedefine
set (USE_SODIUM 1)

# Cryptop never was optional
set (USE_CRYPTOPP 1)

set (HAVE_LIBUV ${USE_LIBUV})
set (HAVE_LIBRAW ${USE_LIBRAW})

option(USE_THIRDPARTY_FROM_VCPKG
"Whether to look for third party dependencies in a vcpkg install. If yes, Mega3rdPartyDir must be a path to a directory containing the vcpkg directory"
${WIN32})

option(USE_QT_FROM_VCPKG
"Whether to look for QT dependencies in a vcpkg install. To set yes, USE_THIRDPARTY_FROM_VCPKG must be yes"
0)

if (NOT USE_THIRDPARTY_FROM_VCPKG AND USE_QT_FROM_VCPKG)
    message( WARNING "USE_THIRDPARTY_FROM_VCPKG is not set, turn off USE_QT_FROM_VCPKG" )
    set (USE_QT_FROM_VCPKG 0)
endif()

message(STATUS " Using 3rd party from vcpkg = ${USE_THIRDPARTY_FROM_VCPKG}")
message(STATUS " Using QT from vcpkg = ${USE_QT_FROM_VCPKG}")
message(STATUS " CMAKE_SYSTEM_NAME = ${CMAKE_SYSTEM_NAME}")

if(WIN32 OR IOS)
    set(NO_READLINE 1 CACHE STRING "")
else()
    set(NO_READLINE 0 CACHE STRING "")
endif()

if (WIN32)
    set (UNCHECKED_ITERATORS 0 CACHE STRING "")
    IF (USE_WEBRTC)
        IF ("${WebRtcDir}" STREQUAL "")
            SET (WebRtcDir "${Mega3rdPartyDir}/libwebrtc/build32debug")
        ENDIF()
    ENDIF()
endif()

if (USE_WEBRTC)
    if ("${WebRtcDir}" STREQUAL "")
        SET(WebRtcDir ${Mega3rdPartyDir}/libwebrtc/build32debug)
    endif()
endif()

set (MEGA_LINK_DYNAMIC_CRT 1 CACHE STRING "")

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
elseif(APPLE AND NOT IOS)
    set (build_64_bit 1)

    if (CMAKE_OSX_ARCHITECTURES MATCHES "arm64" OR (NOT CMAKE_OSX_ARCHITECTURES AND CMAKE_SYSTEM_PROCESSOR STREQUAL "arm64"))
        set(build_arm64 1)
        message("Building for macOS arm64 in a ${CMAKE_HOST_SYSTEM_PROCESSOR} host.")
    else()
        set(build_arm64 0)
        message("Building for macOS x86_64 in a ${CMAKE_HOST_SYSTEM_PROCESSOR} host.")
    endif()

else()
    set (build_64_bit 1 CACHE STRING "Build for a 64 bit target")
    message(STATUS "Building 64 bit")
endif()

if(APPLE AND NOT IOS)
    if(NOT CMAKE_CXX_COMPILER_VERSION VERSION_LESS "15.0")
        message("Building with ${CMAKE_CXX_COMPILER_ID} with version ${CMAKE_CXX_COMPILER_VERSION}")
        add_link_options("-Wl,-ld_classic")
    endif()
endif()


if(APPLE)
    set(CMAKE_CXX_STANDARD 11)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
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

# currently supported scenarios for getting 3rd party libraries:
#  - vcpkg built dependencies (there are some scripts in this folder to build those)
#  - libraries from the system, installed via eg. apt-get on linux

if (USE_THIRDPARTY_FROM_VCPKG) #vcpkg or system

    #determine VCPKG triplet
    if(NOT VCPKG_TRIPLET)
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
        ELSEIF(CMAKE_HOST_APPLE)
            if(NOT IOS)
                if(build_arm64)
                    set(VCPKG_TRIPLET "arm64-osx-mega" CACHE STRING "")
                else()
                    set(VCPKG_TRIPLET "x64-osx-mega" CACHE STRING "")
                endif()
            else()
                set(VCPKG_TRIPLET "arm64-ios" CACHE STRING "")
            endif()
        ELSE()
            set(VCPKG_TRIPLET "x64-linux" CACHE STRING "")
        ENDIF()
    endif()


    set(vcpkg_dir "${Mega3rdPartyDir}/vcpkg/installed/${VCPKG_TRIPLET}")
endif()

message(STATUS, " Mega3rdPartyDir = ${Mega3rdPartyDir}")
message(STATUS, " USE_THIRDPARTY_FROM_VCPKG = ${USE_THIRDPARTY_FROM_VCPKG}")
message(STATUS, " vcpkg_dir = ${vcpkg_dir}")

# Since we offer some Qt support in the SDK, set up those dependencies here
# setup QT_DIR, qt_cmake_dir, qt_qmake_dir
# USE_QT_FROM_VCPKG override USE_QT
if (USE_QT_FROM_VCPKG)
    set(QT_DIR "${vcpkg_dir}/tools/qt5" CACHE STRING "Specify your QT install folder if it is nonstandard")
    set(qt_cmake_dir "${vcpkg_dir}/share/cmake/Qt5")
    set(qt_qmake_dir "${vcpkg_dir}/tools/qt5/bin")
elseif (USE_QT)
    #For convenience, added some default qt paths if none is specified
    if (NOT DEFINED QT_DIR)
        if (CMAKE_HOST_WIN32 )
            if (build_64_bit)
                set (QT_DIR "C:/Qt/${MEGA_QT_VERSION}/msvc2017_64" CACHE STRING "Specify your QT install folder if it is nonstandard")
            else()
                set (QT_DIR "C:/Qt/${MEGA_QT_VERSION}/msvc2017" CACHE STRING "Specify your QT install folder if it is nonstandard")
            ENDIF()
        ELSE()
            set (QT_DIR "/Users/Shared/Qt/${MEGA_QT_VERSION}/clang_64" CACHE STRING "Specify your QT install folder if it is nonstandard")
        ENDIF()
    ENDIF()

    set(qt_cmake_dir "${QT_DIR}/lib/cmake/Qt5")
    set(qt_qmake_dir "${QT_DIR}/bin")
endif()

if (USE_QT OR USE_QT_FROM_VCPKG)
    message(STATUS "USE_QT: ${USE_QT} USE_QT_FROM_VCPKG: ${USE_QT_FROM_VCPKG}")
    message(STATUS "QT_DIR: ${QT_DIR}")

    # find qt package
    if (NOT DEFINED CMAKE_PREFIX_PATH)
        set(CMAKE_PREFIX_PATH "${qt_cmake_dir}")
    endif()

    message(STATUS "CMAKE_PREFIX_PATH (should contain the QT install path): ${CMAKE_PREFIX_PATH}")

    foreach (PKG IN LISTS MEGA_QT_REQUIRED_COMPONENTS)
        message(STATUS "Finding Qt package ${PKG}")
        find_package(Qt5 COMPONENTS ${PKG} REQUIRED)
    endforeach(PKG)

    # get qt version
    if ("${QT_QMAKE_EXECUTABLE}" STREQUAL "")
        if(WIN32)
            set(QT_QMAKE_EXECUTABLE "${qt_qmake_dir}/qmake.exe")
        else()
            set(QT_QMAKE_EXECUTABLE "${qt_qmake_dir}/qmake")
        endif()
    endif()

    execute_process(COMMAND ${QT_QMAKE_EXECUTABLE} -query QT_VERSION OUTPUT_VARIABLE QT_VERSION OUTPUT_STRIP_TRAILING_WHITESPACE)
    message(STATUS "Using QT ${QT_VERSION} from ${QT_QMAKE_EXECUTABLE}")
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
    if (UNCHECKED_ITERATORS)
        set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -D_ITERATOR_DEBUG_LEVEL=0" )
    endif()

    # warnings as errors, just for windows builds, as warnings keep creeping in.  Best if we address them once in the original MRs.
    if (build_64_bit)
        set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /WX" )
    endif()

    # accurate __cplusplus macro for vc++, selecting c++17 here for windows builds though the MEGA SDK library must build for older c++ standards also
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /Zc:__cplusplus /std:c++17")
    add_definitions( -DNOMINMAX )

    #Link against the static C/C++ libraries on windows, by default. Link with dynamic CRT if explicitly requested
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

    set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /MP")
    set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /MP")
    set (CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /Zi")
    set (CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} /Zi")


    # for inet_ntoa (which is available in XP)
    add_definitions( -D_WINSOCK_DEPRECATED_NO_WARNINGS )
ENDIF(WIN32)

include(CheckIncludeFile)
if (NOT WIN32)
    include(CheckFunctionExists)
    check_include_file(inttypes.h HAVE_INTTYPES_H)
    check_include_file(dirent.h HAVE_DIRENT_H)
    check_include_file(uv.h HAVE_LIBUV)
    check_function_exists(aio_write, HAVE_AIO_RT)
endif()

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
    if(WIN32)
        set(_target_lib "${vcpkg_dir}/lib/${win}.lib")
        set(_target_lib_dbg "${vcpkg_dir}/debug/lib/${winDbg}.lib")
    else()
        set(_target_lib "${vcpkg_dir}/lib/${lin}.a")
        set(_target_lib_dbg "${vcpkg_dir}/debug/lib/${linDbg}.a")
    endif()

    if (NOT (EXISTS ${_target_lib} AND EXISTS ${_target_lib_dbg}))
        if (WIN32)
            message(FATAL_ERROR "Could not find libraries for ${winDbg} ${win}")
        else()
            set(_target_lib_shared "${vcpkg_dir}/lib/${lin}${CMAKE_SHARED_LIBRARY_SUFFIX}")
            set(_target_lib_shared_dbg "${vcpkg_dir}/debug/lib/${linDbg}${CMAKE_SHARED_LIBRARY_SUFFIX}")
            if(NOT (EXISTS ${_target_lib_shared} AND EXISTS ${_target_lib_shared_dbg}))
                message(FATAL_ERROR "Could not find libraries for ${lin} ${linDbg}
                checked ${_target_lib} ${_target_lib_dbg}
                ${_target_lib_shared} ${_target_lib_shared_dbg}
                ")
            else()
                set(_target_lib ${_target_lib_shared})
                set(_target_lib_dbg ${_target_lib_shared_dbg})
            endif()
        endif()
    endif()

    add_library(${libName} UNKNOWN IMPORTED)

    set_target_properties(${libName}
        PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES ${vcpkg_dir}/include
            IMPORTED_LOCATION ${_target_lib}
            IMPORTED_LOCATION_DEBUG ${_target_lib_dbg}
    )
endfunction(ImportStdVcpkgLibrary)

function(ImportHeaderLibrary libName includeDir)
    add_library(${libName} INTERFACE IMPORTED)
    set_property(TARGET ${libName} PROPERTY INTERFACE_INCLUDE_DIRECTORIES ${includeDir})
endfunction(ImportHeaderLibrary)

if(NOT NO_READLINE)
    if (APPLE)
        ImportStdVcpkgLibrary(readline _ _ libreadline libreadline)
        ImportStdVcpkgLibrary(history _ _ libhistory libhistory)
        set(readline_LIBRARIES readline history)
    else()
        set(readline_LIBRARIES readline)
    endif()
endif()

if (USE_THIRDPARTY_FROM_VCPKG)
        IF(USE_CRYPTOPP)
            ImportStdVcpkgLibrary(cryptopp        cryptopp cryptopp libcryptopp libcryptopp)
        ENDIF(USE_CRYPTOPP)

        ImportStdVcpkgLibrary(sodium          libsodium libsodium libsodium libsodium)
        ImportStdVcpkgLibrary(z               zlibd zlib libz libz)
        ImportStdVcpkgLibrary(icuuc           icuucd icuuc libicuuc libicuuc)
        ImportStdVcpkgLibrary(icudata         icudtd icudt libicudata libicudata)

        IF(USE_CURL)
            IF(USE_WEBRTC)
                ImportStdVcpkgLibrary(curl         "${Mega3rdPartyDir}/curl/include" "${Mega3rdPartyDir}/curl/build32/lib/Debug/libcurl-d.lib" "${Mega3rdPartyDir}/curl/build32/lib/Release/libcurl.lib")
            ELSE()
                ImportStdVcpkgLibrary(curl        libcurl-d libcurl libcurl-d libcurl)
                #find_package(CURL CONFIG REQUIRED PATHS "${vcpkg_dir}/share/curl" NO_DEFAULT_PATH )
            ENDIF()
        ENDIF()
        IF(MEGA_USE_C_ARES)
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

        IF (NOT IOS)
	ImportStdVcpkgLibrary(gtest           gtestd gtest libgtestd libgtest)
        ImportStdVcpkgLibrary(gmock           gmockd gmock libgmockd libgmock)
	ENDIF()

        IF(USE_MEDIAINFO)
            IF(CMAKE_HOST_APPLE)
                set(DEBUG_SUFIX _debug)
            endif()
            ImportStdVcpkgLibrary(zen        zend zen libzen${DEBUG_SUFIX} libzen)
            ImportStdVcpkgLibrary(tinyxml2   tinyxml2 tinyxml2 libtinyxml2 libtinyxml2)
            ImportStdVcpkgLibrary(mediainfo  mediainfod mediainfo libmediainfo${DEBUG_SUFIX} libmediainfo)
        ENDIF(USE_MEDIAINFO)

        IF(USE_FREEIMAGE OR USE_PDFIUM)
            # both these libaries have these common dependencies
            ImportStdVcpkgLibrary(libpng      libpng16d        libpng16       libpng16d           libpng16)
            ImportStdVcpkgLibrary(libopenjpeg openjp2          openjp2        libopenjp2          libopenjp2         )
        endif()

        IF(USE_LIBRAW OR USE_FREEIMAGE OR USE_PDFIUM)
            ImportStdVcpkgLibrary(liblcms        lcms2            lcms2          liblcms2            liblcms2)
            ImportStdVcpkgLibrary(libturbojpeg   turbojpeg        turbojpeg      libturbojpeg        libturbojpeg       )
        ENDIF()

        IF(USE_LIBRAW OR USE_FREEIMAGE)
            # we might need libraw directly (eg MEGAsync) but also freeimage needs it
            ImportStdVcpkgLibrary(libraw         raw_rd           raw_r          libraw_r           libraw_r)

            # libraw needs jasper and lcms and libjpeg-turbo (all also part of freeimage)
            ImportStdVcpkgLibrary(libjasper      jasperd          jasper         libjasperd          libjasper)

            IF(USE_LIBRAW)
                SET(raw_deps libjasper libturbojpeg liblcms)

                IF(NOT CMAKE_HOST_APPLE AND NOT CMAKE_HOST_WIN32)
                    include(CheckCXXCompilerFlag)

                    check_cxx_compiler_flag(-fgomp HAS_FGOMP)
                    check_cxx_compiler_flag(-fopenmp HAS_FOPENMP)

                    set(Mega_PlatformSpecificLibs
                        ${Mega_PlatformSpecificLibs}
                        $<$<BOOL:${HAS_FGOMP}>:-fgomp>
                        $<$<BOOL:${HAS_FOPENMP}>:-fopenmp>)
                ENDIF()
            ENDIF(USE_LIBRAW)

        ENDIF()

        IF(USE_FREEIMAGE)
            ImportStdVcpkgLibrary(freeimage             FreeImaged       FreeImage      libFreeImaged       libFreeImage       )
            ImportStdVcpkgLibrary(freeimage_Iex         Iex-3_1_d        Iex-3_1        libIex-3_1_d        libIex-3_1         )
            ImportStdVcpkgLibrary(freeimage_OpenEXR     OpenEXR-3_1_d    OpenEXR-3_1    libOpenEXR-3_1_d    libOpenEXR-3_1     )
            ImportStdVcpkgLibrary(freeimage_IlmThread   IlmThread-3_1_d  IlmThread-3_1  libIlmThread-3_1_d  libIlmThread-3_1   )
            ImportStdVcpkgLibrary(freeimage_Imath       Imath-3_1_d      Imath-3_1      libImath-3_1_d      libImath-3_1       )
            ImportStdVcpkgLibrary(freeimage_jpeg        jpeg             jpeg           libjpeg             libjpeg            )
            ImportStdVcpkgLibrary(freeimage_jpegxr      jpegxrd          jpegxr         libjpegxrd          libjpegxr          )
            ImportStdVcpkgLibrary(freeimage_jxrglue     jxrglued         jxrglue        libjxrglued         libjxrglue         )
            ImportStdVcpkgLibrary(freeimage_lzma        lzma             lzma           liblzma             liblzma            )
            ImportStdVcpkgLibrary(freeimage_tiff        tiffd            tiff           libtiffd            libtiff            )
            ImportStdVcpkgLibrary(freeimage_webp        libwebp          libwebp        libwebp             libwebp            )
            ImportStdVcpkgLibrary(freeimage_webpdecoder libwebpdecoder   libwebpdecoder libwebpdecoder      libwebpdecoder     )
            ImportStdVcpkgLibrary(freeimage_webpdemux   libwebpdemux     libwebpdemux   libwebpdemux        libwebpdemux       )
            ImportStdVcpkgLibrary(freeimage_webpmux     libwebpmux       libwebpmux     libwebpmux          libwebpmux         )
            ImportStdVcpkgLibrary(freeimage_sharpyuv    libsharpyuv      libsharpyuv    libsharpyuv         libsharpyuv        )

            target_link_libraries(freeimage INTERFACE
                                            freeimage_Iex
                                            freeimage_OpenEXR
                                            freeimage_IlmThread
                                            libpng
                                            libraw
                                            liblcms
                                            freeimage_tiff
                                            freeimage_webp
                                            freeimage_webpdecoder
                                            freeimage_webpdemux
                                            freeimage_webpmux
                                            freeimage_sharpyuv
                                            freeimage_jpeg
                                            freeimage_Imath
                                            freeimage_lzma
                                            libjasper
                                            libopenjpeg
                                            libturbojpeg
                                            freeimage_jxrglue
                                            freeimage_jpegxr)
            if(NOT IOS)
                #ImportStdVcpkgLibrary(freeimage_tiffxx        tiffxxd            tiffxx           libtiffxxd            libtiffxx            )
                #target_link_libraries(freeimage INTERFACE freeimage_tiffxx)
            endif()

            IF(NOT CMAKE_HOST_APPLE AND NOT CMAKE_HOST_WIN32)
                set(Mega_PlatformSpecificLibs ${Mega_PlatformSpecificLibs} -fopenmp )
            ENDIF()

            # To be included in the config.h file if available.
            file(TO_CMAKE_PATH "${vcpkg_dir}/include/FreeImageConfig.h" FREEIMAGECONFIG_PATH)
            CHECK_INCLUDE_FILE(${FREEIMAGECONFIG_PATH} HAVE_FREEIMAGECONFIG_H)

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
            ImportStdVcpkgLibrary(uv       uv_a uv_a libuv libuv)
        ENDIF(USE_LIBUV)

        IF(USE_PCRE)
            ImportStdVcpkgLibrary(pcre     pcred pcre libpcre libpcre)
            ImportStdVcpkgLibrary(pcrecpp  pcrecppd pcrecpp libpcrecpp libpcrecpp)
            target_link_libraries(pcrecpp  INTERFACE pcre)
        ENDIF(USE_PCRE)

        IF(USE_PDFIUM)
            ImportStdVcpkgLibrary(pdfium       pdfium pdfium libpdfium libpdfium)

            #liblcms and libturbojpeg already added above.  also libopenjpeg
            ImportStdVcpkgLibrary(freetype     freetyped freetype libfreetyped libfreetype)

            # pdfium will use bzip2 if present
            ImportStdVcpkgLibrary(bzip2       bz2d bz2 libbz2d libbz2)

            set(pdfium_deps ${pdfium_deps} freetype liblcms libturbojpeg libopenjpeg bzip2 libpng)
        ENDIF(USE_PDFIUM)

endif(USE_THIRDPARTY_FROM_VCPKG)

#we must compile with UNICODE defined or our link symbols won't match libmediainfo
# Migration from autotools note: there was a check that determined if libmediainfo was compiled with UNICODE.
set(UNICODE 1)

if(WIN32)

    add_definitions(-D_CRT_SECURE_NO_WARNINGS -DCURL_STATICLIB -DCARES_STATICLIB -DWIN32_LEAN_AND_MEAN -DSODIUM_STATIC -D_CONSOLE )
    IF(USE_PCRE)
        add_definitions(-DPCRE_STATICWIN32 )
    ENDIF(USE_PCRE)
    SET(Mega_PlatformSpecificIncludes ${MegaDir}/include/mega/$<IF:${USE_CURL},wincurl,win32>)
    SET(Mega_PlatformSpecificLibs ${Mega_PlatformSpecificLibs} ws2_32 winhttp Shlwapi Secur32.lib  $<$<OR:${USE_CURL},${USE_WEBRTC}>:Wldap32.lib> )
    IF(USE_LIBUV)
        SET(Mega_PlatformSpecificLibs ${Mega_PlatformSpecificLibs} Kernel32.lib Iphlpapi.lib Userenv.lib Psapi.lib DbgHelp.lib )
    ENDIF(USE_LIBUV)
    IF(HAVE_FFMPEG)
        SET(Mega_PlatformSpecificLibs ${Mega_PlatformSpecificLibs} Mfplat.lib mfuuid.LIB strmiids.LIB)
    ENDIF(HAVE_FFMPEG)
    IF(USE_DRIVE_NOTIFICATIONS)
        SET(Mega_PlatformSpecificLibs ${Mega_PlatformSpecificLibs} wbemuuid)
    ENDIF(USE_DRIVE_NOTIFICATIONS)

    SET(Mega_PlatformSpecificFiles ${MegaDir}/src/win32/console.cpp
    ${MegaDir}/src/win32/consolewaiter.cpp
    ${MegaDir}/src/win32/fs.cpp
    $<IF:${USE_CURL},${MegaDir}/src/posix/net.cpp,${MegaDir}/src/win32/net.cpp>
    ${MegaDir}/src/win32/waiter.cpp
    $<${USE_CPPTHREAD}:${MegaDir}/src/thread/cppthread.cpp>
    )
    IF(USE_DRIVE_NOTIFICATIONS)
        SET(Mega_PlatformSpecificFiles ${Mega_PlatformSpecificFiles} ${MegaDir}/include/mega/win32/drivenotifywin.h ${MegaDir}/src/win32/drivenotifywin.cpp)
    ENDIF(USE_DRIVE_NOTIFICATIONS)

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
        SET(Mega_PlatformSpecificFiles ${Mega_PlatformSpecificFiles} ${MegaDir}/src/osx/osxutils.mm)

        # if building all-static megacli for mac, you will need these (uncomment to enable)
        #SET(Mega_PlatformSpecificLibs ${Mega_PlatformSpecificLibs} "-framework LDAP")
        #SET(Mega_PlatformSpecificLibs ${Mega_PlatformSpecificLibs} "libresolv.dylib")

        SET(Mega_PlatformSpecificFiles ${Mega_PlatformSpecificFiles}
                                       ${MegaDir}/include/mega/osx/megafs.h
                                       ${MegaDir}/src/osx/fs.cpp)

        SET(Mega_PlatformSpecificLibs ${Mega_PlatformSpecificLibs}
                                       "-framework CoreServices")
    ENDIF()

    IF (APPLE)
        set(Mega_PlatformSpecificLibs ${Mega_PlatformSpecificLibs}
                                      "-framework CoreFoundation"
                                      "-framework DiskArbitration")

        IF (USE_DRIVE_NOTIFICATIONS)
            SET(Mega_PlatformSpecificFiles ${Mega_PlatformSpecificFiles}
                                           ${MegaDir}/include/mega/osx/drivenotifyosx.h
                                           ${MegaDir}/src/osx/drivenotifyosx.cpp)
        ENDIF()
    ENDIF ()

    IF (CMAKE_HOST_SYSTEM_NAME MATCHES "Linux")
        IF (USE_DRIVE_NOTIFICATIONS)
            SET(Mega_PlatformSpecificFiles ${Mega_PlatformSpecificFiles}
                                           ${MegaDir}/include/mega/posix/drivenotifyposix.h
                                           ${MegaDir}/src/posix/drivenotifyposix.cpp)
        ENDIF ()
    ENDIF ()

    SET(Mega_PlatformSpecificIncludes ${MegaDir}/include/mega/posix)

    SET(Mega_PlatformSpecificLibs ${Mega_PlatformSpecificLibs} pthread z dl termcap)
    IF(APPLE)
       SET(Mega_PlatformSpecificLibs ${Mega_PlatformSpecificLibs} "-framework SystemConfiguration -framework Security")

       SET(Mega_PlatformSpecificIncludes ${MegaDir}/include/mega/osx
                                         ${Mega_PlatformSpecificIncludes})

       if (NOT IOS)
           set(Mega_PlatformSpecificLibs ${Mega_PlatformSpecificLibs} "-framework Cocoa")
       else()
               set(Mega_PlatformSpecificLibs ${Mega_PlatformSpecificLibs}
                   "-framework Foundation"
                   "-framework AVFoundation"
                   "-framework ImageIO"
                   "-framework CoreGraphics"
                   "-framework CoreMedia"
                   "-framework MobileCoreServices"
                   "-framework UIKit"
                   resolv)
       endif()
    ELSE()
        SET(Mega_PlatformSpecificLibs ${Mega_PlatformSpecificLibs} crypto rt stdc++fs)
    ENDIF()

    IF (CMAKE_HOST_SYSTEM_NAME MATCHES "Linux")
        IF (USE_DRIVE_NOTIFICATIONS)
            SET(Mega_PlatformSpecificLibs ${Mega_PlatformSpecificLibs} udev)
        ENDIF ()
    ENDIF ()

    IF(USE_WEBRTC)
        add_definitions( -DWEBRTC_POSIX )
    ENDIF()

ENDIF(WIN32)

configure_file ("${MegaDir}/contrib/cmake/config.h.in" "${MegaDir}/include/mega/config.h" )
configure_file ("${MegaDir}/contrib/cmake/config.h.in" "${MegaDir}/include/config.h" )  # MEGAsync moc files also want it at mega/include/config.h
add_definitions( -DHAVE_CONFIG_H) #otherwise, it won't be included in Windows build!

SET(Mega_CryptoFiles ${MegaDir}/src/crypto/cryptopp.cpp ${MegaDir}/src/crypto/sodium.cpp)
SET(Mega_DbFiles ${MegaDir}/src/db/sqlite.cpp ${MegaDir}/src/db/sqlite.cpp )
SET(Mega_GfxFiles ${MegaDir}/src/gfx/external.cpp ${MegaDir}/src/gfx/freeimage.cpp ${MegaDir}/src/gfx/gfx_pdfium.cpp)

add_library(Mega STATIC
            ${MegaDir}/include/megaapi.h
            ${MegaDir}/include/megaapi_impl.h
            ${MegaDir}/include/mega/osx/osxutils.h
            ${MegaDir}/include/mega/transferslot.h
            ${MegaDir}/include/mega/thread/cppthread.h
            ${MegaDir}/include/mega/thread/posixthread.h
            ${MegaDir}/include/mega/thread/libuvthread.h
            ${MegaDir}/include/mega/process.h
            ${MegaDir}/include/mega/command.h
            ${MegaDir}/include/mega/config.h
            ${MegaDir}/include/mega/thread.h
            ${MegaDir}/include/mega/json.h
            ${MegaDir}/include/mega/base64.h
            ${MegaDir}/include/mega/mega_utf8proc.h
            ${MegaDir}/include/mega/gfx.h
            ${MegaDir}/include/mega/proxy.h
            ${MegaDir}/include/mega/crypto/sodium.h
            ${MegaDir}/include/mega/crypto/cryptopp.h
            ${MegaDir}/include/mega/http.h
            ${MegaDir}/include/mega/useralerts.h
            ${MegaDir}/include/mega/pendingcontactrequest.h
            ${MegaDir}/include/mega/textchat.h
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
            ${MegaDir}/include/mega/nodemanager.h
            ${MegaDir}/include/mega/setandelement.h
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
            ${MegaDir}/include/mega/gfx/freeimage.h
            ${MegaDir}/include/mega/gfx/gfx_pdfium.h
            ${MegaDir}/include/mega/gfx/external.h
            ${MegaDir}/include/mega/pubkeyaction.h
            ${MegaDir}/include/mega/mega_http_parser.h
            ${MegaDir}/include/mega/waiter.h
            ${MegaDir}/include/mega/db/sqlite.h
            ${MegaDir}/include/mega/types.h
            ${MegaDir}/include/mega/filefingerprint.h
            ${MegaDir}/include/mega/filesystem.h
            ${MegaDir}/include/mega/backofftimer.h
            ${MegaDir}/include/mega/raid.h
            ${MegaDir}/include/mega/logging.h
            ${MegaDir}/include/mega/file.h
            ${MegaDir}/include/mega/sync.h
            ${MegaDir}/include/mega/heartbeats.h
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
            ${MegaDir}/include/mega/drivenotify.h
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
            ${MegaDir}/src/textchat.cpp
            ${MegaDir}/src/process.cpp
            ${MegaDir}/src/proxy.cpp
            ${MegaDir}/src/pubkeyaction.cpp
            ${MegaDir}/src/raid.cpp
            ${MegaDir}/src/request.cpp
            ${MegaDir}/src/serialize64.cpp
            ${MegaDir}/src/nodemanager.cpp
            ${MegaDir}/src/setandelement.cpp
            ${MegaDir}/src/share.cpp
            ${MegaDir}/src/sharenodekeys.cpp
            ${MegaDir}/src/sync.cpp
            ${MegaDir}/src/heartbeats.cpp
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
            $<${USE_DRIVE_NOTIFICATIONS}:${MegaDir}/src/drivenotify.cpp>
            $<$<PLATFORM_ID:iOS>:${MegaDir}/src/gfx/GfxProcCG.mm>
            )

target_include_directories(Mega PRIVATE ${MegaDir}/include ${Mega_PlatformSpecificIncludes})
target_include_directories(Mega PUBLIC ${MegaDir}/include ${Mega_PlatformSpecificIncludes})

MESSAGE(STATUS "ID: ${Mega_PlatformSpecificIncludes}")

if (WIN32)
    set(Mega_PlatformSpecificLibs ${Mega_PlatformSpecificLibs} $<${HAVE_FFMPEG}:Mfplat.lib> $<${HAVE_FFMPEG}:Strmiids.lib> $<${HAVE_FFMPEG}:Mfuuid.lib>)
endif()

if (CMAKE_HOST_APPLE)
    set(Mega_PlatformSpecificLibs ${Mega_PlatformSpecificLibs} $<${HAVE_FFMPEG}:-liconv>)
endif()

foreach (PKG IN LISTS MEGA_QT_LINK_LIBRARIES)
    target_link_libraries(Mega PUBLIC ${PKG} )
endforeach(PKG)

target_link_libraries(Mega PUBLIC
                        $<${USE_CRYPTOPP}:cryptopp>
                        sodium
                        $<${USE_WEBRTC}:webrtc>
                        $<${USE_MEDIAINFO}:mediainfo> $<${USE_MEDIAINFO}:zen> $<${USE_MEDIAINFO}:tinyxml2>
                        $<${USE_CURL}:curl>
                        $<${MEGA_USE_C_ARES}:cares>
                        $<$<AND:${USE_OPENSSL},$<NOT:${USE_WEBRTC}>>:ssl>
                        $<$<AND:${USE_OPENSSL},$<NOT:${USE_WEBRTC}>>:crypto>
                        $<${USE_SQLITE}:sqlite3>
                        $<${USE_LIBUV}:uv>
                        $<${USE_PCRE}:pcrecpp>
                        $<${USE_LIBRAW}:libraw> $<${USE_LIBRAW}:${raw_deps}>
                        $<${USE_FREEIMAGE}:freeimage>
                        $<${USE_PDFIUM}:pdfium> $<${USE_PDFIUM}:${pdfium_deps}>
                        $<${HAVE_FFMPEG}:avfilter> $<${HAVE_FFMPEG}:avdevice> $<${HAVE_FFMPEG}:avformat> $<${HAVE_FFMPEG}:avcodec> $<${HAVE_FFMPEG}:avutil> $<${HAVE_FFMPEG}:swscale>  $<${HAVE_FFMPEG}:swresample>
                        z
                        icuuc icudata
                        ${Mega_PlatformSpecificLibs})

target_compile_definitions(Mega PUBLIC
                $<$<PLATFORM_ID:iOS>:USE_IOS>
                $<${USE_MEDIAINFO}:USE_MEDIAINFO>
                $<${USE_SQLITE}:USE_SQLITE>
                $<${USE_CRYPTOPP}:USE_CRYPTOPP>
                $<${USE_OPENSSL}:USE_OPENSSL>
                $<${USE_CURL}:USE_CURL>
                $<${MEGA_USE_C_ARES}:MEGA_USE_C_ARES>
                USE_SODIUM
                $<${ENABLE_SYNC}:ENABLE_SYNC>
                $<${ENABLE_CHAT}:ENABLE_CHAT>
                $<${ENABLE_LOG_PERFORMANCE}:ENABLE_LOG_PERFORMANCE>
                $<${NO_READLINE}:NO_READLINE>
                $<${USE_FREEIMAGE}:USE_FREEIMAGE>
                $<${HAVE_FFMPEG}:HAVE_FFMPEG>
                $<${HAVE_LIBUV}:HAVE_LIBUV>
                $<${USE_CPPTHREAD}:USE_CPPTHREAD>
                $<${USE_QT}:USE_QT>
                $<${USE_PCRE}:USE_PCRE>
                $<${USE_PDFIUM}:HAVE_PDFIUM>
                $<${USE_DRIVE_NOTIFICATIONS}:USE_DRIVE_NOTIFICATIONS>)

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

if(IOS)
    # build with some of these settings as required
    # -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_ARCHITECTURES="arm64" -DCMAKE_Swift_COMPILER_TARGET=arm64-apple-ios7.0.0 -DCMAKE_OSX_SYSROOT=iphoneos -DVCPKG_TRIPLET=arm64-ios-mega
    add_executable(simple_ios_build_test
        ${MegaDir}/bindings/ios/main.mm # stub main to make the executable link
        ${MegaDir}/bindings/ios/MEGAAccountDetails.mm
        ${MegaDir}/bindings/ios/MEGAAchievementsDetails.mm
        ${MegaDir}/bindings/ios/MEGABackgroundMediaUpload.mm
        ${MegaDir}/bindings/ios/MEGABanner.mm
        ${MegaDir}/bindings/ios/MEGABannerList.mm
        ${MegaDir}/bindings/ios/MEGACancelToken.mm
        ${MegaDir}/bindings/ios/MEGAChildrenLists.mm
        ${MegaDir}/bindings/ios/MEGAContactRequest.mm
        ${MegaDir}/bindings/ios/MEGAContactRequestList.mm
    ${MegaDir}/bindings/ios/MEGACurrency.mm
        ${MegaDir}/bindings/ios/MEGAError.mm
        ${MegaDir}/bindings/ios/MEGAEvent.mm
        ${MegaDir}/bindings/ios/MEGAFolderInfo.mm
        ${MegaDir}/bindings/ios/MEGAHandleList.mm
        ${MegaDir}/bindings/ios/MEGANode.mm
        ${MegaDir}/bindings/ios/MEGANodeList.mm
        ${MegaDir}/bindings/ios/MEGAPricing.mm
        ${MegaDir}/bindings/ios/MEGAPushNotificationSettings.mm
        ${MegaDir}/bindings/ios/MEGARecentActionBucket.mm
        ${MegaDir}/bindings/ios/MEGARequest.mm
        ${MegaDir}/bindings/ios/MEGASdk.mm
        ${MegaDir}/bindings/ios/MEGAShare.mm
        ${MegaDir}/bindings/ios/MEGAShareList.mm
        ${MegaDir}/bindings/ios/MEGAStringList.mm
        ${MegaDir}/bindings/ios/MEGATimeZoneDetails.mm
        ${MegaDir}/bindings/ios/MEGATransfer.mm
        ${MegaDir}/bindings/ios/MEGATransferList.mm
        ${MegaDir}/bindings/ios/MEGAUser.mm
        ${MegaDir}/bindings/ios/MEGAUserAlert.mm
        ${MegaDir}/bindings/ios/MEGAUserAlertList.mm
        ${MegaDir}/bindings/ios/MEGAUserList.mm
        ${MegaDir}/bindings/ios/Private/DelegateMEGAGlobalListener.mm
        ${MegaDir}/bindings/ios/Private/DelegateMEGAListener.mm
        ${MegaDir}/bindings/ios/Private/DelegateMEGALoggerListener.mm
        ${MegaDir}/bindings/ios/Private/DelegateMEGARequestListener.mm
        ${MegaDir}/bindings/ios/Private/DelegateMEGATransferListener.mm
        ${MegaDir}/bindings/ios/Private/DelegateMEGATreeProcessorListener.mm
        ${MegaDir}/bindings/ios/Private/ListenerDispatch.mm
        ${MegaDir}/bindings/ios/Private/MEGADataInputStream.mm
        ${MegaDir}/bindings/ios/Private/MEGAFileInputStream.mm
    )
    target_link_libraries(simple_ios_build_test Mega )
    target_include_directories(simple_ios_build_test
        PUBLIC
            ${MegaDir}/bindings/ios/
            ${MegaDir}/bindings/ios/Private
    )
endif()

if (NOT IOS)
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

target_compile_definitions(test_unit PRIVATE _SILENCE_TR1_NAMESPACE_DEPRECATION_WARNING)
target_compile_definitions(test_integration PRIVATE _SILENCE_TR1_NAMESPACE_DEPRECATION_WARNING)
target_link_libraries(test_unit gmock gtest Mega )
target_link_libraries(test_integration gmock gtest Mega )
if(APPLE)
    target_link_libraries(test_integration "-framework Security" )
endif()

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
    set_property(
        TARGET tool_tcprelay
        PROPERTY EXCLUDE_FROM_ALL 1
    )
    target_include_directories(tool_tcprelay PUBLIC "${vcpkg_dir}/installed/include")
    target_compile_definitions(tool_tcprelay PUBLIC -DASIO_STANDALONE)
    target_link_libraries(tool_tcprelay Mega)
    target_compile_features(tool_tcprelay PUBLIC cxx_std_14)

    if (WIN32)
        target_compile_definitions(tool_tcprelay PUBLIC -D_WIN32_WINNT=0x601)
        target_link_libraries(tool_tcprelay Ws2_32.lib)
    endif()

    if (NOT NO_READLINE)
        target_link_libraries(tool_tcprelay ${readline_LIBRARIES})
    endif()
endif()

#Integration tests require the following files to work
file(GLOB TESTING_AUX_FILES "${MegaDir}/tests/integration/test-data/*")
add_custom_command(
    TARGET test_integration POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy ${TESTING_AUX_FILES} $<TARGET_FILE_DIR:test_integration>
    COMMENT "Copying test files for integration tests."
)

# actual apps

add_executable(megacli ${MegaDir}/examples/megacli.cpp)
target_link_libraries(megacli Mega )
if (NOT NO_READLINE)
    target_link_libraries(megacli ${readline_LIBRARIES})
endif (NOT NO_READLINE)

endif()

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

    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /bigobj")  # Sync_test.cpp : fatal error C1128: number of sections exceeded object file format limit: compile with /bigobj

    set_property(TARGET Mega PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>$<${MEGA_LINK_DYNAMIC_CRT}:DLL>")
    set_property(TARGET megacli PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>$<${MEGA_LINK_DYNAMIC_CRT}:DLL>")
    set_property(TARGET testmega PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>$<${MEGA_LINK_DYNAMIC_CRT}:DLL>")
    set_property(TARGET test_integration PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>$<${MEGA_LINK_DYNAMIC_CRT}:DLL>")
    set_property(TARGET test_unit PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>$<${MEGA_LINK_DYNAMIC_CRT}:DLL>")
    if (HAVE_ASIO)
        set_property(TARGET tool_tcprelay PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>$<${MEGA_LINK_DYNAMIC_CRT}:DLL>")
    endif()

else()
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -ggdb3")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -Wall -Wextra -Wconversion -Wno-unused-parameter -Wno-unqualified-std-cast-call -Wno-deprecated-declarations -Wno-unqualified-std-cast-call")  #no-deprecated-declarations because of FSEventStreamScheduleWithRunLoop

    if (ENABLE_WERROR)
        if (CMAKE_BUILD_TYPE STREQUAL "Debug" )
            set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Werror")
            # Warnings which should not be promoted to errors, but still appear as warnings
            set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-error=deprecated-declarations")
            if (APPLE)
              set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-sign-conversion -Wno-overloaded-virtual -Wno-inconsistent-missing-override -Wno-unused-variable -Wno-unused-private-field -Wno-string-conversion -Wno-unused-lambda-capture -Wno-implicit-int-conversion -Wno-shorten-64-to-32 -Wno-unused-value")
            endif()
        endif()
        message(STATUS, "CMAKE_CXX_FLAGS: ${CMAKE_CXX_FLAGS}")
        message(STATUS, "CMAKE_CXX_FLAGS_DEBUG: ${CMAKE_CXX_FLAGS_DEBUG}")
    endif(ENABLE_WERROR)
endif()
