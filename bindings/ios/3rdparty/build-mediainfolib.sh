#!/bin/sh

ZENLIB_VERSION="6694a744d82d942c4a410f25f916561270381889"
MEDIAINFO_VERSION="4ee7f77c087b29055f48d539cd679de8de6f9c48"
source common.sh

# Build zenlib and mediainfo for a specific architecture and platform
build_arch_platform() {
  ARCH="$1"
  PLATFORM="$2"
  CATALYST="${3:-false}"

  rm -rf ZenLib-${ZENLIB_VERSION}
  tar zxf ${ZENLIB_VERSION}.tar.gz
  pushd "ZenLib-${ZENLIB_VERSION}/Project/GNU/Library"

  export BUILD_TOOLS="${DEVELOPER}"
  export BUILD_DEVROOT="${DEVELOPER}/Platforms/${PLATFORM}.platform/Developer"
  export BUILD_SDKROOT="${BUILD_DEVROOT}/SDKs/${PLATFORM}${SDKVERSION}.sdk"

  RUNTARGET=""
  PREFIX_MEDIAINFO=""
  PREFIX_ZENLIB=""
  if [ "${CATALYST}" == "true" ]; then
    RUNTARGET="-target ${ARCH}-apple-ios15.0-macabi"
    PREFIX_ZENLIB="${CURRENTPATH}/bin/zenlib/${PLATFORM}${SDKVERSION}-catalyst-${ARCH}.sdk"
    PREFIX_MEDIAINFO="${CURRENTPATH}/bin/mediainfo/${PLATFORM}${SDKVERSION}-catalyst-${ARCH}.sdk"
  else
    PREFIX_ZENLIB="${CURRENTPATH}/bin/zenlib/${PLATFORM}${SDKVERSION}-${ARCH}.sdk"
    PREFIX_MEDIAINFO="${CURRENTPATH}/bin/mediainfo/${PLATFORM}${SDKVERSION}-${ARCH}.sdk"
  fi
  
  if [ "${PLATFORM}" == "MacOSX" ]; then
    BUILD_SDKROOT="${BUILD_DEVROOT}/SDKs/${PLATFORM}.sdk"
  fi

  if [[ "${ARCH}" == "arm64" && "$PLATFORM" == "iPhoneSimulator" ]]; then
    RUNTARGET="-target ${ARCH}-apple-ios15.0-simulator"
  fi

  echo "${bold}Building zenlib for $PLATFORM (catalyst=$CATALYST) $ARCH $BUILD_SDKROOT ${normal}"
  
  export CC="${BUILD_TOOLS}/usr/bin/gcc -arch ${ARCH}"
  
  mkdir -p ${PREFIX_ZENLIB}
  mkdir -p ${PREFIX_MEDIAINFO}
  
  # Build
  if [[ "${CATALYST}" == "true" || "${PLATFORM}" == "iPhoneOS" || "${PLATFORM}" == "iPhoneSimulator" ]]; then
    export LDFLAGS="-Os -arch ${ARCH} -Wl,-dead_strip -miphoneos-version-min=15.0"
    export CFLAGS="-Os -arch ${ARCH} -pipe -no-cpp-precomp -isysroot ${BUILD_SDKROOT} -miphoneos-version-min=15.0 -DMEDIAINFO_ADVANCED_NO ${RUNTARGET}"
    export CPPFLAGS="${CFLAGS} -I${BUILD_SDKROOT}/usr/include -DNDEBUG"
    export CXXFLAGS="${CPPFLAGS}"
  else # macOS
      export LDFLAGS="-Os -arch ${ARCH} -Wl,-dead_strip -mmacosx-version-min=10.15 -L${BUILD_SDKROOT}/usr/lib"
      export CFLAGS="-Os -arch ${ARCH} -pipe -no-cpp-precomp -isysroot ${BUILD_SDKROOT} -mmacosx-version-min=10.15 -DMEDIAINFO_ADVANCED_NO"
      export CPPFLAGS="${CFLAGS} -I${BUILD_SDKROOT}/usr/include -DNDEBUG"
      export CXXFLAGS="${CPPFLAGS}"
  fi
  
  sh autogen.sh

  if [ "${ARCH}" == "arm64" ]; then
    HOST="arm-apple-darwin"
  else
    HOST="${ARCH}-apple-darwin"
  fi
  
  ./configure --prefix=${PREFIX_ZENLIB} --host=${HOST} --disable-shared --disable-archive

  make -j${CORES}
  make install
  
  popd
  
  rm -rf ZenLib
  mv ZenLib-${ZENLIB_VERSION} ZenLib
  
  rm -rf MediaInfoLib-${MEDIAINFO_VERSION}
  tar zxf ${MEDIAINFO_VERSION}.tar.gz
  pushd "MediaInfoLib-${MEDIAINFO_VERSION}/Project/GNU/Library"
  
  echo "${bold}Building mediainfo for $PLATFORM (catalyst=$CATALYST) $ARCH $BUILD_SDKROOT ${normal}"
  
  sh autogen.sh
    
  ./configure --prefix=${PREFIX_MEDIAINFO} \
  --host=${HOST} --disable-shared --enable-minimize-size --enable-minimal --disable-archive \
  --disable-image --disable-tag --disable-text --disable-swf --disable-flv --disable-hdsf4m \
  --disable-cdxa --disable-dpg --disable-pmp --disable-rm --disable-wtv --disable-mxf \
  --disable-dcp --disable-aaf --disable-bdav --disable-bdmv --disable-dvdv --disable-gxf \
  --disable-mixml --disable-skm --disable-nut --disable-tsp --disable-hls --disable-dxw \
  --disable-dvdif --disable-dashmpd --disable-aic --disable-avsv --disable-canopus \
  --disable-ffv1 --disable-flic --disable-huffyuv --disable-prores --disable-y4m \
  --disable-adpcm --disable-amr --disable-amv --disable-ape --disable-au --disable-la \
  --disable-celt --disable-midi --disable-mpc --disable-openmg --disable-pcm --disable-ps2a \
  --disable-rkau --disable-speex --disable-tak --disable-tta --disable-twinvq --disable-references
  
  make -j${CORES}
  make install
  make clean
  
  popd
}

