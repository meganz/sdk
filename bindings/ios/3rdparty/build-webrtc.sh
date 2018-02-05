#!/bin/sh

MEGACHAT_VERSION="GIT"
SDKVERSION=`xcrun -sdk iphoneos --show-sdk-version`

##############################################
CURRENTPATH=`pwd`
OPENSSL_PREFIX="${CURRENTPATH}"
ARCHS="ia32 x64 arm arm64"
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

export WEBRTC_DEPS_INCLUDE=${CURRENTPATH}/include
export WEBRTC_DEPS_LIB=${CURRENTPATH}/lib

for ARCH in ${ARCHS}
do

echo "BUILDING FOR ${ARCH}"
rm -rf karere-native/webrtc-build/ios/src karere-native/webrtc-build/ios/depot_tools karere-native/webrtc-build/ios/.gclient

karere-native/webrtc-build/build-webrtc.sh ${CURRENTPATH}/karere-native/webrtc-build/ios --platform ios --arch ${ARCH} --batch

mkdir -p "${CURRENTPATH}/bin/${ARCH}"
if [[ ${ARCH} == arm* ]]; then
    libtool -static -o ${CURRENTPATH}/bin/${ARCH}/libWebRTC.a ${CURRENTPATH}/karere-native/webrtc-build/ios/src/out/Release-iphoneos/*.a
else
    libtool -static -o ${CURRENTPATH}/bin/${ARCH}/libWebRTC.a ${CURRENTPATH}/karere-native/webrtc-build/ios/src/out/Release-iphonesimulator/*.a
fi

done


mkdir lib || true
lipo -create ${CURRENTPATH}/bin/ia32/libWebRTC.a ${CURRENTPATH}/bin/x64/libWebRTC.a ${CURRENTPATH}/bin/arm/libWebRTC.a ${CURRENTPATH}/bin/arm64/libWebRTC.a -output ${CURRENTPATH}/lib/libWebRTC.a

mkdir -p include || true
cp -R karere-native/webrtc-build/ios/src/webrtc include/
find include/webrtc -iname "*.c*" -exec rm {} \;
find include/webrtc -iname "*.gyp*" -exec rm {} \;
find include/webrtc -iname "*.gn" -exec rm {} \;
find include/webrtc -iname "*.py" -exec rm {} \;
find include/webrtc -iname "*.mm" -exec rm {} \;

rm -rf karere-native/webrtc-build/ios/src karere-native/webrtc-build/ios/depot_tools karere-native/webrtc-build/ios/.gclient
rm -rf bin
echo "Done."

