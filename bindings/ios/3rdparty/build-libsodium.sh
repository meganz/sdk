#!/bin/sh

LIBSODIUM_VERSION="1.0.11"
SDKVERSION=`xcrun -sdk iphoneos --show-sdk-version`

##############################################
CURRENTPATH=`pwd`
ARCHS="i386 x86_64 armv7 armv7s arm64"
DEVELOPER=`xcode-select -print-path`

if [ ! -d "$DEVELOPER" ]; then
  echo "xcode path is not set correctly $DEVELOPER does not exist (most likely because of xcode > 4.3)"
  echo "run"
  echo "sudo xcode-select -switch <xcode path>"
  echo "for default installation:"
  echo "sudo xcode-select -switch /Applications/Xcode.app/Contents/Developer"
  exit 1
fi

case $DEVELOPER in
     *\ * )
           echo "Your Xcode path contains whitespaces, which is not supported."
           exit 1
          ;;
esac

case $CURRENTPATH in
     *\ * )
           echo "Your path contains whitespaces, which is not supported by 'make install'."
           exit 1
          ;;
esac

set -e

if [ ! -e "libsodium-${LIBSODIUM_VERSION}.tar.gz" ]
then
wget "https://github.com/jedisct1/libsodium/releases/download/${LIBSODIUM_VERSION}/libsodium-${LIBSODIUM_VERSION}.tar.gz"
fi

for ARCH in ${ARCHS}
do
if [[ "${ARCH}" == "i386" || "${ARCH}" == "x86_64" ]];
then
PLATFORM="iPhoneSimulator"
else
PLATFORM="iPhoneOS"
fi

rm -rf libsodium-${LIBSODIUM_VERSION}
tar zxf libsodium-${LIBSODIUM_VERSION}.tar.gz
pushd "libsodium-${LIBSODIUM_VERSION}"

export BUILD_TOOLS="${DEVELOPER}"
export BUILD_DEVROOT="${DEVELOPER}/Platforms/${PLATFORM}.platform/Developer"
export BUILD_SDKROOT="${BUILD_DEVROOT}/SDKs/${PLATFORM}${SDKVERSION}.sdk"

export CC="${BUILD_TOOLS}/usr/bin/gcc -arch ${ARCH}"
mkdir -p "${CURRENTPATH}/bin/${PLATFORM}${SDKVERSION}-${ARCH}.sdk"

# Build
export LDFLAGS="-Os -arch ${ARCH} -Wl,-dead_strip -miphoneos-version-min=7.0 -L${BUILD_SDKROOT}/usr/lib"
export CFLAGS="-Os -arch ${ARCH} -pipe -no-cpp-precomp -isysroot ${BUILD_SDKROOT} -miphoneos-version-min=7.0"
export CPPFLAGS="${CFLAGS} -I${BUILD_SDKROOT}/usr/include"
export CXXFLAGS="${CPPFLAGS}"

if [ "${ARCH}" == "arm64" ]; then
./configure --host=aarch64-apple-darwin --disable-shared --enable-minimal
else
./configure --host=${ARCH}-apple-darwin --disable-shared --enable-minimal
fi

make -j8

cp -f src/libsodium/.libs/libsodium.a ${CURRENTPATH}/bin/${PLATFORM}${SDKVERSION}-${ARCH}.sdk/

popd

done


mkdir lib || true
lipo -create ${CURRENTPATH}/bin/iPhoneSimulator${SDKVERSION}-i386.sdk/libsodium.a ${CURRENTPATH}/bin/iPhoneSimulator${SDKVERSION}-x86_64.sdk/libsodium.a  ${CURRENTPATH}/bin/iPhoneOS${SDKVERSION}-armv7.sdk/libsodium.a ${CURRENTPATH}/bin/iPhoneOS${SDKVERSION}-armv7s.sdk/libsodium.a ${CURRENTPATH}/bin/iPhoneOS${SDKVERSION}-arm64.sdk/libsodium.a -output ${CURRENTPATH}/lib/libsodium.a

cp -fR libsodium-${LIBSODIUM_VERSION}/src/libsodium/include/sodium* include

rm -rf bin
rm -rf libsodium-${LIBSODIUM_VERSION}

echo "Done."