# Build Catalyst (macOS) targets for arm64 and x86_64
build_catalyst() {
  build_arch_platform "arm64" "MacOSX" true
  build_arch_platform "x86_64" "MacOSX" true
  
  echo "${bold}Lipo library for x86_64 and arm64 catalyst ${normal}"
  
  mkdir -p "${CURRENTPATH}/bin/zenlib/catalyst"
  mkdir -p "${CURRENTPATH}/bin/mediainfo/catalyst"
  
  lipo -create "${CURRENTPATH}/bin/zenlib/MacOSX${SDKVERSION}-catalyst-x86_64.sdk/lib/libzen.a" \
    "${CURRENTPATH}/bin/zenlib/MacOSX${SDKVERSION}-catalyst-arm64.sdk/lib/libzen.a" \
    -output "${CURRENTPATH}/bin/zenlib/catalyst/libzen.a"
  
  lipo -create "${CURRENTPATH}/bin/mediainfo/MacOSX${SDKVERSION}-catalyst-x86_64.sdk/lib/libmediainfo.a" \
    "${CURRENTPATH}/bin/mediainfo/MacOSX${SDKVERSION}-catalyst-arm64.sdk/lib/libmediainfo.a" \
    -output "${CURRENTPATH}/bin/mediainfo/catalyst/libmediainfo.a"
}

# Build macOS targets for arm64 and x86_64
build_mac() {
  build_arch_platform "arm64" "MacOSX"
  build_arch_platform "x86_64" "MacOSX"
  
  echo "${bold}Lipo library for x86_64 and arm64 mac ${normal}"
    
  mkdir -p "${CURRENTPATH}/bin/zenlib/mac"
  mkdir -p "${CURRENTPATH}/bin/mediainfo/mac"
  
  lipo -create "${CURRENTPATH}/bin/zenlib/MacOSX${SDKVERSION}-x86_64.sdk/lib/libzen.a" \
    "${CURRENTPATH}/bin/zenlib/MacOSX${SDKVERSION}-arm64.sdk/lib/libzen.a" \
    -output "${CURRENTPATH}/bin/zenlib/mac/libzen.a"
  
  lipo -create "${CURRENTPATH}/bin/mediainfo/MacOSX${SDKVERSION}-x86_64.sdk/lib/libmediainfo.a" \
    "${CURRENTPATH}/bin/mediainfo/MacOSX${SDKVERSION}-arm64.sdk/lib/libmediainfo.a" \
    -output "${CURRENTPATH}/bin/mediainfo/mac/libmediainfo.a"
}

# Build iOS target for arm64
build_iOS() {
  build_arch_platform "arm64" "iPhoneOS"
}

