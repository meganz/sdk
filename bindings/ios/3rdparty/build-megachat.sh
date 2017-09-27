#!/bin/sh

MEGACHAT_VERSION="GIT"
SDKVERSION=`xcrun -sdk iphoneos --show-sdk-version`

##############################################
CURRENTPATH=`pwd`
OPENSSL_PREFIX="${CURRENTPATH}"
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

if [ ! -d "karere-native" ]
then
git clone --recursive -b develop https://code.developers.mega.co.nz/messenger/karere-native
fi

ln -sf ../../mega include/mega

for ARCH in ${ARCHS}
do

if [[ "${ARCH}" == "i386" || "${ARCH}" == "x86_64" ]];
then
PLATFORM="iPhoneSimulator"
else
PLATFORM="iPhoneOS"
fi

echo "BUILDING FOR ${ARCH}"

IOSC_TARGET=iphoneos
IOSC_OS_VERSION=-mios-version-min=7.0
IOSC_ARCH=${ARCH}
IOSC_PLATFORM_SDKNAME=${PLATFORM}
IOSC_CMAKE_TOOLCHAIN="../ios.toolchain.cmake"
IOSC_SYSROOT=`xcrun -sdk $IOSC_TARGET -show-sdk-path`
# the same as SDKROOT

pushd karere-native/src
git reset --hard && git clean -dfx

export BUILD_TOOLS="${DEVELOPER}"
export BUILD_DEVROOT="${DEVELOPER}/Platforms/${PLATFORM}.platform/Developer"
export BUILD_SDKROOT="${BUILD_DEVROOT}/SDKs/${PLATFORM}${SDKVERSION}.sdk"

export CC="${BUILD_TOOLS}/usr/bin/gcc -arch ${ARCH}"
mkdir -p "${CURRENTPATH}/bin/${PLATFORM}${SDKVERSION}-${ARCH}.sdk"

# Build
export LDFLAGS="-Os -arch ${ARCH} -Wl,-dead_strip -miphoneos-version-min=7.0 -L${BUILD_SDKROOT}/usr/lib"
export CFLAGS="-Os -arch ${ARCH} -pipe -no-cpp-precomp -isysroot ${BUILD_SDKROOT} -miphoneos-version-min=7.0 -D_DARWIN_C_SOURCE -g3"
export CPPFLAGS="${CFLAGS} -I${BUILD_SDKROOT}/usr/include"
export CXXFLAGS="${CPPFLAGS}"

if [ "$1" == "--enable-webrtc" ]; then
export ENABLE_WEBRTC="-DoptKarereDisableWebrtc=0"
fi


if [ "${ARCH}" == "arm64" ]; then

IOSC_HOST_TRIPLET=aarch64-apple-darwin

cmake . -D_LIBMEGA_LIBRARIES= -DLIBMEGA_PUBLIC_INCLUDE_DIR=../../../../../include -DCMAKE_SYSROOT=${CURRENTPATH} -DCMAKE_TOOLCHAIN_FILE=${CURRENTPATH}/iOS.cmake -DIOS_PLATFORM=OS -DIOS_PLATFORM_TYPE=${ARCH} -DBUILD_ARM64=1 -DCMAKE_LIBRARY_PATH=${CURRENTPATH}/lib -DCMAKE_INCLUDE_PATH=${CURRENTPATH}/include -DLIBEVENT_INCLUDE_DIRS=${CURRENTPATH}/include/libevent -DLIBEVENT_LIB_CORE=${CURRENTPATH}/lib -DLIBEVENT_LIB_EXTRA=${CURRENTPATH}/lib -DLIBEVENT_LIB_OPENSSL=${CURRENTPATH}/lib -DLIBEVENT_LIB_PTHREADS=${CURRENTPATH}/lib -DOPENSSL_INCLUDE_DIR=${CURRENTPATH}/include -DOPENSSL_SSL_LIBRARY=${CURRENTPATH}/lib/libssl.a -DOPENSSL_CRYPTO_LIBRARY=${CURRENTPATH}/lib/libcrypto.a -DOPENSSL_ROOT_DIR=${CURRENTPATH} -DLIBEVENT_LIB=${CURRENTPATH}/lib/libevent.a ${ENABLE_WEBRTC}

elif [ "${ARCH}" == "i386" ]; then

IOSC_HOST_TRIPLET=${ARCH}-apple-darwin

