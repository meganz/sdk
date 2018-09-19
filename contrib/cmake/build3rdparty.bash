#!/bin/bash
# The usual way to use this file is to make a new folder 3rdParty next to your sdk folder, copy this script to it, and run it there.
# Then building via cmake can refer to these libraries.

git clone https://github.com/Microsoft/vcpkg.git
cd vcpkg
./bootstrap-vcpkg.sh
./vcpkg install --triplet x64-linux zlib
./vcpkg install --triplet x64-linux cryptopp
./vcpkg install --triplet x64-linux openssl
./vcpkg install --triplet x64-linux c-ares
./vcpkg install --triplet x64-linux curl    
./vcpkg install --triplet x64-linux sqlite3
./vcpkg install --triplet x64-linux libevent
./vcpkg install --triplet x64-linux gtest
#./vcpkg install --triplet x64-linux libsodium


