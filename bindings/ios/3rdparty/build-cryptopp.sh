#!/bin/sh

PROJECT_NAME="cryptopp"
CRYPTOPP_VERSION="565"

UNIVERSAL_OUTPUTFOLDER="lib"
BUILD_DIR="cryptopp"
CONFIGURATION="Release"
BUILD_ROOT="cryptopp"

##############################################

set -e

if [ ! -e "cryptopp${CRYPTOPP_VERSION}.zip" ]
then
curl -LO "http://www.cryptopp.com/cryptopp${CRYPTOPP_VERSION}.zip"
fi

unzip cryptopp${CRYPTOPP_VERSION}.zip -d cryptopp
 
# Step 1. Build versions for devices and simulator
xcodebuild -jobs 8 -target cryptopp ONLY_ACTIVE_ARCH=NO -configuration ${CONFIGURATION} -sdk iphoneos  BUILD_DIR="${BUILD_DIR}" BUILD_ROOT="${BUILD_ROOT}"
xcodebuild -jobs 8 -target cryptopp ONLY_ACTIVE_ARCH=NO -configuration ${CONFIGURATION} -sdk iphonesimulator BUILD_DIR="${BUILD_DIR}" BUILD_ROOT="${BUILD_ROOT}"
 
# Make sure the output directory exists
mkdir -p "${UNIVERSAL_OUTPUTFOLDER}"
 
# Step 2.  Create universal binary file, using lipo
lipo -create -output "${UNIVERSAL_OUTPUTFOLDER}/lib${PROJECT_NAME}.a" "${BUILD_DIR}/${CONFIGURATION}-iphoneos/lib${PROJECT_NAME}.a" "${BUILD_DIR}/${CONFIGURATION}-iphonesimulator/lib${PROJECT_NAME}.a"

mkdir -p include/cryptopp || true
cp -f cryptopp/*.h include/cryptopp
sed -i '' $'s/\#ifdef CRYPTOPP_DISABLE_X86ASM/\#define CRYPTOPP_DISABLE_X86ASM\\\n\#ifdef CRYPTOPP_DISABLE_X86ASM/' include/cryptopp/config.h

rm -rf cryptopp
rm -rf build
rm -rf bin

echo "Done."
