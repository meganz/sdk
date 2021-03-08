
REM Run this file from its current folder, with triplet as a parameter. 
REM It will set up and build 3rdParty in a folder next to the SDK repo
REM And also build the SDK against those third party libraries.
REM At the moment, you have to also supply the pdfium library code, eg by cut and paste after starting the script.
REM Or, first comment out pdfium in preferred-ports-sdk.txt.

REM You can run this script multiple times in the same repo, with different triplets.  For example,
REM (and, take into account that the script may need some small tweaks depending on your VS version etc)
REM    fullBuildFromScratchOnWindows.cmd x64-windows-mega
REM    fullBuildFromScratchOnWindows.cmd x86-windows-mega
REM    fullBuildFromScratchOnWindows.cmd x64-windows-mega-staticdev
REM    fullBuildFromScratchOnWindows.cmd x86-windows-mega-staticdev

REM if building for staticdev, you should also adjust CMakeLists (or CMakeCache afterward) with MEGA_LINK_DYNAMIC_CRT:STRING=0 , UNCHECKED_ITERATORS:STRING=1

set TRIPLET=%1
set SDK_DIR=%CD%\..\..

if "%TRIPLET%a" == "a" echo "Please supply a triplet string parameter"
if "%TRIPLET%a" == "a" goto ErrorHandler

REM Prepare 3rdParty folder and build the prep tool

if not exist %SDK_DIR%\..\3rdParty_sdk mkdir %SDK_DIR%\..\3rdParty_sdk
IF %ERRORLEVEL% NEQ 0 goto ErrorHandler

cd %SDK_DIR%\..\3rdParty_sdk
IF %ERRORLEVEL% NEQ 0 goto ErrorHandler

cmake %SDK_DIR%\contrib\cmake\build3rdParty
IF %ERRORLEVEL% NEQ 0 goto ErrorHandler

cmake --build . --config Release
IF %ERRORLEVEL% NEQ 0 goto ErrorHandler

REM use the prep tool to set up just our dependencies and no others

.\Release\build3rdParty.exe --setup --removeunusedports --nopkgconfig --ports "%SDK_DIR%\contrib\cmake\preferred-ports-sdk.txt" --triplet %TRIPLET% --sdkroot "%SDK_DIR%"
IF %ERRORLEVEL% NEQ 0 goto ErrorHandler

REM use the prep tool to invoke vcpkg to build all the dependencies

.\Release\build3rdParty.exe --build --ports "%SDK_DIR%\contrib\cmake\preferred-ports-sdk.txt" --triplet %TRIPLET% --sdkroot "%SDK_DIR%"
IF %ERRORLEVEL% NEQ 0 goto ErrorHandler

REM Now set up to build this repo

if not exist "%SDK_DIR%\%TRIPLET%" mkdir "%SDK_DIR%\%TRIPLET%"
IF %ERRORLEVEL% NEQ 0 goto ErrorHandler
cd "%SDK_DIR%\%TRIPLET%"
IF %ERRORLEVEL% NEQ 0 goto ErrorHandler

set x86orx64=x64
if "%TRIPLET%zz"=="x86-windows-megazz" set x86orx64=Win32
if "%TRIPLET%zz"=="x86-windows-mega-staticdevzz" set x86orx64=Win32

cmake -G "Visual Studio 16 2019" -A %x86orx64% -DMega3rdPartyDir="%SDK_DIR%\..\3rdParty_sdk" -DVCPKG_TRIPLET=%TRIPLET% -S "%SDK_DIR%\contrib\cmake" -B .
IF %ERRORLEVEL% NEQ 0 goto ErrorHandler

cmake --build . --config Debug
IF %ERRORLEVEL% NEQ 0 goto ErrorHandler

cmake --build . --config Release
IF %ERRORLEVEL% NEQ 0 goto ErrorHandler

ECHO ****************************** COMPLETE ********************************
exit /B %ERRORLEVEL%

:ErrorHandler
ECHO ****************************** ERROR %ERRORLEVEL% ********************************
exit /B %ERRORLEVEL%







