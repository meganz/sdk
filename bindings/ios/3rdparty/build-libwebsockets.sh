#!/bin/sh

MEGACHAT_VERSION="GIT"
SDKVERSION=`xcrun -sdk iphoneos --show-sdk-version`

##############################################
CURRENTPATH=`pwd`
ARCHS="x86_64 arm64 arm64-simulator"
DEVELOPER=`xcode-select -print-path`

CORES=$(sysctl -n hw.ncpu)

# Formating
green="\033[32m"
bold="\033[0m${green}\033[1m"
normal="\033[0m"

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

if [[ "${ARCH}" == "x86_64" || "${ARCH}" == "arm64-simulator" ]];
then
PLATFORM="iPhoneSimulator"
if [ "${ARCH}" == "arm64-simulator" ];
then
ARCH="arm64"
fi
else
PLATFORM="iPhoneOS"
fi

echo "${bold}Building libwebsockets for $PLATFORM $ARCH ${normal}"

IOSC_TARGET=iphoneos
IOSC_OS_VERSION=-mios-version-min=14.0
IOSC_ARCH=${ARCH}
IOSC_PLATFORM_SDKNAME=${PLATFORM}
IOSC_SYSROOT=`xcrun -sdk $IOSC_TARGET -show-sdk-path`
# the same as SDKROOT

pushd libwebsockets
git reset --hard && git clean -dfx

export BUILD_TOOLS="${DEVELOPER}"
export BUILD_DEVROOT="${DEVELOPER}/Platforms/${PLATFORM}.platform/Developer"
export BUILD_SDKROOT="${BUILD_DEVROOT}/SDKs/${PLATFORM}${SDKVERSION}.sdk"

RUNTARGET=""
if [[ "${ARCH}" == "arm64"  && "$PLATFORM" == "iPhoneSimulator" ]];
then
RUNTARGET="-target ${ARCH}-apple-ios14.0-simulator"
fi

export CC="${BUILD_TOOLS}/usr/bin/gcc -arch ${ARCH}"
mkdir -p "${CURRENTPATH}/bin/libwebsockets/${PLATFORM}${SDKVERSION}-${ARCH}.sdk"

# Build
export LDFLAGS="-arch ${ARCH} -Wl,-dead_strip -miphoneos-version-min=14.0 -L${BUILD_SDKROOT}/usr/lib"
export CFLAGS="-arch ${ARCH} -pipe -no-cpp-precomp -isysroot ${BUILD_SDKROOT} -miphoneos-version-min=14.0 -g3 ${RUNTARGET}"
export CPPFLAGS="${CFLAGS} -I${BUILD_SDKROOT}/usr/include -DNDEBUG"
export CXXFLAGS="${CPPFLAGS}"

if [[ "${ARCH}" == "arm64" && "$PLATFORM" == "iPhoneOS" ]]; then

cmake . -DCMAKE_INSTALL_PREFIX=${CURRENTPATH}/bin/libwebsockets/${PLATFORM}${SDKVERSION}-${ARCH}.sdk -DCMAKE_TOOLCHAIN_FILE=${CURRENTPATH}/libwebsockets/contrib/iOS.cmake -DIOS_PLATFORM=OS -DBUILD_ARM64=1 -DLWS_OPENSSL_INCLUDE_DIRS=${CURRENTPATH}/webrtc/third_party/boringssl/src/include -DLWS_OPENSSL_LIBRARIES=${CURRENTPATH}/lib/libwebrtc.xcframework/ios-arm64/libwebrtc.a -DLWS_WITH_LIBUV=1 -DLIBUV_INCLUDE_DIRS=${CURRENTPATH}/bin/libuv/${PLATFORM}${SDKVERSION}-${ARCH}.sdk/include -DLIBUV_LIBRARIES=${CURRENTPATH}/bin/libuv/${PLATFORM}${SDKVERSION}-${ARCH}.sdk/lib/liuv.a -DLWS_IPV6=ON -DLWS_SSL_CLIENT_USE_OS_CA_CERTS=0 -DLWS_WITH_SHARED=OFF -DLWS_WITHOUT_TESTAPPS=ON -DLWS_WITHOUT_SERVER=ON -DLWS_WITH_HTTP2=0 -DLWS_WITH_BORINGSSL=1 -DIOS_BITCODE=0