# Build iOS Simulator targets for arm64 and x86_64
build_iOS_simulator() {
  build_arch_platform "arm64" "iPhoneSimulator"
  build_arch_platform "x86_64" "iPhoneSimulator"
  
  echo "${bold}Lipo library for x86_64 and arm64 simulators ${normal}"
  
  mkdir -p "${CURRENTPATH}/bin/zenlib/iPhoneSimulator"
  mkdir -p "${CURRENTPATH}/bin/mediainfo/iPhoneSimulator"
  
  lipo -create "${CURRENTPATH}/bin/zenlib/iPhoneSimulator${SDKVERSION}-x86_64.sdk/lib/libzen.a" \
    "${CURRENTPATH}/bin/zenlib/iPhoneSimulator${SDKVERSION}-arm64.sdk/lib/libzen.a" \
    -output "${CURRENTPATH}/bin/zenlib/iPhoneSimulator/libzen.a"
  
  lipo -create "${CURRENTPATH}/bin/mediainfo/iPhoneSimulator${SDKVERSION}-x86_64.sdk/lib/libmediainfo.a" \
    "${CURRENTPATH}/bin/mediainfo/iPhoneSimulator${SDKVERSION}-arm64.sdk/lib/libmediainfo.a" \
    -output "${CURRENTPATH}/bin/mediainfo/iPhoneSimulator/libmediainfo.a"
}

create_XCFramework() {
  mkdir -p xcframework || true
  
  echo "${bold}Creating xcframework ${normal}"
  
  xcodebuild -create-xcframework \
    -library "${CURRENTPATH}/bin/zenlib/iPhoneSimulator/libzen.a" \
    -headers "${CURRENTPATH}/bin/zenlib/iPhoneSimulator${SDKVERSION}-arm64.sdk/include" \
    -library "${CURRENTPATH}/bin/zenlib/iPhoneOS${SDKVERSION}-arm64.sdk/lib/libzen.a" \
    -headers "${CURRENTPATH}/bin/zenlib/iPhoneOS${SDKVERSION}-arm64.sdk/include" \
    -library "${CURRENTPATH}/bin/zenlib/catalyst/libzen.a" \
    -headers "${CURRENTPATH}/bin/zenlib/MacOSX${SDKVERSION}-catalyst-arm64.sdk/include" \
    -library "${CURRENTPATH}/bin/zenlib/mac/libzen.a" \
    -headers "${CURRENTPATH}/bin/zenlib/MacOSX${SDKVERSION}-arm64.sdk/include" \
    -output "${CURRENTPATH}/xcframework/libzen.xcframework"
      
  
  xcodebuild -create-xcframework \
    -library "${CURRENTPATH}/bin/mediainfo/iPhoneSimulator/libmediainfo.a" \
    -headers "${CURRENTPATH}/bin/mediainfo/iPhoneSimulator${SDKVERSION}-arm64.sdk/include" \
    -library "${CURRENTPATH}/bin/mediainfo/iPhoneOS${SDKVERSION}-arm64.sdk/lib/libmediainfo.a" \
    -headers "${CURRENTPATH}/bin/mediainfo/iPhoneOS${SDKVERSION}-arm64.sdk/include" \
    -library "${CURRENTPATH}/bin/mediainfo/catalyst/libmediainfo.a" \
    -headers "${CURRENTPATH}/bin/mediainfo/MacOSX${SDKVERSION}-catalyst-arm64.sdk/include" \
    -library "${CURRENTPATH}/bin/mediainfo/mac/libmediainfo.a" \
    -headers "${CURRENTPATH}/bin/mediainfo/MacOSX${SDKVERSION}-arm64.sdk/include" \
    -output "${CURRENTPATH}/xcframework/libmediainfo.xcframework"
}

clean_up() {
  echo "${bold}Cleaning up ${normal}"

  rm -rf bin
  rm -rf MediaInfoLib-${MEDIAINFO_VERSION}
  rm -rf ZenLib
  rm -rf ${ZENLIB_VERSION}.tar.gz
  rm -rf ${MEDIAINFO_VERSION}.tar.gz

  echo "${bold}Done.${normal}"
}

download_zenlib() {
  if [ ! -e "${ZENLIB_VERSION}.tar.gz" ]; then
    curl -LO "https://github.com/MediaArea/ZenLib/archive/${ZENLIB_VERSION}.tar.gz"
  fi
}

download_mediainfo() {
  if [ ! -e "${MEDIAINFO_VERSION}.tar.gz" ]; then
    curl -LO "https://github.com/meganz/MediaInfoLib/archive/${MEDIAINFO_VERSION}.tar.gz"
  fi
}

# Main build process
main() {
  check_xcode_path
  check_for_spaces
  
  download_zenlib
  download_mediainfo

  build_mac
  build_catalyst
  build_iOS
  build_iOS_simulator
  
  create_XCFramework
  clean_up
}

# Run the main build process
main

