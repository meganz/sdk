#!/bin/sh

ZENLIB_VERSION="6694a744d82d942c4a410f25f916561270381889"
MEDIAINFO_VERSION="4ee7f77c087b29055f48d539cd679de8de6f9c48"
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

if [ ! -e "${ZENLIB_VERSION}.tar.gz" ]
then
curl -LO "https://github.com/MediaArea/ZenLib/archive/${ZENLIB_VERSION}.tar.gz"
fi

if [ ! -e "${MEDIAINFO_VERSION}.tar.gz" ]
then
curl -LO "https://github.com/meganz/MediaInfoLib/archive/${MEDIAINFO_VERSION}.tar.gz"
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

rm -rf ZenLib-${ZENLIB_VERSION}
tar zxf ${ZENLIB_VERSION}.tar.gz
pushd "ZenLib-${ZENLIB_VERSION}/Project/GNU/Library"

echo "${bold}Building ZenLib for $PLATFORM $ARCH ${normal}"

export BUILD_TOOLS="${DEVELOPER}"
export BUILD_DEVROOT="${DEVELOPER}/Platforms/${PLATFORM}.platform/Developer"
export BUILD_SDKROOT="${BUILD_DEVROOT}/SDKs/${PLATFORM}${SDKVERSION}.sdk"

RUNTARGET=""
if [[ "${ARCH}" == "arm64"  && "$PLATFORM" == "iPhoneSimulator" ]];
then
RUNTARGET="-target ${ARCH}-apple-ios14.0-simulator"
fi

export CC="${BUILD_TOOLS}/usr/bin/gcc -arch ${ARCH}"
mkdir -p "${CURRENTPATH}/bin/zenlib/${PLATFORM}${SDKVERSION}-${ARCH}.sdk"
mkdir -p "${CURRENTPATH}/bin/mediainfo/${PLATFORM}${SDKVERSION}-${ARCH}.sdk"

# Build
export LDFLAGS="-Os -arch ${ARCH} -Wl,-dead_strip -miphoneos-version-min=14.0"
export CFLAGS="-Os -arch ${ARCH} -pipe -no-cpp-precomp -isysroot ${BUILD_SDKROOT} -miphoneos-version-min=14.0 -DMEDIAINFO_ADVANCED_NO ${RUNTARGET}"
export CPPFLAGS="${CFLAGS} -I${BUILD_SDKROOT}/usr/include -DNDEBUG"
export CXXFLAGS="${CPPFLAGS}"

sh autogen.sh

if [ "${ARCH}" == "arm64" ]; then
./configure --prefix="${CURRENTPATH}/bin/zenlib/${PLATFORM}${SDKVERSION}-${ARCH}.sdk" --host=arm-apple-darwin --disable-shared --disable-archive
else
./configure --prefix="${CURRENTPATH}/bin/zenlib/${PLATFORM}${SDKVERSION}-${ARCH}.sdk" --host=${ARCH}-apple-darwin --disable-shared --disable-archive
fi

make -j${CORES}
make install

popd

rm -rf ZenLib
mv ZenLib-${ZENLIB_VERSION} ZenLib

rm -rf MediaInfoLib-${MEDIAINFO_VERSION}
tar zxf ${MEDIAINFO_VERSION}.tar.gz
pushd "MediaInfoLib-${MEDIAINFO_VERSION}/Project/GNU/Library"

echo "${bold}Building mediainfo for $PLATFORM $ARCH${normal}"

sh autogen.sh

