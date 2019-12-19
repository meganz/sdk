REM This script is for getting and building the 3rd party libraries that the MEGA SDK uses (some are optional, and some are only needed only by MEGA apps too)
REM 
REM Your 3rdParty library builds should be outside the SDK repo.  We are moving to use vcpkg to build most of them. You can start it like this:
REM
REM mkdir 3rdParty
REM cd 3rdParty
REM git clone https://github.com/Microsoft/vcpkg.git
REM cd vcpkg
REM .\bootstrap-vcpkg.bat
REM
REM For XP compatibility (which we had but we are deprecating), you can set up a vcpkg triplet to use VS2015 XP compatible toolset 
REM with ` set(VCPKG_PLATFORM_TOOLSET "v140") ` in your vcpkg triplet file.
REM
REM Another thing you might want to consider for developing, is turning off checked iterators in MSVC builds, since those extra tests can cause big delays
REM in debug (eg. deleting the node tree on logout etc).  You can do that by setting set(VCPKG_CXX_FLAGS "${VCPKG_CXX_FLAGS} -D_ITERATOR_DEBUG_LEVEL=0")
REM in your triplet file.  Another way is to edit the STL headers first, which can ensure that nested builds get the right flag.
REM 
REM Copy the folders in contrib/cmake/vcpkg_extra_ports to your 3rdParty/vcpkg/ports folder.
REM Comment out any libraries that you won't use.
REM Call this script from your vcpkg folder, with the desired triplet as the parameter.  (usually x64-windows-mega or x86-windows-mega)
REM If using the x64-windows-mega or x86-windows-mega triplets (which select static or DLL libraries as used in MEGAsync), copy those from the contrib/cmake folder

set TRIPLET=%1%

CALL :build_one zlib
CALL :build_one cryptopp
CALL :build_one c-ares
CALL :build_one sqlite3
CALL :build_one libevent
CALL :build_one libsodium
CALL :build_one ffmpeg
CALL :build_one gtest
CALL :build_one openssl
CALL :build_one curl
CALL :build_one libzen
CALL :build_one libmediainfo

REM freeimage is not needed for MEGASync (but might be for other projects)
REM CALL :build_one freeimage

REM MEGASync needs libuv
CALL :build_one libuv

REM MEGASync needs pdfium, and building it is quite tricky - we can build it statically though with its own CMakeLists.txt after getting the code per their instructions.  
REM It in turn depends on these libs which are easier to build with vcpkg as part of our compatible static library set than as part of its own third_party dependencies
CALL :build_one icu
CALL :build_one lcms
CALL :build_one libjpeg-turbo
CALL :build_one openjpeg

REM ------ building pdifum - this is mostly manual, sorry - to be done after vcpkg builds finish ---------------
REM - Set up your Depot Tools (this can be one time, reuse it for other builds etc)
REM      Follow these instructions to get the depot_tools (download .zip, extract all, set variable, run gclient): https://chromium.googlesource.com/chromium/src/+/master/docs/windows_build_instructions.md#install
REM - Then make a `pdfium` folder in your 3rdParty folder, and run these commands in it to get the pdfium source:
REM      set DEPOT_TOOLS=<<<<your depot_tools path>>>>
REM      set PATH=%DEPOT_TOOLS%;%PATH%
REM      set DEPOT_TOOLS_WIN_TOOLCHAIN=0
REM      mkdir pdfium
REM      cd pdfium
REM      gclient config --unmanaged https://pdfium.googlesource.com/pdfium.git
REM      gclient sync
REM - Copy the pdfium-CMakeLists.txt file from the SDK/contrib/cmake folder to your 3rdParty/pdfium/pdfium folder and rename as just CMakeLists.txt
REM - Make this one small patch:
REM      in pdfium\core\fxcrt\fx_memory_wrappers.h(26)   comment out the static_assert (uint8_t counts as an arithmentic type)
REM - In 3rdParty/pdfium/pdfium, run as required: 
REM      mkdir build_x86
REM      mkdir build_x64
REM      c:\cmake\bin\cmake.exe -B build_x86 -G "Visual Studio 15 2017"         -DCMAKE_CONFIGURATION_TYPES="Debug;Release" .
REM      c:\cmake\bin\cmake.exe -B build_x64 -G "Visual Studio 15 2017 Win64"   -DCMAKE_CONFIGURATION_TYPES="Debug;Release" .
REM      c:\cmake\bin\cmake.exe --build build_x86 --config Debug
REM      c:\cmake\bin\cmake.exe --build build_x86 --config Release
REM      c:\cmake\bin\cmake.exe --build build_x64 --config Debug
REM      c:\cmake\bin\cmake.exe --build build_x64 --config Release
REM - And then we also need to build its version of freetype:
REM      cd third_party\freetype\src
REM      mkdir build_x86
REM      mkdir build_x64
REM      c:\cmake\bin\cmake.exe -B build_x86 -G "Visual Studio 15 2017"         -DCMAKE_CONFIGURATION_TYPES="Debug;Release" .
REM      c:\cmake\bin\cmake.exe -B build_x64 -G "Visual Studio 15 2017 Win64"   -DCMAKE_CONFIGURATION_TYPES="Debug;Release" .
REM      c:\cmake\bin\cmake.exe --build build_x86 --config Debug
REM      c:\cmake\bin\cmake.exe --build build_x86 --config Release
REM      c:\cmake\bin\cmake.exe --build build_x64 --config Debug
REM      c:\cmake\bin\cmake.exe --build build_x64 --config Release

exit /b 0

:build_one 
.\vcpkg.exe install --triplet %TRIPLET% %1%
echo %errorlevel% %1% %TRIPLET% >> buildlog
exit /b 0