cmake . -D_LIBMEGA_LIBRARIES= -DLIBMEGA_PUBLIC_INCLUDE_DIR=../../../../../include -DCMAKE_SYSROOT=${CURRENTPATH} -DCMAKE_TOOLCHAIN_FILE=${CURRENTPATH}/iOS.cmake -DIOS_PLATFORM=SIMULATOR -DCMAKE_LIBRARY_PATH=${CURRENTPATH}/lib -DCMAKE_INCLUDE_PATH=${CURRENTPATH}/include -DLIBEVENT_INCLUDE_DIRS=${CURRENTPATH}/include/libevent -DLIBEVENT_LIB_CORE=${CURRENTPATH}/lib -DLIBEVENT_LIB_EXTRA=${CURRENTPATH}/lib -DLIBEVENT_LIB_OPENSSL=${CURRENTPATH}/lib -DLIBEVENT_LIB_PTHREADS=${CURRENTPATH}/lib -DOPENSSL_INCLUDE_DIR=${CURRENTPATH}/include -DOPENSSL_SSL_LIBRARY=${CURRENTPATH}/lib/libssl.a -DOPENSSL_CRYPTO_LIBRARY=${CURRENTPATH}/lib/libcrypto.a -DOPENSSL_ROOT_DIR=${CURRENTPATH} -DLIBEVENT_LIB=${CURRENTPATH}/lib/libevent.a ${ENABLE_WEBRTC}

elif [ "${ARCH}" == "x86_64" ]; then

IOSC_HOST_TRIPLET=${ARCH}-apple-darwin

cmake . -D_LIBMEGA_LIBRARIES= -DLIBMEGA_PUBLIC_INCLUDE_DIR=../../../../../include -DCMAKE_SYSROOT=${CURRENTPATH} -DCMAKE_TOOLCHAIN_FILE=${CURRENTPATH}/iOS.cmake -DIOS_PLATFORM=SIMULATOR64 -DCMAKE_LIBRARY_PATH=${CURRENTPATH}/lib -DCMAKE_INCLUDE_PATH=${CURRENTPATH}/include -DLIBEVENT_INCLUDE_DIRS=${CURRENTPATH}/include/libevent -DLIBEVENT_LIB_CORE=${CURRENTPATH}/lib -DLIBEVENT_LIB_EXTRA=${CURRENTPATH}/lib -DLIBEVENT_LIB_OPENSSL=${CURRENTPATH}/lib -DLIBEVENT_LIB_PTHREADS=${CURRENTPATH}/lib -DOPENSSL_INCLUDE_DIR=${CURRENTPATH}/include -DOPENSSL_SSL_LIBRARY=${CURRENTPATH}/lib/libssl.a -DOPENSSL_CRYPTO_LIBRARY=${CURRENTPATH}/lib/libcrypto.a -DOPENSSL_ROOT_DIR=${CURRENTPATH} -DLIBEVENT_LIB=${CURRENTPATH}/lib/libevent.a ${ENABLE_WEBRTC}

else

IOSC_HOST_TRIPLET=${ARCH}-apple-darwin

cmake . -D_LIBMEGA_LIBRARIES= -DLIBMEGA_PUBLIC_INCLUDE_DIR=../../../../../include -DCMAKE_SYSROOT=${CURRENTPATH} -DCMAKE_TOOLCHAIN_FILE=${CURRENTPATH}/iOS.cmake -DIOS_PLATFORM=OS -DIOS_PLATFORM_TYPE=${ARCH} -DCMAKE_LIBRARY_PATH=${CURRENTPATH}/lib -DCMAKE_INCLUDE_PATH=${CURRENTPATH}/include -DLIBEVENT_INCLUDE_DIRS=${CURRENTPATH}/include/libevent -DLIBEVENT_LIB_CORE=${CURRENTPATH}/lib -DLIBEVENT_LIB_EXTRA=${CURRENTPATH}/lib -DLIBEVENT_LIB_OPENSSL=${CURRENTPATH}/lib -DLIBEVENT_LIB_PTHREADS=${CURRENTPATH}/lib -DOPENSSL_INCLUDE_DIR=${CURRENTPATH}/include -DOPENSSL_SSL_LIBRARY=${CURRENTPATH}/lib/libssl.a -DOPENSSL_CRYPTO_LIBRARY=${CURRENTPATH}/lib/libcrypto.a -DOPENSSL_ROOT_DIR=${CURRENTPATH} -DLIBEVENT_LIB=${CURRENTPATH}/lib/libevent.a ${ENABLE_WEBRTC}
fi