if [ "${ARCH}" == "arm64" ]; then
./configure --prefix="${CURRENTPATH}/bin/mediainfo/${PLATFORM}${SDKVERSION}-${ARCH}.sdk" --host=arm-apple-darwin --disable-shared --enable-minimize-size --enable-minimal --disable-archive --disable-image --disable-tag --disable-text --disable-swf --disable-flv --disable-hdsf4m --disable-cdxa --disable-dpg --disable-pmp --disable-rm --disable-wtv --disable-mxf --disable-dcp --disable-aaf --disable-bdav --disable-bdmv --disable-dvdv --disable-gxf --disable-mixml --disable-skm --disable-nut --disable-tsp --disable-hls --disable-dxw --disable-dvdif --disable-dashmpd --disable-aic --disable-avsv --disable-canopus --disable-ffv1 --disable-flic --disable-huffyuv --disable-prores --disable-y4m --disable-adpcm --disable-amr --disable-amv --disable-ape --disable-au --disable-la --disable-celt --disable-midi --disable-mpc --disable-openmg --disable-pcm --disable-ps2a --disable-rkau --disable-speex --disable-tak --disable-tta --disable-twinvq --disable-references
else
./configure --prefix="${CURRENTPATH}/bin/mediainfo/${PLATFORM}${SDKVERSION}-${ARCH}.sdk" --host=${ARCH}-apple-darwin --disable-shared --enable-minimize-size --enable-minimal --disable-archive --disable-image --disable-tag --disable-text --disable-swf --disable-flv --disable-hdsf4m --disable-cdxa --disable-dpg --disable-pmp --disable-rm --disable-wtv --disable-mxf --disable-dcp --disable-aaf --disable-bdav --disable-bdmv --disable-dvdv --disable-gxf --disable-mixml --disable-skm --disable-nut --disable-tsp --disable-hls --disable-dxw --disable-dvdif --disable-dashmpd --disable-aic --disable-avsv --disable-canopus --disable-ffv1 --disable-flic --disable-huffyuv --disable-prores --disable-y4m --disable-adpcm --disable-amr --disable-amv --disable-ape --disable-au --disable-la --disable-celt --disable-midi --disable-mpc --disable-openmg --disable-pcm --disable-ps2a --disable-rkau --disable-speex --disable-tak --disable-tta --disable-twinvq --disable-references
fi

make -j${CORES}
make install
make clean

popd

done

mkdir xcframework || true

echo "${bold}Lipo library for x86_64 and arm64 simulators for zenlib ${normal}"

lipo -create ${CURRENTPATH}/bin/zenlib/iPhoneSimulator${SDKVERSION}-x86_64.sdk/lib/libzen.a ${CURRENTPATH}/bin/zenlib/iPhoneSimulator${SDKVERSION}-arm64.sdk/lib/libzen.a -output ${CURRENTPATH}/bin/zenlib/libzen.a

echo "${bold}Creating libzen xcframework ${normal}"

xcodebuild -create-xcframework -library ${CURRENTPATH}/bin/zenlib/libzen.a -headers ${CURRENTPATH}/bin/zenlib/iPhoneSimulator${SDKVERSION}-arm64.sdk/include -library  ${CURRENTPATH}/bin/zenlib/iPhoneOS${SDKVERSION}-arm64.sdk/lib/libzen.a -headers ${CURRENTPATH}/bin/zenlib/iPhoneOS${SDKVERSION}-arm64.sdk/include -output ${CURRENTPATH}/xcframework/libzen.xcframework

echo "${bold}Lipo library for x86_64 and arm64 simulators for mediainfo ${normal}"

lipo -create ${CURRENTPATH}/bin/mediainfo/iPhoneSimulator${SDKVERSION}-x86_64.sdk/lib/libmediainfo.a ${CURRENTPATH}/bin/mediainfo/iPhoneSimulator${SDKVERSION}-arm64.sdk/lib/libmediainfo.a -output ${CURRENTPATH}/bin/mediainfo/libmediainfo.a

echo "${bold}Creating libmediainfo xcframework ${normal}"

xcodebuild -create-xcframework -library ${CURRENTPATH}/bin/mediainfo/libmediainfo.a -headers ${CURRENTPATH}/bin/mediainfo/iPhoneSimulator${SDKVERSION}-arm64.sdk/include -library  ${CURRENTPATH}/bin/mediainfo/iPhoneOS${SDKVERSION}-arm64.sdk/lib/libmediainfo.a -headers ${CURRENTPATH}/bin/mediainfo/iPhoneOS${SDKVERSION}-arm64.sdk/include -output ${CURRENTPATH}/xcframework/libmediainfo.xcframework
 
echo "${bold}Cleaning up ${normal}"

rm -rf bin
rm -rf MediaInfoLib-${MEDIAINFO_VERSION}
rm -rf ZenLib

echo "${bold}Done.${normal}"

