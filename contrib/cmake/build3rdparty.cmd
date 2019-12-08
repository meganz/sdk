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
REM Call this script from your vcpkg folder, with the desired triplet as the parameter.  (usually x64-windows or x86-windows)

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

REM freeimage is not needed for MEGASync
CALL :build_one freeimage

REM MEGASync needs libuv
CALL :build_one libuv

exit /b 0

:build_one 
.\vcpkg.exe install --triplet %TRIPLET% %1%
echo %errorlevel% %1% %TRIPLET% >> buildlog
exit /b 0
