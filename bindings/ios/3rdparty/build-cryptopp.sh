#!/bin/sh
CRYPTOPP_VERSION="b6806f47e9fef7556689e7d3e5a458950acd3365"
source common.sh

set -e

# Build libcryptopp for a specific architecture and platform
build_arch_platform() {
  ARCH="$1"
  PLATFORM="$2"
  CATALYST="${3:-false}"
  
  rm -rf "${CRYPTOPP_VERSION}"
  tar zxf "${CRYPTOPP_VERSION}.tar.gz"
  pushd "cryptopp-${CRYPTOPP_VERSION}"
  
  if [[ "$ARCH" = "arm64" && ("$PLATFORM" == "iPhoneSimulator" || "$PLATFORM" == "iPhoneOS") ]]; then
    sed -i '' $'75s/DEF_CPPFLAGS=\"-DNDEBUG\"/DEF_CPPFLAGS=\"-DNDEBUG -DCRYPTOPP_DISABLE_ARM_CRC32\"/' TestScripts/setenv-ios.sh
  fi
    
  if [ "${CATALYST}" == "true" ]; then
    source TestScripts/setenv-macos.sh ${ARCH} MACOS_CATALYST=1
    mkdir -p "${CURRENTPATH}/bin/libcryptopp/${PLATFORM}-catalyst-${ARCH}.sdk"
  elif [ "${PLATFORM}" == "MacOSX" ]; then
    source TestScripts/setenv-macos.sh ${ARCH}
    mkdir -p "${CURRENTPATH}/bin/libcryptopp/${PLATFORM}-${ARCH}.sdk"
  else
    source TestScripts/setenv-ios.sh ${PLATFORM} ${ARCH}
    mkdir -p "${CURRENTPATH}/bin/libcryptopp/${PLATFORM}-${ARCH}.sdk"
  fi
  
  if [ "$PLATFORM" == "MacOSX" ]; then
    make -f GNUmakefile-cross static -j${CORES}
  else
    make -f GNUmakefile-cross lean -j${CORES}
  fi
  
  if [ "${CATALYST}" == "true" ]; then
    mv libcryptopp.a "${CURRENTPATH}/bin/libcryptopp/${PLATFORM}-catalyst-${ARCH}.sdk"
    mkdir -p "${CURRENTPATH}/bin/libcryptopp/${PLATFORM}-catalyst-${ARCH}.sdk/include/cryptopp"
    cp -f *.h "${CURRENTPATH}/bin/libcryptopp/${PLATFORM}-catalyst-${ARCH}.sdk/include/cryptopp"
  else
    mv libcryptopp.a "${CURRENTPATH}/bin/libcryptopp/${PLATFORM}-${ARCH}.sdk"
    mkdir -p "${CURRENTPATH}/bin/libcryptopp/${PLATFORM}-${ARCH}.sdk/include/cryptopp"
    cp -f *.h "${CURRENTPATH}/bin/libcryptopp/${PLATFORM}-${ARCH}.sdk/include/cryptopp"
  fi
  make clean
  
  popd
}

# Build Catalyst (macOS) targets for arm64 and x86_64
build_catalyst() {
  build_arch_platform "x86_64" "MacOSX" true
  build_arch_platform "arm64" "MacOSX" true
  
  echo "${bold}Lipo library for x86_64 and arm64 catalyst ${normal}"
  
  mkdir -p "${CURRENTPATH}/bin/libcryptopp/catalyst"
  
  lipo -create "${CURRENTPATH}/bin/libcryptopp/MacOSX-catalyst-x86_64.sdk/libcryptopp.a" "${CURRENTPATH}/bin/libcryptopp/MacOSX-catalyst-arm64.sdk/libcryptopp.a" -output "${CURRENTPATH}/bin/libcryptopp/catalyst/libcryptopp.a"
}

# Build macOS targets for arm64 and x86_64
build_mac() {
  build_arch_platform "x86_64" "MacOSX"
  build_arch_platform "arm64" "MacOSX"
  
  echo "${bold}Lipo library for x86_64 and arm64 catalyst ${normal}"
  
  mkdir -p "${CURRENTPATH}/bin/libcryptopp/mac"
  
  lipo -create "${CURRENTPATH}/bin/libcryptopp/MacOSX-x86_64.sdk/libcryptopp.a" "${CURRENTPATH}/bin/libcryptopp/MacOSX-arm64.sdk/libcryptopp.a" -output "${CURRENTPATH}/bin/libcryptopp/mac/libcryptopp.a"
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
  
  mkdir -p "${CURRENTPATH}/bin/libcryptopp/iPhoneSimulator"
  
  lipo -create "${CURRENTPATH}/bin/libcryptopp/iPhoneSimulator-x86_64.sdk/libcryptopp.a" "${CURRENTPATH}/bin/libcryptopp/iPhoneSimulator-arm64.sdk/libcryptopp.a" -output "${CURRENTPATH}/bin/libcryptopp/iPhoneSimulator/libcryptopp.a"
}

create_XCFramework() {
  mkdir -p xcframework || true
  
  echo "${bold}Creating xcframework ${normal}"
  
  xcodebuild -create-xcframework \
    -library "${CURRENTPATH}/bin/libcryptopp/iPhoneSimulator/libcryptopp.a" \
    -headers "${CURRENTPATH}/bin/libcryptopp/iPhoneSimulator-arm64.sdk/include" \
    -library "${CURRENTPATH}/bin/libcryptopp/iPhoneOS-arm64.sdk/libcryptopp.a" \
    -headers "${CURRENTPATH}/bin/libcryptopp/iPhoneOS-arm64.sdk/include" \
    -library "${CURRENTPATH}/bin/libcryptopp/catalyst/libcryptopp.a" \
    -headers "${CURRENTPATH}/bin/libcryptopp/MacOSX-catalyst-arm64.sdk/include" \
    -library "${CURRENTPATH}/bin/libcryptopp/mac/libcryptopp.a" \
    -headers "${CURRENTPATH}/bin/libcryptopp/MacOSX-arm64.sdk/include" \
    -output "${CURRENTPATH}/xcframework/libcryptopp.xcframework"
}

clean_up() {
  echo "${bold}Cleaning up ${normal}"

  rm -rf "${CRYPTOPP_VERSION}.tar.gz"
  rm -rf "cryptopp-${CRYPTOPP_VERSION}"
  rm -rf bin

  echo "${bold}Done.${normal}"
}

# Main build process
main() {
  if [ ! -e "${CRYPTOPP_VERSION}.tar.gz" ]; then
    curl -LO "https://github.com/jnavarrom/cryptopp/archive/${CRYPTOPP_VERSION}.tar.gz"
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
