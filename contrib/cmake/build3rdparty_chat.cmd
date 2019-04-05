REM Use this method to build all the third party libraries when you are using MEGAchat libray with WebRTC
REM Normally this script is used outside of the git repo, so that 3rdParty_chat is created next to the projects

mkdir 3rdParty_chat
cd 3rdParty_chat

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

CALL :build_one zlib
CALL :build_one cryptopp
CALL :build_one c-ares
CALL :build_one sqlite3
CALL :build_one libevent
CALL :build_one libsodium
CALL :build_one freeimage
CALL :build_one ffmpeg
CALL :build_one gtest
CALL :build_one libuv

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


cd ..
git clone https://github.com/aisouard/libwebrtc
cd libwebrtc
mkdir build32debug
cd build32debug
c:\cmake\bin\cmake.exe -DCMAKE_CONFIGURATION_TYPES=Debug -DWEBRTC_BRANCH_HEAD="" -DWEBRTC_REVISION=c1a58bae4196651d2f7af183be1878bb00d45a57 ..
cd ..\..


git clone https://github.com/curl/curl.git
cd curl
git checkout curl-7_62_0
mkdir build32
cd build32
c:\cmake\bin\cmake -DCMAKE_TOOLCHAIN_FILE="C:\Users\MATTW\source\repos\3rdParty_chat\vcpkg\scripts\buildsystems\vcpkg.cmake" -DCARES_INCLUDE_DIR="C:\Users\MATTW\source\repos\3rdParty_chat\vcpkg\installed\x86-windows-static-uncheckediterators\include" -DBUILD_CURL_EXE=NO -DBUILD_SHARED_LIBS=NO -DBUILD_TESTING=NO -DCMAKE_USE_OPENSSL=YES -DCURL_STATIC_CRT=YES -DENABLE_ARES=YES -DENABLE_CURLDEBUG=YES -DENABLE_DEBUG=YES ..
cd ../..

git clone https://github.com/warmcat/libwebsockets.git
cd libwebsockets
git checkout v2.4.2
REM <apply patch>
mkdir build32
cd build32
c:\cmake\bin\cmake -DCMAKE_BULID_TYPE=Debug -DLWS_IPV6=OFF -DLIBUV_INCLUDE_DIRS=C:\Users\MATTW\source\repos\3rdParty_chat\vcpkg\installed\x86-windows-static-uncheckediterators\include -DLIBUV_LIBRARIES="C:\Users\MATTW\source\repos\3rdParty_chat\vcpkg\installed\x86-windows-static-uncheckediterators\lib\libuv.lib" -DOPENSSL_INCLUDE_DIRS="C:\Users\MATTW\source\repos\3rdParty_chat\libwebrtc\build32debug\webrtc\src\third_party\boringssl\src\include" -DLWS_WITHOUT_SERVER=ON LWS_WITHOUT_TESTAPPS=ON -DLWS_WITH_BORINGSSL=ON -DLWS_WITH_LIBUV=ON -DLWS_WITH_SHARED=OFF -DLWS_WITH_STATIC=ON -DOPENSSL_INCLUDE_DIR="C:\Users\MATTW\source\repos\3rdParty_chat\libwebrtc\build32debug\webrtc\src\third_party\boringssl\src\include"  ..
REM (manually switch msvc projects to /MT /MTd and build each)
cd ../..


exit /b 0

:build_one 
.\vcpkg.exe install --triplet x64-windows-static-uncheckediterators %1%
echo %errorlevel% %1% x64-windows-static-uncheckediterators >> buildlog
.\vcpkg.exe install --triplet x86-windows-static-uncheckediterators %1%
echo %errorlevel% %1% x86-windows-static-uncheckediterators >> buildlog
exit /b 0
