echo off
SETLOCAL EnableExtensions

set DIR=%~dp0

set PORTS_FILE="%DIR%preferred-ports.txt"
set DEPS_FILE="%DIR%3rdparty_deps.txt"
set OVERLAYTRIPLETS=--overlay-triplets=%DIR%vcpkg_extra_triplets

:GETOPTS
 if /I "%1" == "-h" ( goto Help ) ^
 else (if /I "%1" == "-r" (set REMOVEBEFORE=true)^
 else (if /I "%1" == "-d" (set DEPS_FILE=%2 & shift )^
 else (if /I "%1" == "-p" (set PORTS_FILE=%2 & shift )^
 else (if /I "%1" == "-t" (set OVERLAYTRIPLETS=--overlay-triplets=%2 & shift )^
 else (if /I "%1" == "-o" (set OVERRIDEVCPKGPORTS=true)^
 else (^
 echo "%1"|>nul findstr /rx \"-.*\" && goto Help REM if parameter starts width - and has not been recognized, go to Help
 if "%1" neq "" (if "%TRIPLET%" == "" (set TRIPLET=%1% ))^
 else (goto :START)))))))
 
 shift
 goto GETOPTS
 
:START

if "%TRIPLET%" == ""  goto Help

set "OVERLAYPORTS= "
set VCPKG=vcpkg

IF "%OVERRIDEVCPKGPORTS%" == "true" (goto portsOverrided) else (goto portsOverlaid)
exit /b 0


:portsOverlaid
echo "Using overlaid ports from: %PORTS_FILE%" 
Setlocal

Setlocal EnableDelayedExpansion
for /f "eol=# tokens=* delims=," %%l in ('type %PORTS_FILE%') do ^
set "OVERLAYPORTS=--overlay-ports=%DIR%vcpkg_extra_ports/%%l !OVERLAYPORTS!"

WHERE vcpkg >nul || set VCPKG=./vcpkg.exe & 2>nul ren %%~dpi.\ports ports_moved

REM rename vcpkg's ports folder, to prevent using those ports
for /f %%i IN ('WHERE vcpkg') DO (
set vcpkgpath=%%~dpi
2>nul ren %%~dpi.\ports ports_moved
)
goto doBuild

:copyPort
echo %1 %2
set mydir=%1
for /D %%A in (%mydir%/..) do set PORTNAME=%%~nA
if not exist  %2\%PORTNAME%-OLD ren %2\%PORTNAME% %PORTNAME%-OLD
if exist %2\%PORTNAME% rmdir %2\%PORTNAME% /s /q
xcopy /E /F /R %1 %2\%PORTNAME%\
exit /b 0

:portsOverrided
echo "Overriding VCPKG ports with those in: %PORTS_FILE%" 

for /f %%i IN ('WHERE vcpkg') DO ( set vcpkgports=%%~dpi.\ports )
for /f "eol=# tokens=* delims=," %%l in ('type %PORTS_FILE%') do ( CALL :copyPort "%DIR%vcpkg_extra_ports\%%l" %vcpkgports%)
goto doBuild


:doBuild
for /f "eol=# tokens=* delims=," %%l in ('type %DEPS_FILE%') do ^
call :build_one %%l

exit /b 0

:build_one 
echo ports = %PORTS_FILE%
echo deps = %DEPS_FILE%
echo overtri = %OVERLAYTRIPLETS%

set what=%1%

if "%REMOVEBEFORE%"=="true" (
%VCPKG% remove %what% --triplet %TRIPLET% %OVERLAYPORTS% %OVERLAYTRIPLETS%
)

%VCPKG% install --triplet %TRIPLET% %what% %OVERLAYPORTS% %OVERLAYTRIPLETS%
echo %errorlevel% %1% % % %%TRIPLET%% >> buildlog
if %errorlevel% neq 0 (
    echo Failed: %VCPKG% install --triplet %TRIPLET% %what%  %OVERLAYPORTS% %OVERLAYTRIPLETS%
    exit %errorlevel%
)
exit /b 0

:help
    echo off
    echo.
    echo Usage:
    echo  $app [-d deps_file] [-p ports_file] [-t triplets_path] TRIPLET
    echo.
    echo This script is for getting and building the 3rd party libraries that the MEGA SDK uses (some are optional, and some are only needed only by MEGA apps too)
    echo.
    echo Your 3rdParty library builds should be outside the SDK repo.  We are moving to use vcpkg to build most of them. You can start it like this:
    echo.
    echo mkdir 3rdParty
    echo cd 3rdParty
    echo git clone https://github.com/Microsoft/vcpkg.git
    echo cd vcpkg
    echo .\bootstrap-vcpkg.bat -disableMetrics
    echo.
    
    echo Note that our current toolset for XP compatibility (which we had but we are deprecating), is VS2015 with v140 or v140_xp.
    echo  The triplet files ending -mega set v140, please adjust to your requirements.

    echo.
    echo If using pdfium, follow the instructions in 3rdparty_deps.txt to get the source code first before running this script
    echo.
    echo From your 3rdParty/vcpkg folder (or with vcpkg in your environment PATH), run this script (in its proper location) with the desired TRIPLET as the parameter.  (usually x64-windows-mega or x86-windows-mega)
    echo.
    echo Once built, optionally capture just the headers and build products with:
    echo  vcpkg  export --triplet <YOUR_TRIPLET> --zip zlib cryptopp libsodium sqlite3 openssl c-ares curl libevent libzen libmediainfo ffmpeg gtest libuv libraw icu lcms libjpeg-turbo openjpeg libwebsockets pdfium pdfium-freetype
    echo.
    echo Your packages will be installed in 3rdParty/vcpkg/installed
    echo.
    echo Options:
    echo  -d : path to file listing dependencies. By default %DIR%3rdparty_deps.txt. Comment out any libraries that you won't use.
    echo  -p : paths to ports file with dependencies/versions too look for. By default: %DIR%preferred-ports.txt
    echo  -t : overlay triplets path. By default %DIR%vcpkg_extra_triplets
    echo  -r : remove before install
    echo  -o : override vcpkg ports by the overlaid ports (will copy the previous ones into PORT-OLD)
    echo.

