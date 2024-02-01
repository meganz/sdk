#!/bin/sh

SDKVERSION=$(xcrun -sdk iphoneos --show-sdk-version)

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
