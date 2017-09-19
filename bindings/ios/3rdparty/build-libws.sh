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

if [ ! -d "libws" ]
then
git clone --recursive -b master https://github.com/meganz/libws
fi

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

pushd libws
git reset --hard && git clean -dfx

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

IOSC_HOST_TRIPLET=aarch64-apple-darwin

cmake . -DCMAKE_TOOLCHAIN_FILE=${CURRENTPATH}/iOS.cmake -DIOS_PLATFORM=OS -DIOS_PLATFORM_TYPE=${ARCH} -DBUILD_ARM64=1 -DCMAKE_LIBRARY_PATH=${CURRENTPATH}/lib -DCMAKE_INCLUDE_PATH=${CURRENTPATH}/include -DLIBEVENT_INCLUDE_DIRS=${CURRENTPATH}/include/libevent -DLIBEVENT_LIB_CORE=${CURRENTPATH}/lib -DLIBEVENT_LIB_EXTRA=${CURRENTPATH}/lib -DLIBEVENT_LIB_OPENSSL=${CURRENTPATH}/lib -DLIBEVENT_LIB_PTHREADS=${CURRENTPATH}/lib -DOPENSSL_INCLUDE_DIR=${CURRENTPATH}/include -DOPENSSL_SSL_LIBRARY=${CURRENTPATH}/lib/libssl.a -DOPENSSL_CRYPTO_LIBRARY=${CURRENTPATH}/lib/libcrypto.a -DOPENSSL_ROOT_DIR=${CURRENTPATH} -DLIBEVENT_LIB=${CURRENTPATH}/lib/libevent.a

elif [ "${ARCH}" == "i386" ]; then

IOSC_HOST_TRIPLET=${ARCH}-apple-darwin

cmake . -DCMAKE_TOOLCHAIN_FILE=${CURRENTPATH}/iOS.cmake -DIOS_PLATFORM=SIMULATOR -DCMAKE_LIBRARY_PATH=${CURRENTPATH}/lib -DCMAKE_INCLUDE_PATH=${CURRENTPATH}/include -DLIBEVENT_INCLUDE_DIRS=${CURRENTPATH}/include/libevent -DLIBEVENT_LIB_CORE=${CURRENTPATH}/lib -DLIBEVENT_LIB_EXTRA=${CURRENTPATH}/lib -DLIBEVENT_LIB_OPENSSL=${CURRENTPATH}/lib -DLIBEVENT_LIB_PTHREADS=${CURRENTPATH}/lib -DOPENSSL_INCLUDE_DIR=${CURRENTPATH}/include -DOPENSSL_SSL_LIBRARY=${CURRENTPATH}/lib/libssl.a -DOPENSSL_CRYPTO_LIBRARY=${CURRENTPATH}/lib/libcrypto.a -DOPENSSL_ROOT_DIR=${CURRENTPATH} -DLIBEVENT_LIB=${CURRENTPATH}/lib/libevent.a

elif [ "${ARCH}" == "x86_64" ]; then

IOSC_HOST_TRIPLET=${ARCH}-apple-darwin

cmake . -DCMAKE_TOOLCHAIN_FILE=${CURRENTPATH}/iOS.cmake -DIOS_PLATFORM=SIMULATOR64 -DCMAKE_LIBRARY_PATH=${CURRENTPATH}/lib -DCMAKE_INCLUDE_PATH=${CURRENTPATH}/include -DLIBEVENT_INCLUDE_DIRS=${CURRENTPATH}/include/libevent -DLIBEVENT_LIB_CORE=${CURRENTPATH}/lib -DLIBEVENT_LIB_EXTRA=${CURRENTPATH}/lib -DLIBEVENT_LIB_OPENSSL=${CURRENTPATH}/lib -DLIBEVENT_LIB_PTHREADS=${CURRENTPATH}/lib -DOPENSSL_INCLUDE_DIR=${CURRENTPATH}/include -DOPENSSL_SSL_LIBRARY=${CURRENTPATH}/lib/libssl.a -DOPENSSL_CRYPTO_LIBRARY=${CURRENTPATH}/lib/libcrypto.a -DOPENSSL_ROOT_DIR=${CURRENTPATH} -DLIBEVENT_LIB=${CURRENTPATH}/lib/libevent.a

else

IOSC_HOST_TRIPLET=${ARCH}-apple-darwin

cmake . -DCMAKE_TOOLCHAIN_FILE=${CURRENTPATH}/iOS.cmake -DIOS_PLATFORM=OS -DIOS_PLATFORM_TYPE=${ARCH} -DCMAKE_LIBRARY_PATH=${CURRENTPATH}/lib -DCMAKE_INCLUDE_PATH=${CURRENTPATH}/include -DLIBEVENT_INCLUDE_DIRS=${CURRENTPATH}/include/libevent -DLIBEVENT_LIB_CORE=${CURRENTPATH}/lib -DLIBEVENT_LIB_EXTRA=${CURRENTPATH}/lib -DLIBEVENT_LIB_OPENSSL=${CURRENTPATH}/lib -DLIBEVENT_LIB_PTHREADS=${CURRENTPATH}/lib -DOPENSSL_INCLUDE_DIR=${CURRENTPATH}/include -DOPENSSL_SSL_LIBRARY=${CURRENTPATH}/lib/libssl.a -DOPENSSL_CRYPTO_LIBRARY=${CURRENTPATH}/lib/libcrypto.a -DOPENSSL_ROOT_DIR=${CURRENTPATH} -DLIBEVENT_LIB=${CURRENTPATH}/lib/libevent.a
fi

CMAKE_XCOMPILE_ARGS="-DCMAKE_TOOLCHAIN_FILE=$IOSC_CMAKE_TOOLCHAIN -DCMAKE_INSTALL_PREFIX=$IOSC_BUILDROOT"
CONFIGURE_XCOMPILE_ARGS="--prefix=$IOSC_BUILDROOT --host=$IOSC_HOST_TRIPLET"

make -j8

cp -f lib/libws.a ${CURRENTPATH}/bin/${PLATFORM}${SDKVERSION}-${ARCH}.sdk/

popd

done


mkdir lib || true
lipo -create ${CURRENTPATH}/bin/iPhoneSimulator${SDKVERSION}-i386.sdk/libws.a ${CURRENTPATH}/bin/iPhoneSimulator${SDKVERSION}-x86_64.sdk/libws.a  ${CURRENTPATH}/bin/iPhoneOS${SDKVERSION}-armv7.sdk/libws.a ${CURRENTPATH}/bin/iPhoneOS${SDKVERSION}-armv7s.sdk/libws.a ${CURRENTPATH}/bin/iPhoneOS${SDKVERSION}-arm64.sdk/libws.a -output ${CURRENTPATH}/lib/libws.a

mkdir -p include/libws || true
cp -f -R libws/src/*.h include/libws/
cp -f -R libws/libws_config.h include/libws/

echo "Done."
