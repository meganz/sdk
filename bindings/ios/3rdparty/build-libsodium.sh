#!/bin/sh

LIBSODIUM_VERSION="1.0.11"

set -e

if [ ! -e "libsodium-${LIBSODIUM_VERSION}.tar.gz" ]
then
wget "https://github.com/jedisct1/libsodium/releases/download/${LIBSODIUM_VERSION}/libsodium-${LIBSODIUM_VERSION}.tar.gz"
fi

rm -rf libsodium-${LIBSODIUM_VERSION}
tar zxf libsodium-${LIBSODIUM_VERSION}.tar.gz
pushd "libsodium-${LIBSODIUM_VERSION}"

sh dist-build/ios.sh

cp -f libsodium-ios/lib/libsodium.a ../lib
cp -fR libsodium-ios/include/sodium* ../include
#make clean

popd

rm -rf libsodium-${LIBSODIUM_VERSION}
rm -rf libsodium-${LIBSODIUM_VERSION}.tar.gz


echo "Done."
