#!/usr/bin/env bash

# Function to generate CMake toolchain file
generate_cmake_toolchain_file()
{
    local toolchain_path="$1"
    local architecture=32

    [ -v ToolChainDir64 ] && architecture=64

    c_compiler="CC$architecture"
    c_compiler_flags="CFLAGS$architecture"
    cxx_compiler="CXX$architecture"
    cxx_compiler_flags="CFLAGS$architecture"
    linker_flags="LDFLAGS$architecture"
    toolchain_system_root_dir="ToolChainSysRoot$architecture"

    (
        printf 'set(CMAKE_SYSTEM_NAME Linux)\n'
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

# Function to generate VCPKG triplet file
generate_vcpkg_triplet_file()
{
    local platform="$1"
    local toolchain_path="$2"
    local architecture="$3"

    printf 'set(VCPKG_CHAINLOAD_TOOLCHAIN_FILE "%s%s")\n' \
           '${CMAKE_CURRENT_LIST_DIR}/' \
           "$platform.toolchain.cmake"
    printf 'set(VCPKG_CMAKE_SYSTEM_NAME Linux)\n'
    printf 'set(VCPKG_LIBRARY_LINKAGE static)\n'
    printf 'set(VCPKG_CRT_LINKAGE dynamic)\n'
    printf 'set(VCPKG_TARGET_ARCHITECTURE %s)\n' "$(to_vcpkg_architecture "$architecture")"
}

# Function to translate architecture names for VCPKG
to_vcpkg_architecture()
{
    case "$1" in
        'x86_64')
            echo x64
            ;;
        *)
            echo $1
            ;;
    esac
}

# Check for platform argument
if [ "$#" -ne 1 ]; then
    echo "Usage: $0 <platform>"
    exit 1
fi

platform="$1"
toolchain_path="mega/toolchain"
vcpkg_toolchain_base="mega/toolchains"

echo "Processing platform: $platform"

# Source the platform's file
source "/mega/pkgscripts/include/platform.$platform" || { echo "Failed to source platform file"; exit 1; }

# Generate CMake toolchain file
generate_cmake_toolchain_file "$toolchain_path" > "$platform.toolchain.cmake" || { echo "Couldn't generate toolchain file for \"$platform\""; exit 1; }

# Generate VCPKG triplet file
generate_vcpkg_triplet_file "$platform" "$vcpkg_toolchain_base/" "$ARCH" > "$platform.cmake" || { echo "Couldn't generate triplet file for \"$platform\""; exit 1; }