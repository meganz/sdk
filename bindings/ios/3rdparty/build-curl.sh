#!/bin/bash

CURL_VERSION="8.1.2"
SDKVERSION=$(xcrun -sdk iphoneos --show-sdk-version)

##############################################
CURRENTPATH=$(pwd)
DEVELOPER=$(xcode-select -print-path)

CORES=$(sysctl -n hw.ncpu)

# Formating
green="\033[32m"
bold="\033[0m${green}\033[1m"
normal="\033[0m"

# Function to print error messages and exit
print_error() {
  echo -e "\033[31mError: $1\033[0m" >&2
  exit 1
}

# Check if Xcode path is correctly set
check_xcode_path() {
  if [ ! -d "$DEVELOPER" ]; then
    print_error "Xcode path is not set correctly: $DEVELOPER does not exist."
  fi
}

# Check for spaces in paths
check_for_spaces() {
  if [[ "$DEVELOPER" == *" "* || "$CURRENTPATH" == *" "* ]]; then
    print_error "Paths with spaces are not supported."
  fi
}

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
    ./configure --prefix="${CURRENTPATH}/bin/libcurl/${PLATFORM}${SDKVERSION}-${ARCH}.sdk" --host=arm-apple-darwin --enable-static --disable-shared --with-secure-transport --with-zlib --disable-manual --disable-ftp --disable-file --disable-ldap --disable-ldaps --disable-rtsp --disable-dict --disable-telnet --disable-tftp --disable-pop3 --disable-imap --disable-smtp --disable-gopher --disable-sspi --enable-ipv6 --disable-smb
  else
    ./configure --prefix="${CURRENTPATH}/bin/libcurl/${PLATFORM}${SDKVERSION}-${ARCH}.sdk" --host=${ARCH}-apple-darwin --enable-static --disable-shared --with-secure-transport --with-zlib --disable-manual --disable-ftp --disable-file --disable-ldap --disable-ldaps --disable-rtsp --disable-dict --disable-telnet --disable-tftp --disable-pop3 --disable-imap --disable-smtp --disable-gopher --disable-sspi --enable-ipv6 --disable-smb
  fi

  make -j${CORES}
  make install
  make clean

  popd
}

# Build Catalyst (macOS) targets for arm64 and x86_64
buildCatalyst() {
  build_arch_platform "arm64" "MacOSX"
  build_arch_platform "x86_64" "MacOSX"
  
  echo "${bold}Lipo library for x86_64 and arm64 catalyst ${normal}"
  
  mkdir -p "${CURRENTPATH}/bin/libcurl/catalyst"
  
  lipo -create "${CURRENTPATH}/bin/libcurl/MacOSX${SDKVERSION}-x86_64.sdk/lib/libcurl.a" "${CURRENTPATH}/bin/libcurl/MacOSX${SDKVERSION}-arm64.sdk/lib/libcurl.a" -output "${CURRENTPATH}/bin/libcurl/catalyst/libcurl.a"
}

# Build iOS target for arm64
buildIOS() {
  build_arch_platform "arm64" "iPhoneOS"
}

# Build iOS Simulator targets for arm64 and x86_64
buildIOSSim() {
  build_arch_platform "arm64" "iPhoneSimulator"
  build_arch_platform "x86_64" "iPhoneSimulator"
  
  echo "${bold}Lipo library for x86_64 and arm64 simulators ${normal}"
  
  mkdir -p "${CURRENTPATH}/bin/libcurl/iPhoneSimulator"
  
  lipo -create "${CURRENTPATH}/bin/libcurl/iPhoneSimulator${SDKVERSION}-x86_64.sdk/lib/libcurl.a" "${CURRENTPATH}/bin/libcurl/iPhoneSimulator${SDKVERSION}-arm64.sdk/lib/libcurl.a" -output "${CURRENTPATH}/bin/libcurl/iPhoneSimulator/libcurl.a"
}

createXCFramework() {
  mkdir -p xcframework || true
  
  echo "${bold}Creating xcframework ${normal}"
  
  xcodebuild -create-xcframework -library "${CURRENTPATH}/bin/libcurl/iPhoneSimulator/libcurl.a" -headers "${CURRENTPATH}/bin/libcurl/iPhoneSimulator${SDKVERSION}-arm64.sdk/include" \
    -library "${CURRENTPATH}/bin/libcurl/iPhoneOS${SDKVERSION}-arm64.sdk/lib/libcurl.a" -headers "${CURRENTPATH}/bin/libcurl/iPhoneOS${SDKVERSION}-arm64.sdk/include" \
        -library "${CURRENTPATH}/bin/libcurl/catalyst/libcurl.a" -headers "${CURRENTPATH}/bin/libcurl/MacOSX${SDKVERSION}-arm64.sdk/include" \
        -output "${CURRENTPATH}/xcframework/libcurl.xcframework"
}

cleanUp() {
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

  buildCatalyst
  buildIOS
  buildIOSSim
  
  createXCFramework
  cleanUp
}

# Run the main build process
main


