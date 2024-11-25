#!/usr/bin/env bash

# Load the configuration
source /mega/dms-toolchains.conf

# Function to download the toolchain and verify its integrity
download_toolchain() {
    local platform="$1"
    local url="${TOOLCHAIN_URLS[$platform]}"
    local file="${TOOLCHAIN_FILES[$platform]}"
    local md5="${TOOLCHAIN_MD5_CHECKSUMS[$platform]}"

    echo "Downloading toolchain for $platform..."
    wget -q "$url" -O "/mega/$file.txz"

    echo "Verifying MD5 checksum..."
    echo "$md5  /mega/$file.txz" | md5sum -c -

    if [ $? -ne 0 ]; then
        echo "MD5 checksum verification failed!"
        exit 1
    fi

    echo "Extracting toolchain..."
    mkdir -p /mega/toolchain
    tar -xf "/mega/$file.txz" -C /mega/toolchain
    rm "/mega/$file.txz"
}

# Generate CMake toolchain file
generate_cmake_toolchain_file() {
    local toolchain_path="$1"
    local bitMode=32

    # Variables loaded from pkscripts platform file.
    [ -v ToolChainDir64 ] && bitMode=64
    arch="ARCH"
    c_compiler="CC$bitMode"
    c_compiler_flags="CFLAGS$bitMode"
    cxx_compiler="CXX$bitMode"
    cxx_compiler_flags="CFLAGS$bitMode"
    linker_flags="LDFLAGS$bitMode"
    toolchain_system_root_dir="ToolChainSysRoot$bitMode"

    (
        printf 'set(CMAKE_SYSTEM_NAME Linux)\n'
        printf 'set(CMAKE_SYSTEM_PROCESSOR %s)\n' "${!arch}"
        printf 'set(CMAKE_C_COMPILER "%s")\n' "/${!c_compiler/wrap-}"
        printf 'set(CMAKE_C_FLAGS_INIT "%s")\n' "${!c_compiler_flags} -Wno-psabi"
        printf 'set(CMAKE_CXX_COMPILER "%s")\n' "/${!cxx_compiler/wrap-}"
        printf 'set(CMAKE_CXX_FLAGS_INIT "%s -Wno-psabi")\n' "${!cxx_compiler_flags} -Wno-psabi"
        printf 'set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)\n'
        printf 'set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)\n'
        printf 'set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)\n'
        printf 'set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)\n'
        printf 'set(CMAKE_SYSROOT "%s")\n' "/${!toolchain_system_root_dir}"
    ) | sed -e "s!/usr/local!$toolchain_path!g;s!-mfpu=neon[^ ]\+!!g"
}

# Generate VCPKG triplet file
generate_vcpkg_triplet_file() {
    local platform="$1"
    local architecture="$2"

    printf 'set(VCPKG_CHAINLOAD_TOOLCHAIN_FILE "%s%s")\n' \
           '${CMAKE_CURRENT_LIST_DIR}/' \
           "$platform.toolchain.cmake"
    printf 'set(VCPKG_CMAKE_SYSTEM_NAME Linux)\n'
    printf 'set(VCPKG_LIBRARY_LINKAGE static)\n'
    printf 'set(VCPKG_CRT_LINKAGE dynamic)\n'
    printf 'set(VCPKG_TARGET_ARCHITECTURE %s)\n' "$(to_vcpkg_architecture "$architecture")"
}

# Translate architecture names for VCPKG
to_vcpkg_architecture() {
    case "$1" in
        'x86_64')
            echo x64
            ;;
        *)
            echo $1
            ;;
    esac
}

if [ "$#" -ne 1 ]; then
    echo "Usage: $0 <platform>"
    exit 1
fi

platform="$1"

# Check if the architecture is valid
if [[ ! " ${PLATFORM_LIST[@]} " =~ " $platform " ]]; then
    echo "Unsupported architecture: $platform"
    exit 1
fi

download_toolchain $platform

toolchain_path="mega/toolchain"

# Source the platform's file from the pkgscripts provided by Synology
# Some useful variables used in this script from the platfom file:
#
# ARCH: Target processor architecture (x86_64, arm, arm64,...)
# CC32, CC64: C Compiler, for 32 and 64 bits.
# CFLAGS32, CFLAGS64: C flags, for 32 and 64 bits.
# CXX32, CXX64: C++ Compiler, for 32 and 64 bits.
# CFLAGS32, CLFAGS64: C++ flags, for 32 and 64 bits.
# LDFLAGS32, LDFLAGS64: Linker flags, for 32 and 64 bits.
# ToolChainDir32, ToolChainDir64: Root of compiler, sysroot,... for 32 and 64 bits.
# ToolChainSysRoot32, ToolChainSysRoot: sysroot path, for 32 and 64 bits.
#
# Note: 64 variables only exists for 64 capable platforms. That could be used to detect them
source "/mega/pkgscripts/include/platform.$platform" || { echo "Failed to source platform file"; exit 1; }

# Generate CMake toolchain file
generate_cmake_toolchain_file "$toolchain_path" > "/mega/$platform.toolchain.cmake" || { echo "Couldn't generate toolchain file for \"$platform\""; exit 1; }

# Generate VCPKG triplet file
generate_vcpkg_triplet_file "$platform" "$ARCH" > "/mega/$platform.cmake" || { echo "Couldn't generate triplet file for \"$platform\""; exit 1; }
