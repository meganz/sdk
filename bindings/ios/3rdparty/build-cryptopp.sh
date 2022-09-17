#!/bin/sh

CURRENTPATH=`pwd`

CRYPTOPP_VERSION="982655845a784a9a4cfbc92221359a25a74184a3"

set -e

NPROCESSORS=$(getconf NPROCESSORS_ONLN 2>/dev/null || getconf _NPROCESSORS_ONLN 2>/dev/null)

if [ ! -e "${CRYPTOPP_VERSION}.tar.gz" ]
then
curl -LO "https://github.com/weidai11/cryptopp/archive/${CRYPTOPP_VERSION}.tar.gz"
fi

ARCHS="x86_64 arm64"

for ARCH in ${ARCHS}
do
tar zxf ${CRYPTOPP_VERSION}.tar.gz
pushd cryptopp-${CRYPTOPP_VERSION}
if [ $ARCH = "x86_64" ]; then
sed -i '' $'204s/IOS_FLAGS=\"\$IOS_FLAGS -DCRYPTOPP_DISABLE_ASM\"/IOS_FLAGS=\"\$IOS_FLAGS -DCRYPTOPP_DISABLE_ASM -miphoneos-version-min=7\"/' setenv-ios.sh
fi;
source setenv-ios.sh ${ARCH}
mkdir -p "${CURRENTPATH}/bin/${ARCH}.sdk"
make -f GNUmakefile-cross lean -j ${NPROCESSORS}
mv libcryptopp.a "${CURRENTPATH}/bin/${ARCH}.sdk"
popd
rm -rf cryptopp-${CRYPTOPP_VERSION}
done

mkdir -p lib

lipo -create "${CURRENTPATH}/bin/x86_64.sdk/libcryptopp.a" "${CURRENTPATH}/bin/arm64.sdk/libcryptopp.a" -output "${CURRENTPATH}/libcryptopp.a"

tar zxf ${CRYPTOPP_VERSION}.tar.gz
mkdir -p include/cryptopp || true
cp -f cryptopp-${CRYPTOPP_VERSION}/*.h include/cryptopp
rm -rf cryptopp-${CRYPTOPP_VERSION}
mv -f libcryptopp.a lib/

rm -rf bin
rm -rf ${CRYPTOPP_VERSION}.tar.gz

echo "Done."
