: Variables to provide:
set CONFIGURATION=Release
set PDFium_BRANCH=chromium/3710
set PLATFORM=x86
set GITPATH=C:\Program Files (x86)\Microsoft Visual Studio\2017\Professional\Common7\IDE\CommonExtensions\Microsoft\TeamFoundation\Team Explorer\Git\cmd
set WINSDKVER=10.0.17763.0

: Input
set WindowsSDK_DIR=C:\Program Files (x86)\Windows Kits\10\bin\%WINSDKVER%\%PLATFORM%
set DepotTools_URL=https://storage.googleapis.com/chrome-infra/depot_tools.zip
set DepotTools_DIR=%CD%\depot_tools
set PDFium_URL=https://pdfium.googlesource.com/pdfium.git
set PDFium_SOURCE_DIR=%CD%\pdfium
set PDFium_BUILD_DIR=%PDFium_SOURCE_DIR%\out
set PDFium_PATCH_DIR=%CD%\patches
set PDFium_ARGS=%CD%\windows.args.gn

: Output
set PDFium_STAGING_DIR=%CD%\staging
set PDFium_INCLUDE_DIR=%PDFium_STAGING_DIR%\include
set PDFium_LIB_DIR=%PDFium_STAGING_DIR%\%PLATFORM%\lib
set PDFium_ARTIFACT=%CD%\pdfium-windows-%PLATFORM%.zip
if "%CONFIGURATION%"=="Debug" set PDFium_ARTIFACT=%CD%\pdfium-windows-%PLATFORM%-debug.zip

echo on

: Prepare directories
mkdir %PDFium_BUILD_DIR%
mkdir %PDFium_STAGING_DIR%
mkdir %PDFium_LIB_DIR%

: Download depot_tools
set PATH=%GITPATH%;%PATH%
call curl -fsSL -o depot_tools.zip %DepotTools_URL% || exit /b
if not exist %DepotTools_DIR% powershell Expand-Archive depot_tools.zip %DepotTools_DIR% || exit /b

set PATH=%DepotTools_DIR%;%WindowsSDK_DIR%;%PATH%
set DEPOT_TOOLS_WIN_TOOLCHAIN=0

: check that rc.exe is in PATH
where rc.exe || exit /b

:: Clone
call gclient config --unmanaged %PDFium_URL% || exit /b
call gclient sync || exit /b

:: Checkout branch (or ignore if it doesn't exist)
echo on
cd %PDFium_SOURCE_DIR%
git.exe checkout %PDFium_BRANCH% && call gclient sync

:: Patch
cd %PDFium_SOURCE_DIR%
copy "%PDFium_PATCH_DIR%\resources.rc" . || exit /b
git.exe apply -v "%PDFium_PATCH_DIR%\shared_library.patch" || exit /b
git.exe -C build apply -v "%PDFium_PATCH_DIR%\rc_compiler.patch" || exit /b

: Configure
cd %PDFium_SOURCE_DIR%
copy %PDFium_ARGS% %PDFium_BUILD_DIR%\args.gn
if "%CONFIGURATION%"=="Release" echo is_debug=false >> %PDFium_BUILD_DIR%\args.gn
if "%PLATFORM%"=="x86" echo target_cpu="x86" >> %PDFium_BUILD_DIR%\args.gn

: Generate Ninja files
call gn gen %PDFium_BUILD_DIR% || exit /b

: Build
:call ninja -C %PDFium_BUILD_DIR% pdfium -t clean || exit /b
call ninja -C %PDFium_BUILD_DIR% pdfium -j 5 || exit /b

: Install
echo on
copy %PDFium_SOURCE_DIR%\LICENSE %PDFium_STAGING_DIR% || exit /b
xcopy /S /Y %PDFium_SOURCE_DIR%\public %PDFium_INCLUDE_DIR%\ || exit /b
del %PDFium_INCLUDE_DIR%\DEPS
del %PDFium_INCLUDE_DIR%\README
del %PDFium_INCLUDE_DIR%\PRESUBMIT.py
move %PDFium_BUILD_DIR%\pdfium.dll.lib %PDFium_LIB_DIR%\pdfium.lib || exit /b
move %PDFium_BUILD_DIR%\pdfium.dll %PDFium_LIB_DIR% || exit /b
move %PDFium_BUILD_DIR%\pdfium.dll.pdb %PDFium_LIB_DIR%

: Pack
cd %PDFium_STAGING_DIR%
del %PDFium_ARTIFACT%
call powershell Compress-Archive * %PDFium_ARTIFACT%
