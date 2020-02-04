#!/bin/bash
# This script is for getting and building the 3rd party libraries that the MEGA SDK uses (some are optional, and some are only needed only by MEGA apps too)
# 
# Your 3rdParty library builds should be outside the SDK repo.  We are moving to use vcpkg to build most of them. You can start it like this:
#
# mkdir 3rdParty
# cd 3rdParty
# git clone https://github.com/Microsoft/vcpkg.git
# cd vcpkg
# .\bootstrap-vcpkg.sh -disableMetrics
#
# Comment out any libraries that you won't use.
# If using pdfium, follow the instructions below to get the source code
#
# On Mac, go to ports/libraw/CONTROL and comment out the freeglut dependency
# 
# From your 3rdParty/vcpkg folder, run this script (in its proper location) with the desired triplet as the parameter.  (usually x64-linux or x64-osx)


DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"


display_help() {
    local app=$(basename "$0")
    echo ""
    echo "This script is for getting and building the 3rd party libraries that the MEGA SDK uses (some are optional, and some are only needed only by MEGA apps too)"
    echo ""
    echo "Your 3rdParty library builds should be outside the SDK repo.  We are moving to use vcpkg to build most of them. You can start it like this:"
    echo ""
    echo "mkdir 3rdParty"
    echo "cd 3rdParty"
    echo "git clone https://github.com/Microsoft/vcpkg.git"
    echo "cd vcpkg"
    echo ".\bootstrap-vcpkg.sh -disableMetrics"
    echo ""
    echo "If using pdfium, follow the instructions in 3rdparty_deps.txt to get the source code"
    echo ""
    echo "From your 3rdParty/vcpkg folder (or with vcpkg in your PATH), run this script (in its proper location) with the desired TRIPLET as the parameter.  (usually x64-linux or x64-osx)"
    echo ""
    echo "Your packages will be installed in 3rdParty/vcpkg/installed"
    echo ""
    echo "Usage:"
    echo " $app [-d deps_file] [-p ports_file] [-t triplets_path] TRIPLET"
    echo ""
    echo "Options:"
    echo " -d : path to file listing dependencies. By default $DIR/3rdparty_deps.txt. Comment out any libraries that you won't use."
    echo " -p : paths to ports file with dependencies/versions too look for. By default: $DIR/preferred-ports.txt"
    echo " -t : overlay triplets path. By default $DIR/vcpkg_extra_triplets"
    echo ""
}


PORTS_FILE="$DIR"/preferred-ports.txt
DEPS_FILE="$DIR"/3rdparty_deps.txt
OVERLAYTRIPLETS="--overlay-triplets=$DIR/vcpkg_extra_triplets"

while getopts ":d:p:t:" opt; do
  case $opt in
    p)
        PORTS_FOLDERS_FILE="$OPTARG"
    ;;
    d)
        DEPS_FILE="$OPTARG"
    ;;
    t)
        OVERLAYTRIPLETS="--overlay-triplets=$OPTARG"
    ;;
    \?)
        echo "Invalid option: $opt -$OPTARG" >&2
        display_help $0
        exit
    ;;
    *)
        display_help $0
        exit
    ;;
  esac
done

shift $(($OPTIND-1))

if [ "$#" -ne 1 ] && [ -z $TRIPLET ]; then
    echo "Illegal number of parameters: $#"
    display_help $0
    exit
fi

set -e
[ -z $TRIPLET ] && export TRIPLET=$1


OVERLAYPORTS=()

for l in $(cat "$PORTS_FILE" | grep -v "^#" | grep [a-z0-9A-Z]); do
OVERLAYPORTS=("--overlay-ports=$DIR/vcpkg_extra_ports/$l" "${OVERLAYPORTS[@]}")
done

[ -z $VCPKG ] && VCPKG=$(hash vcpkg 2>/dev/null && echo "vcpkg" || echo "./vckpg")
PARENTVCPKG=$(which vcpkg 2>/dev/null | awk -F '/' '{OFS="/"; $NF=""; print $0}')
echo mv ${PARENTVCPKG}ports{,_moved} 2>/dev/null || true

build_one ()
{
  $VCPKG install --triplet $TRIPLET $1 "${OVERLAYPORTS[@]}" "$OVERLAYTRIPLETS"
  echo $? $1 $TRIPLET >> buildlog
}

for dep in $(cat "$DEPS_FILE" | grep -v "^#" | grep [a-z0-9A-Z]); do
build_one $dep
done

