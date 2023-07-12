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
mkdir -p "${CURRENTPATH}/bin/cryptopp/${ARCH}.sdk"
make -f GNUmakefile-cross lean -j${NPROCESSORS}
mv libcryptopp.a "${CURRENTPATH}/bin/cryptopp/${ARCH}.sdk"
mkdir -p "${CURRENTPATH}/bin/cryptopp/${ARCH}.sdk/include/cryptopp"
cp -f *.h "${CURRENTPATH}/bin/cryptopp/${ARCH}.sdk/include/cryptopp"
make clean
popd
done

mkdir xcframework || true

lipo -create "${CURRENTPATH}/bin/cryptopp/x86_64.sdk/libcryptopp.a" "${CURRENTPATH}/bin/cryptopp/arm64-simulator.sdk/libcryptopp.a" -output "${CURRENTPATH}/bin/cryptopp/libcryptopp.a"

xcodebuild -create-xcframework -library ${CURRENTPATH}/bin/cryptopp/libcryptopp.a -headers ${CURRENTPATH}/bin/cryptopp/arm64-simulator.sdk//include -library ${CURRENTPATH}/bin/cryptopp/arm64.sdk/libcryptopp.a -headers ${CURRENTPATH}/bin/cryptopp/arm64.sdk/include -output ${CURRENTPATH}/xcframework/libcryptopp.xcframework

rm -rf bin
rm -rf ${CRYPTOPP_VERSION}.tar.gz
rm -rf cryptopp-${CRYPTOPP_VERSION}


echo "${bold}Done.${normal}"
