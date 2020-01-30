REM This script is for getting and building the 3rd party libraries that the MEGA SDK uses (some are optional, and some are only needed only by MEGA apps too)
REM 
REM Your 3rdParty library builds should be outside the SDK repo.  We are moving to use vcpkg to build most of them. You can start it like this:
REM
REM mkdir 3rdParty
REM cd 3rdParty
REM git clone https://github.com/Microsoft/vcpkg.git
REM cd vcpkg
REM .\bootstrap-vcpkg.bat -disableMetrics
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
REM
REM Once built, optionally capture just the headers and build products with:
REM vcpkg  export --triplet <YOUR_TRIPLET> --zip zlib cryptopp libsodium sqlite3 openssl c-ares curl libevent libzen libmediainfo ffmpeg gtest libuv libraw icu lcms libjpeg-turbo openjpeg libwebsockets pdfium pdfium-freetype


REM TODO: allow for -d -p -t parameters. See build3rdparty.bash
REM TODO: update docs (see build3rdparty.bash), and mention that vcpkg.exe has to be in PATH or in CWD

echo off

set DIR=%~dp0

Setlocal

set PORTS_FILE="%DIR%preferred-ports.txt"
set DEPS_FILE="%DIR%3rdparty_deps.txt"
set OVERLAYTRIPLETS=--overlay-triplets=%DIR%vcpkg_extra_triplets

echo %PORTS_FILE%

set TRIPLET=%1%

set "OVERLAYPORTS= "

Setlocal EnableDelayedExpansion
for /f "eol=# tokens=* delims=," %%l in ('type %PORTS_FILE%') do ^
set "OVERLAYPORTS=--overlay-ports=%DIR%vcpkg_extra_ports/%%l !OVERLAYPORTS!"

set VCPKG=vcpkg
WHERE vcpkg >nul
if %errorlevel% neq 0 set VCPKG=./vcpkg.exe


for /f "eol=# tokens=* delims=," %%l in ('type %DEPS_FILE%') do ^
call :build_one %%l


:build_one 
%VCPKG% install --triplet %TRIPLET% %1% % %  %%OVERLAYPORTS%% %%OVERLAYTRIPLETS%%
echo %errorlevel% %1% % % %%TRIPLET%% >> buildlog
if %errorlevel% neq 0 exit %errorlevel%
