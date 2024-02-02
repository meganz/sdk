#!/bin/bash

CURL_VERSION="8.1.2"
source common.sh

# Build libcurl for a specific architecture and platform
build_arch_platform() {
  ARCH="$1"
  PLATFORM="$2"

  rm -rf "curl-${CURL_VERSION}"
  tar zxf "curl-${CURL_VERSION}.tar.gz"
  pushd "curl-${CURL_VERSION}"

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

  echo "${bold}Building CURL for $PLATFORM $ARCH $BUILD_SDKROOT ${normal}"
  
  export CC="${BUILD_TOOLS}/usr/bin/gcc -arch ${ARCH}"
  mkdir -p "${CURRENTPATH}/bin/libcurl/${PLATFORM}${SDKVERSION}-${ARCH}.sdk"

  export LDFLAGS="-Os -arch ${ARCH} -Wl,-dead_strip -miphoneos-version-min=15.0"
  export CFLAGS="-Os -arch ${ARCH} -pipe -no-cpp-precomp -isysroot ${BUILD_SDKROOT} -miphoneos-version-min=15.0 ${RUNTARGET}"
  export CPPFLAGS="${CFLAGS} -DNDEBUG"
  export CXXFLAGS="${CPPFLAGS}"

  if [ "${ARCH}" == "arm64" ]; then
    HOST="arm-apple-darwin"
  else
    HOST="${ARCH}-apple-darwin"
  fi
  
  ./configure --prefix="${CURRENTPATH}/bin/libcurl/${PLATFORM}${SDKVERSION}-${ARCH}.sdk" --host=${HOST} --enable-static --disable-shared --with-secure-transport --with-zlib --disable-manual --disable-ftp --disable-file --disable-ldap --disable-ldaps --disable-rtsp --disable-dict --disable-telnet --disable-tftp --disable-pop3 --disable-imap --disable-smtp --disable-gopher --disable-sspi --enable-ipv6 --disable-smb

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
  
  mkdir -p "${CURRENTPATH}/bin/libcurl/catalyst"
  
  lipo -create "${CURRENTPATH}/bin/libcurl/MacOSX${SDKVERSION}-x86_64.sdk/lib/libcurl.a" "${CURRENTPATH}/bin/libcurl/MacOSX${SDKVERSION}-arm64.sdk/lib/libcurl.a" -output "${CURRENTPATH}/bin/libcurl/catalyst/libcurl.a"
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
    -headers "${CURRENTPATH}/bin/libcurl/MacOSX${SDKVERSION}-arm64.sdk/include" \
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

  build_catalyst
  build_iOS
  build_iOS_simulator
  
  create_XCFramework
  clean_up
}

# Run the main build process
main


