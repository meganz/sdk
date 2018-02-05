#!/bin/sh

CURL_VERSION="7.51.0"
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

if [ ! -e "curl-${CURL_VERSION}.tar.gz" ]
then
curl -LO "https://curl.haxx.se/download/curl-${CURL_VERSION}.tar.gz"
fi

for ARCH in ${ARCHS}
do
if [[ "${ARCH}" == "i386" || "${ARCH}" == "x86_64" ]];
then
PLATFORM="iPhoneSimulator"
else
PLATFORM="iPhoneOS"
fi

rm -rf curl-${CURL_VERSION}
tar zxf curl-${CURL_VERSION}.tar.gz
pushd "curl-${CURL_VERSION}"

# Do not resolve IPs!!
sed -i '' $'s/\#define USE_RESOLVE_ON_IPS 1//' lib/curl_setup.h

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
./configure --host=aarch64-apple-darwin --enable-static --disable-shared --with-ssl=${OPENSSL_PREFIX} --with-zlib --disable-manual --disable-ftp --disable-file --disable-ldap --disable-ldaps --disable-rtsp --disable-proxy --disable-dict --disable-telnet --disable-tftp --disable-pop3 --disable-imap --disable-smtp --disable-gopher --disable-sspi --enable-ipv6 --disable-smb
else
./configure --host=${ARCH}-apple-darwin --enable-static --disable-shared --with-ssl=${OPENSSL_PREFIX} --with-zlib --disable-manual --disable-ftp --disable-file --disable-ldap --disable-ldaps --disable-rtsp --disable-proxy --disable-dict --disable-telnet --disable-tftp --disable-pop3 --disable-imap --disable-smtp --disable-gopher --disable-sspi --enable-ipv6 --disable-smb
fi

make -j8
cp -f lib/.libs/libcurl.a ${CURRENTPATH}/bin/${PLATFORM}${SDKVERSION}-${ARCH}.sdk/
#make clean

popd

done


mkdir lib || true
lipo -create ${CURRENTPATH}/bin/iPhoneSimulator${SDKVERSION}-i386.sdk/libcurl.a ${CURRENTPATH}/bin/iPhoneSimulator${SDKVERSION}-x86_64.sdk/libcurl.a  ${CURRENTPATH}/bin/iPhoneOS${SDKVERSION}-armv7.sdk/libcurl.a ${CURRENTPATH}/bin/iPhoneOS${SDKVERSION}-armv7s.sdk/libcurl.a ${CURRENTPATH}/bin/iPhoneOS${SDKVERSION}-arm64.sdk/libcurl.a -output ${CURRENTPATH}/lib/libcurl.a

mkdir -p include/curl || true
cp -f curl-${CURL_VERSION}/include/curl/*.h include/curl/
sed -i '' $'s/\#define CURL_SIZEOF_LONG 8/\#ifdef __LP64__\\\n\#define CURL_SIZEOF_LONG 8\\\n#else\\\n\#define CURL_SIZEOF_LONG 4\\\n\#endif/' include/curl/curlbuild.h
sed -i '' $'s/\#define CURL_SIZEOF_CURL_OFF_T 8/\#ifdef __LP64__\\\n\#define CURL_SIZEOF_CURL_OFF_T 8\\\n#else\\\n\#define CURL_SIZEOF_CURL_OFF_T 4\\\n\#endif/' include/curl/curlbuild.h

rm -rf bin
rm -rf curl-${CURL_VERSION}


echo "Done."
