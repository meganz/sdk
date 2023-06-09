#!/bin/sh

CURL_VERSION="8.1.2"
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

if [ ! -e "curl-${CURL_VERSION}.tar.gz" ]
then
curl -LO "https://curl.haxx.se/download/curl-${CURL_VERSION}.tar.gz"
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

rm -rf curl-${CURL_VERSION}
tar zxf curl-${CURL_VERSION}.tar.gz
pushd "curl-${CURL_VERSION}"

echo "${bold}Building CURL for $PLATFORM $ARCH ${normal}"

export BUILD_TOOLS="${DEVELOPER}"
export BUILD_DEVROOT="${DEVELOPER}/Platforms/${PLATFORM}.platform/Developer"
export BUILD_SDKROOT="${BUILD_DEVROOT}/SDKs/${PLATFORM}${SDKVERSION}.sdk"

RUNTARGET=""
if [[ "${ARCH}" == "arm64"  && "$PLATFORM" == "iPhoneSimulator" ]];
then
RUNTARGET="-target ${ARCH}-apple-ios14.0-simulator"
fi
export CC="${BUILD_TOOLS}/usr/bin/gcc -arch ${ARCH}"
mkdir -p "${CURRENTPATH}/bin/libcurl/${PLATFORM}${SDKVERSION}-${ARCH}.sdk"

# Build
export LDFLAGS="-Os -arch ${ARCH} -Wl,-dead_strip -miphoneos-version-min=14.0"
export CFLAGS="-Os -arch ${ARCH} -pipe -no-cpp-precomp -isysroot ${BUILD_SDKROOT} -miphoneos-version-min=14.0 ${RUNTARGET}"
export CPPFLAGS="${CFLAGS} -DNDEBUG"
export CXXFLAGS="${CPPFLAGS}"

if [ "${ARCH}" == "arm64" ]; then
./configure --prefix="${CURRENTPATH}/bin/libcurl/${PLATFORM}${SDKVERSION}-${ARCH}.sdk" --host=arm-apple-darwin --enable-static --disable-shared --with-secure-transport --with-zlib --disable-manual --disable-ftp --disable-file --disable-ldap --disable-ldaps --disable-rtsp --disable-dict --disable-telnet --disable-tftp --disable-pop3 --disable-imap --disable-smtp --disable-gopher --disable-sspi --enable-ipv6 --disable-smb
else
./configure --prefix="${CURRENTPATH}/bin/libcurl/${PLATFORM}${SDKVERSION}-${ARCH}.sdk" --host=${ARCH}-apple-darwin --enable-static --disable-shared --with-secure-transport --with-zlib --disable-manual --disable-ftp --disable-file --disable-ldap --disable-ldaps --disable-rtsp --disable-dict --disable-telnet --disable-tftp --disable-pop3 --disable-imap --disable-smtp --disable-gopher --disable-sspi --enable-ipv6 --disable-smb
fi

make -j${CORES}
make install
make clean

popd

done

mkdir xcframework || true

echo "${bold}Lipo library for x86_64 and arm64 simulators ${normal}"

lipo -create ${CURRENTPATH}/bin/libcurl/iPhoneSimulator${SDKVERSION}-x86_64.sdk/lib/libcurl.a ${CURRENTPATH}/bin/libcurl/iPhoneSimulator${SDKVERSION}-arm64.sdk/lib/libcurl.a -output ${CURRENTPATH}/bin/libcurl/libcurl.a

echo "${bold}Creating xcframework ${normal}"

xcodebuild -create-xcframework -library ${CURRENTPATH}/bin/libcurl/libcurl.a -headers ${CURRENTPATH}/bin/libcurl/iPhoneSimulator${SDKVERSION}-arm64.sdk/include -library ${CURRENTPATH}/bin/libcurl/iPhoneOS${SDKVERSION}-arm64.sdk/lib/libcurl.a -headers ${CURRENTPATH}/bin/libcurl/iPhoneOS${SDKVERSION}-arm64.sdk/include -output ${CURRENTPATH}/xcframework/libcurl.xcframework
 
echo "${bold}Cleaning up ${normal}"

rm -rf curl-${CURL_VERSION}
rm -rf curl-${CURL_VERSION}.tar.gz
rm -rf bin

echo "${bold}Done.${normal}"