elif [ "${ARCH}" == "x86_64" ]; then

cmake . -DCMAKE_INSTALL_PREFIX=${CURRENTPATH}/bin/libwebsockets/${PLATFORM}${SDKVERSION}-${ARCH}.sdk -DCMAKE_TOOLCHAIN_FILE=${CURRENTPATH}/libwebsockets/contrib/iOS.cmake -DIOS_PLATFORM=SIMULATOR64 -DLWS_OPENSSL_INCLUDE_DIRS=${CURRENTPATH}/webrtc/third_party/boringssl/src/include -DLWS_OPENSSL_LIBRARIES=${CURRENTPATH}/lib/libwebrtc.xcframework/ios-arm64_x86_64-simulator/libwebrtc.a -DLWS_WITH_LIBUV=1 -DLIBUV_INCLUDE_DIRS=${CURRENTPATH}/bin/libuv/${PLATFORM}${SDKVERSION}-${ARCH}.sdk/include -DLIBUV_LIBRARIES=${CURRENTPATH}/bin/libuv/${PLATFORM}${SDKVERSION}-${ARCH}.sdk/lib/liuv.a -DLWS_IPV6=ON -DLWS_SSL_CLIENT_USE_OS_CA_CERTS=0 -DLWS_WITH_SHARED=OFF -DLWS_WITHOUT_TESTAPPS=ON -DLWS_WITHOUT_SERVER=ON -DLWS_WITH_HTTP2=0 -DLWS_WITH_BORINGSSL=1 -DIOS_BITCODE=0

else

cmake . -DCMAKE_INSTALL_PREFIX=${CURRENTPATH}/bin/libwebsockets/${PLATFORM}${SDKVERSION}-${ARCH}.sdk -DCMAKE_TOOLCHAIN_FILE=${CURRENTPATH}/libwebsockets/contrib/iOS.cmake -DIOS_PLATFORM=OS -DBUILD_ARM64=1 -DLWS_OPENSSL_INCLUDE_DIRS=${CURRENTPATH}/webrtc/third_party/boringssl/src/include -DLWS_OPENSSL_LIBRARIES=${CURRENTPATH}/lib/libwebrtc.xcframework/ios-arm64_x86_64-simulator/libwebrtc.a -DLWS_WITH_LIBUV=1 -DLIBUV_INCLUDE_DIRS=${CURRENTPATH}/bin/libuv/${PLATFORM}${SDKVERSION}-${ARCH}.sdk/include -DLIBUV_LIBRARIES=${CURRENTPATH}/bin/libuv/${PLATFORM}${SDKVERSION}-${ARCH}.sdk/lib/liuv.a -DLWS_IPV6=ON -DLWS_SSL_CLIENT_USE_OS_CA_CERTS=0 -DLWS_WITH_SHARED=OFF -DLWS_WITHOUT_TESTAPPS=ON -DLWS_WITHOUT_SERVER=ON -DLWS_WITH_HTTP2=0 -DLWS_WITH_BORINGSSL=1 -DIOS_BITCODE=0

fi

make -j${CORES}
make install
make clean

popd

done

mkdir xcframework || true

echo "${bold}Lipo library for x86_64 and arm64 simulators ${normal}"

lipo -create ${CURRENTPATH}/bin/libwebsockets/iPhoneSimulator${SDKVERSION}-x86_64.sdk/lib/libwebsockets.a ${CURRENTPATH}/bin/libwebsockets/iPhoneSimulator${SDKVERSION}-arm64.sdk/lib/libwebsockets.a -output ${CURRENTPATH}/bin/libwebsockets/libwebsockets.a

echo "${bold}Creating xcframework ${normal}"

xcodebuild -create-xcframework -library ${CURRENTPATH}/bin/libwebsockets/libwebsockets.a -headers ${CURRENTPATH}/bin/libwebsockets/iPhoneSimulator${SDKVERSION}-arm64.sdk/include -library ${CURRENTPATH}/bin/libwebsockets/iPhoneOS${SDKVERSION}-arm64.sdk/lib/libwebsockets.a -headers ${CURRENTPATH}/bin/libwebsockets/iPhoneOS${SDKVERSION}-arm64.sdk/include -output ${CURRENTPATH}/xcframework/libwebsockets.xcframework

rm -fr bin
rm -fr libwebsockets
echo "${bold}Done.${normal}"
