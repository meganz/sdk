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

if [ ! -d "libevent" ]
then
git clone --recursive -b master https://github.com/meganz/libevent
fi

for ARCH in ${ARCHS}
do
if [[ "${ARCH}" == "i386" || "${ARCH}" == "x86_64" ]];
then
PLATFORM="iPhoneSimulator"
else
PLATFORM="iPhoneOS"
fi

pushd libevent
git reset --hard && git clean -dfx

export BUILD_TOOLS="${DEVELOPER}"
export BUILD_DEVROOT="${DEVELOPER}/Platforms/${PLATFORM}.platform/Developer"
export BUILD_SDKROOT="${BUILD_DEVROOT}/SDKs/${PLATFORM}${SDKVERSION}.sdk"

export CC="${BUILD_TOOLS}/usr/bin/gcc -arch ${ARCH}"
mkdir -p "${CURRENTPATH}/bin/${PLATFORM}${SDKVERSION}-${ARCH}.sdk"

# Build
export LDFLAGS="-Os -arch ${ARCH} -Wl,-dead_strip -miphoneos-version-min=7.0 -L${BUILD_SDKROOT}/usr/lib -L${OPENSSL_PREFIX}/lib"
export CFLAGS="-Os -arch ${ARCH} -pipe -no-cpp-precomp -isysroot ${BUILD_SDKROOT} -miphoneos-version-min=7.0 -I${OPENSSL_PREFIX}/include"
export CPPFLAGS="${CFLAGS} -I${BUILD_SDKROOT}/usr/include"
export CXXFLAGS="${CPPFLAGS}"

./autogen.sh

if [ "${ARCH}" == "arm64" ]; then
./configure --host=aarch64-apple-darwin --enable-static --disable-shared --disable-libevent-regress --disable-tests --disable-samples --disable-clock-gettime
else
./configure --host=${ARCH}-apple-darwin --enable-static --disable-shared --disable-libevent-regress --disable-tests --disable-samples --disable-clock-gettime
fi

make -j8
cp -f .libs/libevent.a ${CURRENTPATH}/bin/${PLATFORM}${SDKVERSION}-${ARCH}.sdk/
cp -f .libs/libevent_core.a ${CURRENTPATH}/bin/${PLATFORM}${SDKVERSION}-${ARCH}.sdk/
cp -f .libs/libevent_extra.a ${CURRENTPATH}/bin/${PLATFORM}${SDKVERSION}-${ARCH}.sdk/
cp -f .libs/libevent_pthreads.a ${CURRENTPATH}/bin/${PLATFORM}${SDKVERSION}-${ARCH}.sdk/
cp -f .libs/libevent_openssl.a ${CURRENTPATH}/bin/${PLATFORM}${SDKVERSION}-${ARCH}.sdk/

#make clean

popd

done


mkdir lib || true
lipo -create ${CURRENTPATH}/bin/iPhoneSimulator${SDKVERSION}-i386.sdk/libevent.a ${CURRENTPATH}/bin/iPhoneSimulator${SDKVERSION}-x86_64.sdk/libevent.a  ${CURRENTPATH}/bin/iPhoneOS${SDKVERSION}-armv7.sdk/libevent.a ${CURRENTPATH}/bin/iPhoneOS${SDKVERSION}-armv7s.sdk/libevent.a ${CURRENTPATH}/bin/iPhoneOS${SDKVERSION}-arm64.sdk/libevent.a -output ${CURRENTPATH}/lib/libevent.a

lipo -create ${CURRENTPATH}/bin/iPhoneSimulator${SDKVERSION}-i386.sdk/libevent_core.a ${CURRENTPATH}/bin/iPhoneSimulator${SDKVERSION}-x86_64.sdk/libevent_core.a  ${CURRENTPATH}/bin/iPhoneOS${SDKVERSION}-armv7.sdk/libevent_core.a ${CURRENTPATH}/bin/iPhoneOS${SDKVERSION}-armv7s.sdk/libevent_core.a ${CURRENTPATH}/bin/iPhoneOS${SDKVERSION}-arm64.sdk/libevent_core.a -output ${CURRENTPATH}/lib/libevent_core.a

lipo -create ${CURRENTPATH}/bin/iPhoneSimulator${SDKVERSION}-i386.sdk/libevent_extra.a ${CURRENTPATH}/bin/iPhoneSimulator${SDKVERSION}-x86_64.sdk/libevent_extra.a  ${CURRENTPATH}/bin/iPhoneOS${SDKVERSION}-armv7.sdk/libevent_extra.a ${CURRENTPATH}/bin/iPhoneOS${SDKVERSION}-armv7s.sdk/libevent_extra.a ${CURRENTPATH}/bin/iPhoneOS${SDKVERSION}-arm64.sdk/libevent_extra.a -output ${CURRENTPATH}/lib/libevent_extra.a

lipo -create ${CURRENTPATH}/bin/iPhoneSimulator${SDKVERSION}-i386.sdk/libevent_pthreads.a ${CURRENTPATH}/bin/iPhoneSimulator${SDKVERSION}-x86_64.sdk/libevent_pthreads.a  ${CURRENTPATH}/bin/iPhoneOS${SDKVERSION}-armv7.sdk/libevent_pthreads.a ${CURRENTPATH}/bin/iPhoneOS${SDKVERSION}-armv7s.sdk/libevent_pthreads.a ${CURRENTPATH}/bin/iPhoneOS${SDKVERSION}-arm64.sdk/libevent_pthreads.a -output ${CURRENTPATH}/lib/libevent_pthreads.a

lipo -create ${CURRENTPATH}/bin/iPhoneSimulator${SDKVERSION}-i386.sdk/libevent_openssl.a ${CURRENTPATH}/bin/iPhoneSimulator${SDKVERSION}-x86_64.sdk/libevent_openssl.a  ${CURRENTPATH}/bin/iPhoneOS${SDKVERSION}-armv7.sdk/libevent_openssl.a ${CURRENTPATH}/bin/iPhoneOS${SDKVERSION}-armv7s.sdk/libevent_openssl.a ${CURRENTPATH}/bin/iPhoneOS${SDKVERSION}-arm64.sdk/libevent_openssl.a -output ${CURRENTPATH}/lib/libevent_openssl.a

mkdir -p include/libevent || true
cp -f -R libevent/include/* include/libevent/

#rm -rf bin

echo "Done."
