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
REM Note that our current toolset for XP compatibility (which we had but we are deprecating), is VS2015 with v140 or v140_xp.  
REM The triplet files ending -mega set v140, please adjust to your requirements.
REM
REM Another thing you might want to consider for developing, is turning off checked iterators in MSVC builds, since those extra tests can cause big delays
REM in debug (eg. deleting the node tree on logout etc).  You can do that by setting set(VCPKG_CXX_FLAGS "${VCPKG_CXX_FLAGS} -D_ITERATOR_DEBUG_LEVEL=0")
REM in your triplet file, though every library used must have that same setting.
REM 
REM Another way is to edit the STL headers first, which can ensure that nested builds get the right flag.
REM 
REM Copy the folders in contrib/cmake/vcpkg_extra_ports to your 3rdParty/vcpkg/ports folder.
REM Copy the folders in contrib/cmake/vcpkg_extra_triplets to your 3rdParty/vcpkg/triplets folder.
REM 
REM Comment out any libraries that you won't use.
REM If using pdfium, follow the instructions below to get the source code
REM 
REM Copy this script to your 3rdParty/vcpkg folder, and run it with the desired triplet as the parameter.  (usually x64-windows-mega or x86-windows-mega)

set TRIPLET=%1%

CALL :build_one zlib
CALL :build_one cryptopp
CALL :build_one libsodium
CALL :build_one sqlite3
CALL :build_one openssl
CALL :build_one c-ares
CALL :build_one curl
CALL :build_one libevent
CALL :build_one libzen
CALL :build_one libmediainfo
CALL :build_one ffmpeg
CALL :build_one gtest

REM freeimage is not needed for MEGASync (but might be for other projects)
REM CALL :build_one freeimage

REM MEGASync needs libuv and libraw
CALL :build_one libuv
CALL :build_one libraw

REM MEGASync needs pdfium, and building it is quite tricky - we can build it statically though with its own CMakeLists.txt after getting the code per their instructions.  
REM It in turn depends on these libs which are easier to build with vcpkg as part of our compatible static library set than as part of its own third_party dependencies
CALL :build_one icu
CALL :build_one lcms
CALL :build_one libjpeg-turbo
CALL :build_one openjpeg

REM If building something that depends on MEGAchat you will also need libwebsockets:
CALL :build_one libwebsockets

REM ------ building pdifum - this one needs some manual steps - these can be done before calling the script ---------------
REM - Set up your Depot Tools (this can be one time, reuse it for other builds etc)
REM      Follow these instructions to get the depot_tools (download .zip, extract all, set variable, run gclient): https://chromium.googlesource.com/chromium/src/+/master/docs/windows_build_instructions.md#install
REM - Then in your 3rdParty/vcpkg folder, and run these commands in it to get the pdfium source:
REM      set DEPOT_TOOLS=<<<<your depot_tools path>>>>
REM      set PATH=%DEPOT_TOOLS%;%PATH%
REM      set DEPOT_TOOLS_WIN_TOOLCHAIN=0
REM      mkdir pdfium
REM      cd pdfium
REM      gclient config --unmanaged https://pdfium.googlesource.com/pdfium.git
REM      gclient sync
REM      REM branch 3710 is compatibile with the VS 2015 compiler and v140 toolset  (or if you want to use the latest, see below)
REM      git checkout chromium/3710
REM      gclient sync
REM - If using the latest Pdfium, use at least VS2017 and skip the branch checkout above, and substitute the pdfium-masterbranch-CMakeLists.txt in vcpkg/ports/pdfium and make this one small patch (other changes may be needed if the master branch has changed):
REM      in pdfium\core\fxcrt\fx_memory_wrappers.h(26)   comment out the static_assert (uint8_t counts as an arithmentic type)

CALL :build_one pdfium
CALL :build_one pdfium-freetype

exit /b 0

:build_one 
.\vcpkg.exe install --triplet %TRIPLET% %1%
echo %errorlevel% %1% %TRIPLET% >> buildlog
exit /b 0
