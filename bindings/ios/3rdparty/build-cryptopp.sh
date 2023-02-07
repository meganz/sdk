#!/bin/sh

CURRENTPATH=`pwd`

CRYPTOPP_VERSION="64ecb2e974bfecd26467c4c301de3bd5ed29cb67"

set -e

NPROCESSORS=$(getconf NPROCESSORS_ONLN 2>/dev/null || getconf _NPROCESSORS_ONLN 2>/dev/null)

if [ ! -e "${CRYPTOPP_VERSION}.tar.gz" ]
then
curl -LO "https://github.com/weidai11/cryptopp/archive/${CRYPTOPP_VERSION}.tar.gz"
fi

ARCHS="x86_64 arm64-simulator arm64"

tar zxf ${CRYPTOPP_VERSION}.tar.gz

for ARCH in ${ARCHS}
do
pushd cryptopp-${CRYPTOPP_VERSION}
if [ $ARCH = "x86_64" ]; then
source TestScripts/setenv-ios.sh iPhoneSimulator ${ARCH}
elif [ $ARCH = "arm64" ]; then
sed -i '' $'75s/DEF_CPPFLAGS=\"-DNDEBUG\"/DEF_CPPFLAGS=\"-DNDEBUG -DCRYPTOPP_DISABLE_ARM_CRC32\"/' TestScripts/setenv-ios.sh
source TestScripts/setenv-ios.sh iPhone ${ARCH}
elif [ $ARCH = "arm64-simulator" ]; then
source TestScripts/setenv-ios.sh iPhoneSimulator ${ARCH}
fi;
mkdir -p "${CURRENTPATH}/bin/${ARCH}.sdk"
make -f GNUmakefile-cross lean -j${NPROCESSORS}
mv libcryptopp.a "${CURRENTPATH}/bin/${ARCH}.sdk"
make clean
popd
done

mkdir xcframework || true

lipo -create "${CURRENTPATH}/bin/x86_64.sdk/libcryptopp.a" "${CURRENTPATH}/bin/arm64-simulator.sdk/libcryptopp.a" -output "${CURRENTPATH}/bin/libcryptopp.a"

xcodebuild -create-xcframework -library ${CURRENTPATH}/bin/libcryptopp.a -library ${CURRENTPATH}/bin/arm64.sdk/libcryptopp.a -output ${CURRENTPATH}/xcframework/libcryptopp.xcframework

tar zxf ${CRYPTOPP_VERSION}.tar.gz
mkdir -p include/cryptopp || true
cp -f cryptopp-${CRYPTOPP_VERSION}/*.h include/cryptopp
rm -rf cryptopp-${CRYPTOPP_VERSION}

rm -rf bin
rm -rf ${CRYPTOPP_VERSION}.tar.gz

echo "Done."
