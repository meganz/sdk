#!/bin/sh

LIBSODIUM_VERSION="1.0.16"
source common.sh

# Build libsodium for a specific architecture and platform
build_arch_platform() {
  ARCH="$1"
  PLATFORM="$2"
  
  rm -rf "libsodium-${LIBSODIUM_VERSION}"
  tar zxf "libsodium-${LIBSODIUM_VERSION}.tar.gz"
  pushd "libsodium-${LIBSODIUM_VERSION}"

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

  echo "${bold}Building libsodium for $PLATFORM $ARCH $BUILD_SDKROOT ${normal}"
  
  export CC="${BUILD_TOOLS}/usr/bin/gcc -arch ${ARCH}"
  mkdir -p "${CURRENTPATH}/bin/libsodium/${PLATFORM}${SDKVERSION}-${ARCH}.sdk"

  export LDFLAGS="-Os -arch ${ARCH} -Wl,-dead_strip -miphoneos-version-min=15.0 -L${BUILD_SDKROOT}/usr/lib"
  export CFLAGS="-Os -arch ${ARCH} -pipe -no-cpp-precomp -isysroot ${BUILD_SDKROOT} -miphoneos-version-min=15.0 -DNDEBUG ${RUNTARGET}"
  export CPPFLAGS="${CFLAGS} -I${BUILD_SDKROOT}/usr/include"
  export CXXFLAGS="${CPPFLAGS}"

  if [ "${ARCH}" == "arm64" ]; then
    HOST=arm-apple-darwin
  else
    HOST=${ARCH}-apple-darwin
  fi
  
  ./configure --prefix="${CURRENTPATH}/bin/libsodium/${PLATFORM}${SDKVERSION}-${ARCH}.sdk" --host=${HOST} --disable-shared --enable-minimal

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
  
  mkdir -p "${CURRENTPATH}/bin/libsodium/catalyst"
  
  lipo -create "${CURRENTPATH}/bin/libsodium/MacOSX${SDKVERSION}-x86_64.sdk/lib/libsodium.a" "${CURRENTPATH}/bin/libsodium/MacOSX${SDKVERSION}-arm64.sdk/lib/libsodium.a" -output "${CURRENTPATH}/bin/libsodium/catalyst/libsodium.a"
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
  
  mkdir -p "${CURRENTPATH}/bin/libsodium/iPhoneSimulator"
  
  lipo -create "${CURRENTPATH}/bin/libsodium/iPhoneSimulator${SDKVERSION}-x86_64.sdk/lib/libsodium.a" "${CURRENTPATH}/bin/libsodium/iPhoneSimulator${SDKVERSION}-arm64.sdk/lib/libsodium.a" -output "${CURRENTPATH}/bin/libsodium/iPhoneSimulator/libsodium.a"
}

create_XCFramework() {
  mkdir -p xcframework || true
  
  echo "${bold}Creating xcframework ${normal}"
  
  xcodebuild -create-xcframework \
    -library "${CURRENTPATH}/bin/libsodium/iPhoneSimulator/libsodium.a" \
    -headers "${CURRENTPATH}/bin/libsodium/iPhoneSimulator${SDKVERSION}-arm64.sdk/include" \
    -library "${CURRENTPATH}/bin/libsodium/iPhoneOS${SDKVERSION}-arm64.sdk/lib/libsodium.a" \
    -headers "${CURRENTPATH}/bin/libsodium/iPhoneOS${SDKVERSION}-arm64.sdk/include" \
    -library "${CURRENTPATH}/bin/libsodium/catalyst/libsodium.a" \
    -headers "${CURRENTPATH}/bin/libsodium/MacOSX${SDKVERSION}-arm64.sdk/include" \
    -output "${CURRENTPATH}/xcframework/libsodium.xcframework"
}

clean_up() {
  echo "${bold}Cleaning up ${normal}"

  rm -rf bin
  rm -rf "libsodium-${LIBSODIUM_VERSION}"
  rm -rf "libsodium-${LIBSODIUM_VERSION}.tar.gz"

  echo "${bold}Done.${normal}"
}

# Main build process
main() {
  check_xcode_path
  check_for_spaces
  
  if [ ! -e "libsodium-${LIBSODIUM_VERSION}.tar.gz" ]; then
    curl -LO "https://github.com/jedisct1/libsodium/releases/download/${LIBSODIUM_VERSION}/libsodium-${LIBSODIUM_VERSION}.tar.gz"
  fi

  build_catalyst
  build_iOS
  build_iOS_simulator
  
  create_XCFramework
  clean_up
}

# Run the main build process
main