CMAKE_XCOMPILE_ARGS="-DCMAKE_TOOLCHAIN_FILE=$IOSC_CMAKE_TOOLCHAIN -DCMAKE_INSTALL_PREFIX=$IOSC_BUILDROOT"
CONFIGURE_XCOMPILE_ARGS="--prefix=$IOSC_BUILDROOT --host=$IOSC_HOST_TRIPLET"

make -j8

cp -f libkarere.a ${CURRENTPATH}/bin/${PLATFORM}${SDKVERSION}-${ARCH}.sdk/libkarere.a
cp -f base/libservices.a ${CURRENTPATH}/bin/${PLATFORM}${SDKVERSION}-${ARCH}.sdk/libservices.a

if [ "$1" == "--enable-webrtc" ]; then
cp -f rtcModule/base/strophe/libstrophe.a ${CURRENTPATH}/bin/${PLATFORM}${SDKVERSION}-${ARCH}.sdk/libstrophe.a
cp -f rtcModule/librtcmodule.a ${CURRENTPATH}/bin/${PLATFORM}${SDKVERSION}-${ARCH}.sdk/librtcmodule.a
fi

popd

done


mkdir lib || true
lipo -create ${CURRENTPATH}/bin/iPhoneSimulator${SDKVERSION}-i386.sdk/libkarere.a ${CURRENTPATH}/bin/iPhoneSimulator${SDKVERSION}-x86_64.sdk/libkarere.a  ${CURRENTPATH}/bin/iPhoneOS${SDKVERSION}-armv7.sdk/libkarere.a ${CURRENTPATH}/bin/iPhoneOS${SDKVERSION}-armv7s.sdk/libkarere.a ${CURRENTPATH}/bin/iPhoneOS${SDKVERSION}-arm64.sdk/libkarere.a -output ${CURRENTPATH}/lib/libkarere.a
lipo -create ${CURRENTPATH}/bin/iPhoneSimulator${SDKVERSION}-i386.sdk/libservices.a ${CURRENTPATH}/bin/iPhoneSimulator${SDKVERSION}-x86_64.sdk/libservices.a  ${CURRENTPATH}/bin/iPhoneOS${SDKVERSION}-armv7.sdk/libservices.a ${CURRENTPATH}/bin/iPhoneOS${SDKVERSION}-armv7s.sdk/libservices.a ${CURRENTPATH}/bin/iPhoneOS${SDKVERSION}-arm64.sdk/libservices.a -output ${CURRENTPATH}/lib/libservices.a

if [ "$1" == "--enable-webrtc" ]; then
lipo -create ${CURRENTPATH}/bin/iPhoneSimulator${SDKVERSION}-i386.sdk/libstrophe.a ${CURRENTPATH}/bin/iPhoneSimulator${SDKVERSION}-x86_64.sdk/libstrophe.a  ${CURRENTPATH}/bin/iPhoneOS${SDKVERSION}-armv7.sdk/libstrophe.a ${CURRENTPATH}/bin/iPhoneOS${SDKVERSION}-armv7s.sdk/libstrophe.a ${CURRENTPATH}/bin/iPhoneOS${SDKVERSION}-arm64.sdk/libstrophe.a -output ${CURRENTPATH}/lib/libstrophe.a
lipo -create ${CURRENTPATH}/bin/iPhoneSimulator${SDKVERSION}-i386.sdk/librtcmodule.a ${CURRENTPATH}/bin/iPhoneSimulator${SDKVERSION}-x86_64.sdk/librtcmodule.a ${CURRENTPATH}/bin/iPhoneOS${SDKVERSION}-armv7.sdk/librtcmodule.a ${CURRENTPATH}/bin/iPhoneOS${SDKVERSION}-armv7s.sdk/librtcmodule.a ${CURRENTPATH}/bin/iPhoneOS${SDKVERSION}-arm64.sdk/librtcmodule.a -output ${CURRENTPATH}/lib/librtcmodule.a
fi

mkdir -p include || true
cp -f karere-native/src/megachatapi.h include/

#rm -rf bin

echo "Done."
