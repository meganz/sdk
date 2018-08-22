REM best to use this script outside of the MEGA repo.  Once the libraries are built, the CMakeLists.txt can be adjusted to refer to them.
mkdir 3rdParty
cd 3rdParty

git clone https://github.com/Microsoft/vcpkg.git
cd vcpkg
CALL .\bootstrap-vcpkg.bat

REM we use a lot of map iterators, and the checking is linear, causing big delays in node deletion for example
copy .\triplets\x86-windows-static.cmake .\triplets\x86-windows-static-uncheckediterators.cmake
copy .\triplets\x64-windows-static.cmake .\triplets\x64-windows-static-uncheckediterators.cmake
echo #comment >> .\triplets\x86-windows-static-uncheckediterators.cmake
echo #comment >> .\triplets\x64-windows-static-uncheckediterators.cmake
echo set(VCPKG_CXX_FLAGS "${VCPKG_CXX_FLAGS} -D_ITERATOR_DEBUG_LEVEL=0") >> .\triplets\x86-windows-static-uncheckediterators.cmake
echo set(VCPKG_CXX_FLAGS "${VCPKG_CXX_FLAGS} -D_ITERATOR_DEBUG_LEVEL=0") >> .\triplets\x64-windows-static-uncheckediterators.cmake
echo set(VCPKG_C_FLAGS "${VCPKG_C_FLAGS} -D_ITERATOR_DEBUG_LEVEL=0") >> .\triplets\x86-windows-static-uncheckediterators.cmake
echo set(VCPKG_C_FLAGS "${VCPKG_C_FLAGS} -D_ITERATOR_DEBUG_LEVEL=0") >> .\triplets\x64-windows-static-uncheckediterators.cmake

REM for libsodium for x86, currently some adjustments are needed:
REM add this to its portfile.cmake's vcpkg_build_msbuild call (having moved that definition before it):  PLATFORM ${BUILD_ARCH}
REM also edit its vcproj file to set Configuration->AllOptions->TargetMachine to x86 for the Win32 configurations.
REM you can build everything first then make those adjustments and manually call vcpkg for libsodium x86.

CALL :build_one zlib
CALL :build_one cryptopp
CALL :build_one libsodium
CALL :build_one openssl
CALL :build_one curl
CALL :build_one c-ares
CALL :build_one sqlite3
CALL :build_one libevent
CALL :build_one gtest

REM for megachat:
REM CALL :build_one libuv
REM CALL :build_one libwebsockets

REM currently for libwebsockets a few changes are needed (mainly get 2.4.2, turn off ipv6, turn on libuv features)
REM modify these attributes in its portfile: 
REM     REF v2.4.2
REM     SHA512 
REM     7bee49f6763ff3ab7861fcda25af8d80f6757c56e197ea42be53e0b2480969eee73de3aee5198f5ff06fd1cb8ab2be4c6495243e83cd0acc235b0da83b2353d1
REM     HEAD_REF v2.4-stable
REM and add libuv dependency (this might not be strictly necessary)
REM     Feature: libuv
REM     Build-Depends: libuv
REM     Description: turns on LWS_WITH_LIBUU
REM then use remove and install again.
REM also find libwebsocket's CMakeLists.txt file and modify the libuv find_library, adding 'libuv' as that's what the libuv vcpkg produces:
REM     find_library(LIBUV_LIBRARIES NAMES uv libuv)



exit /b 0

:build_one 
.\vcpkg.exe install --triplet x64-windows-static-uncheckediterators %1%
echo %errorlevel% %1% x64-windows-static-uncheckediterators >> buildlog
.\vcpkg.exe install --triplet x86-windows-static-uncheckediterators %1%
echo %errorlevel% %1% x86-windows-static-uncheckediterators >> buildlog
exit /b 0
