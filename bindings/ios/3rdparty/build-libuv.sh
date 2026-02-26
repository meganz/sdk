#!/bin/sh

UV_VERSION="1.45.0"
source common.sh

# Build libuv for a specific architecture and platform
build_arch_platform() {
  ARCH="$1"
  PLATFORM="$2"
  CATALYST="${3:-false}"

  rm -rf libuv-v${UV_VERSION}
  tar zxf libuv-v${UV_VERSION}.tar.gz
  pushd "libuv-v${UV_VERSION}"

  export BUILD_TOOLS="${DEVELOPER}"
  export BUILD_DEVROOT="${DEVELOPER}/Platforms/${PLATFORM}.platform/Developer"
  export BUILD_SDKROOT="${BUILD_DEVROOT}/SDKs/${PLATFORM}${SDKVERSION}.sdk"

  RUNTARGET=""
  PREFIX=""
  if [ "${CATALYST}" == "true" ]; then
    RUNTARGET="-target ${ARCH}-apple-ios15.0-macabi"
    PREFIX="${CURRENTPATH}/bin/libuv/${PLATFORM}${SDKVERSION}-catalyst-${ARCH}.sdk"
  else
    PREFIX="${CURRENTPATH}/bin/libuv/${PLATFORM}${SDKVERSION}-${ARCH}.sdk"
  fi
  
  if [ "${PLATFORM}" == "MacOSX" ]; then
    BUILD_SDKROOT="${BUILD_DEVROOT}/SDKs/${PLATFORM}.sdk"
  fi

  if [[ "${ARCH}" == "arm64" && "$PLATFORM" == "iPhoneSimulator" ]]; then
    RUNTARGET="-target ${ARCH}-apple-ios15.0-simulator"
  fi

  echo "${bold}Building libuv for $PLATFORM (catalyst=$CATALYST) $ARCH $BUILD_SDKROOT ${normal}"
  
  export CC="${BUILD_TOOLS}/usr/bin/gcc -arch ${ARCH}"
    
  mkdir -p ${PREFIX}

  if [[ "${CATALYST}" == "true" || "${PLATFORM}" == "iPhoneOS" || "${PLATFORM}" == "iPhoneSimulator" ]]; then
    export LDFLAGS="-Os -arch ${ARCH} -Wl,-dead_strip -miphoneos-version-min=15.0"
    export CFLAGS="-Os -arch ${ARCH} -pipe -no-cpp-precomp -isysroot ${BUILD_SDKROOT} -miphoneos-version-min=15.0 ${RUNTARGET}"
    export CPPFLAGS="${CFLAGS} -DNDEBUG"
    export CXXFLAGS="${CPPFLAGS}"
  else # macOS
      export LDFLAGS="-Os -arch ${ARCH} -Wl,-dead_strip -mmacosx-version-min=10.15 -L${BUILD_SDKROOT}/usr/lib"
      export CFLAGS="-Os -arch ${ARCH} -pipe -no-cpp-precomp -isysroot ${BUILD_SDKROOT} -mmacosx-version-min=10.15 -DNDEBUG"
      export CPPFLAGS="${CFLAGS} -I${BUILD_SDKROOT}/usr/include"
      export CXXFLAGS="${CPPFLAGS}"
  fi
  
  sh autogen.sh
        
  if [ "${ARCH}" == "arm64" ]; then
    HOST="arm-apple-darwin"
  else
    HOST="${ARCH}-apple-darwin"
  fi
  
  ./configure --prefix=${PREFIX} --host=${HOST} --enable-static --disable-shared
  
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
  
  mkdir -p "${CURRENTPATH}/bin/libuv/catalyst"
  
  lipo -create "${CURRENTPATH}/bin/libuv/MacOSX${SDKVERSION}-catalyst-x86_64.sdk/lib/libuv.a" "${CURRENTPATH}/bin/libuv/MacOSX${SDKVERSION}-catalyst-arm64.sdk/lib/libuv.a" -output "${CURRENTPATH}/bin/libuv/catalyst/libuv.a"
}

# Build macOS targets for arm64 and x86_64
build_mac() {
  build_arch_platform "arm64" "MacOSX"
  build_arch_platform "x86_64" "MacOSX"
  
  echo "${bold}Lipo library for x86_64 and arm64 mac ${normal}"
  
  mkdir -p "${CURRENTPATH}/bin/libuv/mac"
  
  lipo -create "${CURRENTPATH}/bin/libuv/MacOSX${SDKVERSION}-x86_64.sdk/lib/libuv.a" "${CURRENTPATH}/bin/libuv/MacOSX${SDKVERSION}-arm64.sdk/lib/libuv.a" -output "${CURRENTPATH}/bin/libuv/mac/libuv.a"
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
  
  mkdir -p "${CURRENTPATH}/bin/libuv/iPhoneSimulator"
  
  lipo -create "${CURRENTPATH}/bin/libuv/iPhoneSimulator${SDKVERSION}-x86_64.sdk/lib/libuv.a" "${CURRENTPATH}/bin/libuv/iPhoneSimulator${SDKVERSION}-arm64.sdk/lib/libuv.a" -output "${CURRENTPATH}/bin/libuv/iPhoneSimulator/libuv.a"
}

create_XCFramework() {
  mkdir -p xcframework || true
  
  echo "${bold}Creating xcframework ${normal}"
  
  xcodebuild -create-xcframework \
    -library "${CURRENTPATH}/bin/libuv/iPhoneSimulator/libuv.a" \
    -headers "${CURRENTPATH}/bin/libuv/iPhoneSimulator${SDKVERSION}-arm64.sdk/include" \
    -library "${CURRENTPATH}/bin/libuv/iPhoneOS${SDKVERSION}-arm64.sdk/lib/libuv.a" \
    -headers "${CURRENTPATH}/bin/libuv/iPhoneOS${SDKVERSION}-arm64.sdk/include" \
    -library "${CURRENTPATH}/bin/libuv/catalyst/libuv.a" \
    -headers "${CURRENTPATH}/bin/libuv/MacOSX${SDKVERSION}-catalyst-arm64.sdk/include" \
    -library "${CURRENTPATH}/bin/libuv/mac/libuv.a" \
    -headers "${CURRENTPATH}/bin/libuv/MacOSX${SDKVERSION}-arm64.sdk/include" \
    -output "${CURRENTPATH}/xcframework/libuv.xcframework"
}

clean_up() {
  echo "${bold}Cleaning up ${normal}"

  rm -rf bin
  rm -rf libuv-v${UV_VERSION}
  rm -rf libuv-v${UV_VERSION}.tar.gz

  echo "${bold}Done.${normal}"
}

download_libuv() {
  if [ ! -e "libuv-v${UV_VERSION}.tar.gz" ]; then
    curl -LO "http://dist.libuv.org/dist/v${UV_VERSION}/libuv-v${UV_VERSION}.tar.gz"
  fi
}

# Main build process
main() {
  check_xcode_path
  check_for_spaces
  
  download_libuv

  build_mac
  build_catalyst
  build_iOS
  build_iOS_simulator
  
  create_XCFramework
  clean_up
}

# Run the main build process
main
