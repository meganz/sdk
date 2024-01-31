#!/bin/sh

UV_VERSION="1.45.0"
source common.sh

# Build libuv for a specific architecture and platform
build_arch_platform() {
  ARCH="$1"
  PLATFORM="$2"

  rm -rf libuv-v${UV_VERSION}
  tar zxf libuv-v${UV_VERSION}.tar.gz
  pushd "libuv-v${UV_VERSION}"

  export BUILD_TOOLS="${DEVELOPER}"
  export BUILD_DEVROOT="${DEVELOPER}/Platforms/${PLATFORM}.platform/Developer"
  export BUILD_SDKROOT="${BUILD_DEVROOT}/SDKs/${PLATFORM}${SDKVERSION}.sdk"

  RUNTARGET=""
  if [[ "${ARCH}" == "arm64" && "$PLATFORM" == "iPhoneSimulator" ]]; then
    RUNTARGET="-target ${ARCH}-apple-ios15.0-simulator"
  elif [[ "$PLATFORM" == "MacOSX" ]]; then
    RUNTARGET="-target ${ARCH}-apple-ios15.0-macabi"
    BUILD_SDKROOT="${BUILD_DEVROOT}/SDKs/${PLATFORM}.sdk"
  fi

  echo "${bold}Building libuv for $PLATFORM $ARCH $BUILD_SDKROOT ${normal}"
  
  export CC="${BUILD_TOOLS}/usr/bin/gcc -arch ${ARCH}"
  mkdir -p "${CURRENTPATH}/bin/libuv/${PLATFORM}${SDKVERSION}-${ARCH}.sdk"

  export LDFLAGS="-Os -arch ${ARCH} -Wl,-dead_strip -miphoneos-version-min=15.0"
  export CFLAGS="-Os -arch ${ARCH} -pipe -no-cpp-precomp -isysroot ${BUILD_SDKROOT} -miphoneos-version-min=15.0 ${RUNTARGET}"
  export CPPFLAGS="${CFLAGS} -DNDEBUG"
  export CXXFLAGS="${CPPFLAGS}"
  
  sh autogen.sh
  
  if [ "${ARCH}" == "arm64" ]; then
    HOST="arm-apple-darwin"
  else
    HOST="${ARCH}-apple-darwin"
  fi
  
  ./configure --prefix="${CURRENTPATH}/bin/libuv/${PLATFORM}${SDKVERSION}-${ARCH}.sdk" --host=${HOST} --enable-static --disable-shared

  make -j${CORES}
  make install
  make clean

  popd
}

# Build Catalyst (macOS) targets for arm64 and x86_64
build_catalyst() {
  build_arch_platform "arm64" "MacOSX"
  build_arch_platform "x86_64" "MacOSX"
  
  echo "${bold}Lipo library for x86_64 and arm64 catalyst ${normal}"
  
  mkdir -p "${CURRENTPATH}/bin/libuv/catalyst"
  
  lipo -create "${CURRENTPATH}/bin/libuv/MacOSX${SDKVERSION}-x86_64.sdk/lib/libuv.a" "${CURRENTPATH}/bin/libuv/MacOSX${SDKVERSION}-arm64.sdk/lib/libuv.a" -output "${CURRENTPATH}/bin/libuv/catalyst/libuv.a"
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

  build_catalyst
  build_iOS
  build_iOS_simulator
  
  create_XCFramework
  clean_up
}

# Run the main build process
main
