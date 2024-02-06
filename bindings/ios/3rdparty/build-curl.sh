#!/bin/bash

CURL_VERSION="8.1.2"
source common.sh

# Build libcurl for a specific architecture and platform
build_arch_platform() {
  ARCH="$1"
  PLATFORM="$2"
  CATALYST="${3:-false}"

  rm -rf "curl-${CURL_VERSION}"
  tar zxf "curl-${CURL_VERSION}.tar.gz"
  pushd "curl-${CURL_VERSION}"

  export BUILD_TOOLS="${DEVELOPER}"
  export BUILD_DEVROOT="${DEVELOPER}/Platforms/${PLATFORM}.platform/Developer"
  export BUILD_SDKROOT="${BUILD_DEVROOT}/SDKs/${PLATFORM}${SDKVERSION}.sdk"

  RUNTARGET=""
  PREFIX=""
  if [ "${CATALYST}" == "true" ]; then
    RUNTARGET="-target ${ARCH}-apple-ios15.0-macabi"
    PREFIX="${CURRENTPATH}/bin/libcurl/${PLATFORM}${SDKVERSION}-catalyst-${ARCH}.sdk"
  else
    PREFIX="${CURRENTPATH}/bin/libcurl/${PLATFORM}${SDKVERSION}-${ARCH}.sdk"
  fi
  
  if [ "${PLATFORM}" == "MacOSX" ]; then
    BUILD_SDKROOT="${BUILD_DEVROOT}/SDKs/${PLATFORM}.sdk"
  fi

  if [[ "${ARCH}" == "arm64" && "$PLATFORM" == "iPhoneSimulator" ]]; then
    RUNTARGET="-target ${ARCH}-apple-ios15.0-simulator"
  fi

  echo "${bold}Building CURL for $PLATFORM (catalyst=$CATALYST) $ARCH $BUILD_SDKROOT ${normal}"
  
  export CC="${BUILD_TOOLS}/usr/bin/gcc -arch ${ARCH}"
  
  mkdir -p "${PREFIX}"

  if [[ "${CATALYST}" == "true" || "${PLATFORM}" == "iPhoneOS" || "${PLATFORM}" == "iPhoneSimulator" ]]; then
    export LDFLAGS="-Os -arch ${ARCH} -Wl,-dead_strip -miphoneos-version-min=15.0"
    export CFLAGS="-Os -arch ${ARCH} -pipe -no-cpp-precomp -isysroot ${BUILD_SDKROOT} -miphoneos-version-min=15.0 ${RUNTARGET}"
    export CPPFLAGS="${CFLAGS} -DNDEBUG"
    export CXXFLAGS="${CPPFLAGS}"
  else #macOS
      export LDFLAGS="-Os -arch ${ARCH} -Wl,-dead_strip -mmacosx-version-min=10.15 -L${BUILD_SDKROOT}/usr/lib"
      export CFLAGS="-Os -arch ${ARCH} -pipe -no-cpp-precomp -isysroot ${BUILD_SDKROOT} -mmacosx-version-min=10.15"
      export CPPFLAGS="${CFLAGS} -I${BUILD_SDKROOT}/usr/include -DNDEBUG"
      export CXXFLAGS="${CPPFLAGS}"
  fi

  if [ "${ARCH}" == "arm64" ]; then
    HOST="arm-apple-darwin"
  else
    HOST="${ARCH}-apple-darwin"
  fi
  
  ./configure --prefix="${PREFIX}" --host=${HOST} --enable-static --disable-shared --with-secure-transport --with-zlib --disable-manual --disable-ftp --disable-file --disable-ldap --disable-ldaps --disable-rtsp --disable-dict --disable-telnet --disable-tftp --disable-pop3 --disable-imap --disable-smtp --disable-gopher --disable-sspi --enable-ipv6 --disable-smb --without-brotli --without-zstd

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
  
  mkdir -p "${CURRENTPATH}/bin/libcurl/catalyst"
  
  lipo -create "${CURRENTPATH}/bin/libcurl/MacOSX${SDKVERSION}-catalyst-x86_64.sdk/lib/libcurl.a" "${CURRENTPATH}/bin/libcurl/MacOSX${SDKVERSION}-catalyst-arm64.sdk/lib/libcurl.a" -output "${CURRENTPATH}/bin/libcurl/catalyst/libcurl.a"
}

# Build macOS targets for arm64 and x86_64
build_mac() {
  build_arch_platform "arm64" "MacOSX"
  build_arch_platform "x86_64" "MacOSX"
  
  echo "${bold}Lipo library for x86_64 and arm64 mac ${normal}"
  
  mkdir -p "${CURRENTPATH}/bin/libcurl/mac"
  
  lipo -create "${CURRENTPATH}/bin/libcurl/MacOSX${SDKVERSION}-x86_64.sdk/lib/libcurl.a" "${CURRENTPATH}/bin/libcurl/MacOSX${SDKVERSION}-arm64.sdk/lib/libcurl.a" -output "${CURRENTPATH}/bin/libcurl/mac/libcurl.a"
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
  
  mkdir -p "${CURRENTPATH}/bin/libcurl/iPhoneSimulator"
  
  lipo -create "${CURRENTPATH}/bin/libcurl/iPhoneSimulator${SDKVERSION}-x86_64.sdk/lib/libcurl.a" "${CURRENTPATH}/bin/libcurl/iPhoneSimulator${SDKVERSION}-arm64.sdk/lib/libcurl.a" -output "${CURRENTPATH}/bin/libcurl/iPhoneSimulator/libcurl.a"
}

create_XCFramework() {
  mkdir -p xcframework || true
  
  echo "${bold}Creating xcframework ${normal}"
  
  xcodebuild -create-xcframework \
    -library "${CURRENTPATH}/bin/libcurl/iPhoneSimulator/libcurl.a" \
    -headers "${CURRENTPATH}/bin/libcurl/iPhoneSimulator${SDKVERSION}-arm64.sdk/include" \
    -library "${CURRENTPATH}/bin/libcurl/iPhoneOS${SDKVERSION}-arm64.sdk/lib/libcurl.a" \
    -headers "${CURRENTPATH}/bin/libcurl/iPhoneOS${SDKVERSION}-arm64.sdk/include" \
    -library "${CURRENTPATH}/bin/libcurl/catalyst/libcurl.a" \
    -headers "${CURRENTPATH}/bin/libcurl/MacOSX${SDKVERSION}-catalyst-arm64.sdk/include" \
    -library "${CURRENTPATH}/bin/libcurl/mac/libcurl.a" \
    -headers "${CURRENTPATH}/bin/libcurl/MacOSX${SDKVERSION}-x86_64.sdk/include" \
    -output "${CURRENTPATH}/xcframework/libcurl.xcframework"
}

clean_up() {
  echo "${bold}Cleaning up ${normal}"

  rm -rf "curl-${CURL_VERSION}"
  rm -rf "curl-${CURL_VERSION}.tar.gz"
  rm -rf bin

  echo "${bold}Done.${normal}"
}

# Main build process
main() {
  check_xcode_path
  check_for_spaces

  if [ ! -e "curl-${CURL_VERSION}.tar.gz" ]; then
    curl -LO "https://curl.haxx.se/download/curl-${CURL_VERSION}.tar.gz"
  fi

  build_mac
  build_catalyst
  build_iOS
  build_iOS_simulator
  
  create_XCFramework
  clean_up
}

# Run the main build process
main


