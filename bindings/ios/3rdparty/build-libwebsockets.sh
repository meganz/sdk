#!/bin/sh

MEGACHAT_VERSION="GIT"
SDKVERSION=`xcrun -sdk iphoneos --show-sdk-version`

##############################################
CURRENTPATH=`pwd`
OPENSSL_PREFIX="${CURRENTPATH}"
ARCHS="arm64 x86_64"
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

if [ ! -d "libwebsockets" ]
then
git clone -b v4.2-stable https://github.com/warmcat/libwebsockets.git
fi

for ARCH in ${ARCHS}
do

if [ "${ARCH}" == "x86_64" ];
then
PLATFORM="iPhoneSimulator"
else
PLATFORM="iPhoneOS"
fi

echo "BUILDING FOR ${ARCH}"

IOSC_TARGET=iphoneos
IOSC_OS_VERSION=-mios-version-min=12.1
IOSC_ARCH=${ARCH}
IOSC_PLATFORM_SDKNAME=${PLATFORM}
IOSC_CMAKE_TOOLCHAIN="../ios.toolchain.cmake"
IOSC_SYSROOT=`xcrun -sdk $IOSC_TARGET -show-sdk-path`
# the same as SDKROOT

pushd libwebsockets
git reset --hard && git clean -dfx

export BUILD_TOOLS="${DEVELOPER}"
export BUILD_DEVROOT="${DEVELOPER}/Platforms/${PLATFORM}.platform/Developer"
export BUILD_SDKROOT="${BUILD_DEVROOT}/SDKs/${PLATFORM}${SDKVERSION}.sdk"

export CC="${BUILD_TOOLS}/usr/bin/gcc -arch ${ARCH}"
mkdir -p "${CURRENTPATH}/bin/${PLATFORM}${SDKVERSION}-${ARCH}.sdk"

# Build
export LDFLAGS="-arch ${ARCH} -Wl,-dead_strip -miphoneos-version-min=12.1 -L${BUILD_SDKROOT}/usr/lib"
export CFLAGS="-arch ${ARCH} -pipe -no-cpp-precomp -isysroot ${BUILD_SDKROOT} -miphoneos-version-min=12.1 -g3"
export CPPFLAGS="${CFLAGS} -I${BUILD_SDKROOT}/usr/include"
export CXXFLAGS="${CPPFLAGS}"

if [ "${ARCH}" == "arm64" ]; then

IOSC_HOST_TRIPLET=aarch64-apple-darwin

cmake . -DCMAKE_TOOLCHAIN_FILE=${CURRENTPATH}/libwebsockets/contrib/iOS.cmake -DIOS_PLATFORM=OS -DBUILD_ARM64=1  -DLWS_OPENSSL_INCLUDE_DIRS=${CURRENTPATH}/webrtc/third_party/boringssl/src/include -DLWS_OPENSSL_LIBRARIES=${CURRENTPATH}/lib/libwebrtc.a -DLWS_WITH_LIBUV=1 -DLIBUV_INCLUDE_DIRS=${CURRENTPATH}/include -DLIBUV_LIBRARIES=${CURRENTPATH}/lib/libuv.a -DLWS_IPV6=ON -DLWS_SSL_CLIENT_USE_OS_CA_CERTS=0 -DLWS_WITH_SHARED=OFF -DLWS_WITHOUT_TESTAPPS=ON -DLWS_WITHOUT_SERVER=ON -DIOS_BITCODE=1 -DLWS_WITH_HTTP2=0 -DLWS_WITH_BORINGSSL=1

elif [ "${ARCH}" == "x86_64" ]; then

IOSC_HOST_TRIPLET=${ARCH}-apple-darwin

cmake . -DCMAKE_TOOLCHAIN_FILE=${CURRENTPATH}/libwebsockets/contrib/iOS.cmake -DIOS_PLATFORM=SIMULATOR64  -DLWS_OPENSSL_INCLUDE_DIRS=${CURRENTPATH}/webrtc/third_party/boringssl/src/include -DLWS_OPENSSL_LIBRARIES=${CURRENTPATH}/lib/libwebrtc.a -DLWS_WITH_LIBUV=1 -DLIBUV_INCLUDE_DIRS=${CURRENTPATH}/include -DLIBUV_LIBRARIES=${CURRENTPATH}/lib/libuv.a -DLWS_IPV6=ON -DLWS_SSL_CLIENT_USE_OS_CA_CERTS=0 -DLWS_WITH_SHARED=OFF -DLWS_WITHOUT_TESTAPPS=ON -DLWS_WITHOUT_SERVER=ON -DIOS_BITCODE=1 -DLWS_WITH_HTTP2=0 -DLWS_WITH_BORINGSSL=1

fi

CMAKE_XCOMPILE_ARGS="-DCMAKE_TOOLCHAIN_FILE=$IOSC_CMAKE_TOOLCHAIN -DCMAKE_INSTALL_PREFIX=$IOSC_BUILDROOT"
CONFIGURE_XCOMPILE_ARGS="--prefix=$IOSC_BUILDROOT --host=$IOSC_HOST_TRIPLET"

make -j8

cp -f lib/libwebsockets.a ${CURRENTPATH}/bin/${PLATFORM}${SDKVERSION}-${ARCH}.sdk/

popd

done

mkdir lib || true
lipo -create ${CURRENTPATH}/bin/iPhoneSimulator${SDKVERSION}-x86_64.sdk/libwebsockets.a  ${CURRENTPATH}/bin/iPhoneOS${SDKVERSION}-arm64.sdk/libwebsockets.a -output ${CURRENTPATH}/lib/libwebsockets.a

cp -f -R libwebsockets/include/* include/

rm -fr bin
rm -fr libwebsockets
echo "Done."
