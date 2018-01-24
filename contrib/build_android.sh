#!/bin/bash

##
 # @file contrib/build_android.sh
 # @brief Builds MEGA SDK library for Android on x86_64 Linux host
 #
 # (c) 2013-2014 by Mega Limited, Auckland, New Zealand
 #
 # This file is part of the MEGA SDK - Client Access Engine.
 #
 # Applications using the MEGA API must present a valid application key
 # and comply with the the rules set forth in the Terms of Service.
 #
 # The MEGA SDK is distributed in the hope that it will be useful,
 # but WITHOUT ANY WARRANTY; without even the implied warranty of
 # MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 #
 # @copyright Simplified (2-clause) BSD License.
 #
 # You should have received a copy of the license along with this
 # program.
##

# https://android.googlesource.com/platform/ndk/+/ics-mr0/docs/STANDALONE-TOOLCHAIN.html

if [ -z "$ANDROID_NDK_ROOT" ]; then
    echo "Please set ANDROID_NDK_ROOT env variable to a valid NDK installation path!"
    exit 1;
fi

_ANDROID_ARCH=arch-arm
_ANDROID_API="android-9"
export ANDROID_GCC_VER=4.9

export ANDROID_SYSROOT="$ANDROID_NDK_ROOT/platforms/$_ANDROID_API/$_ANDROID_ARCH"
export SYSROOT="$ANDROID_SYSROOT"
export NDK_SYSROOT="$ANDROID_SYSROOT"
export ANDROID_NDK_SYSROOT="$ANDROID_SYSROOT"
export ANDROID_API="$_ANDROID_API"
export TOOLCHAIN_BIN="$ANDROID_NDK_ROOT/toolchains/arm-linux-androideabi-$ANDROID_GCC_VER/prebuilt/linux-x86_64/bin"
export TOOLCHAIN_INC="$ANDROID_NDK_ROOT/toolchains/arm-linux-androideabi-$ANDROID_GCC_VER/prebuilt/linux-x86_64/include"
export TOOLCHAIN_LIB="$ANDROID_NDK_ROOT/toolchains/arm-linux-androideabi-$ANDROID_GCC_VER/prebuilt/linux-x86_64/lib"
export ANDROID_DEV="$ANDROID_NDK_ROOT/platforms/$_ANDROID_API/$_ANDROID_ARCH/usr"
export HOSTCC=gcc

#STL port
#export ANDROID_STL_INC="$ANDROID_NDK_ROOT/sources/cxx-stl/stlport/stlport/"
#export STLPORT_LIB=libstlport_shared.so
#export ANDROID_STL_LIB="$ANDROID_NDK_ROOT/sources/cxx-stl/stlport/libs/armeabi/$STLPORT_LIB"

#GNU STL
export ANDROID_STL_INC="$ANDROID_NDK_ROOT/sources/cxx-stl/gnu-libstdc++/${ANDROID_GCC_VER}/include"
export ANDROID_STL_INC2="$ANDROID_NDK_ROOT/sources/cxx-stl/gnu-libstdc++/${ANDROID_GCC_VER}/libs/armeabi/include"
export GNUSTL_LIB=libgnustl_static.a
export ANDROID_STL_LIB="$ANDROID_NDK_ROOT/sources/cxx-stl/gnu-libstdc++/libs/armeabi/$ANDROID_GNUSTL_LIB"

#cryptopp565
export AOSP_SYSROOT="$ANDROID_SYSROOT" #used by cryptopp
export AOSP_STL_LIB="$ANDROID_STL_LIB"
export AOSP_BITS_INC="$ANDROID_STL_INC"
export AOSP_FLAGS="$ANDROID_FLAGS"

export MACHINE=armv7
export RELEASE=2.6.37
export SYSTEM=android
export ARCH=arm
export IS_ANDROID=1
export IS_CROSS_COMPILE=1
#export CROSS_COMPILE="arm-linux-androideabi-"

export CC=$TOOLCHAIN_BIN/arm-linux-androideabi-gcc
export CXX=$TOOLCHAIN_BIN/arm-linux-androideabi-g++
export CPP=$TOOLCHAIN_BIN/arm-linux-androideabi-cpp
export LD=$TOOLCHAIN_BIN/arm-linux-androideabi-ld
export AR=$TOOLCHAIN_BIN/arm-linux-androideabi-ar
export LIBTOOL=$TOOLCHAIN_BIN/arm-linux-androideabi-libtool
export RANLIB=$TOOLCHAIN_BIN/arm-linux-androideabi-ranlib
export AS=$TOOLCHAIN_BIN/arm-linux-androideabi-as
export STRIP=$TOOLCHAIN_BIN/arm-linux-androideabi-strip
export NM=$TOOLCHAIN_BIN/arm-linux-androideabi-nm
export RANLIB=$TOOLCHAIN_BIN/arm-linux-androideabi-ranlib

export INCLUDE="-I${SYSROOT}/usr/include -I${TOOLCHAIN_INC} -I${ANDROID_STL_INC}"


export CFLAGS="${CFLAGS} --sysroot=${SYSROOT}"
export CXXFLAGS="${CXXFLAGS} --sysroot=${SYSROOT} -I${SYSROOT}/usr/include -I${TOOLCHAIN_INC} -I${ANDROID_STL_INC} -I${ANDROID_STL_INC2}"
export CPPFLAGS="--sysroot=${SYSROOT} -I${SYSROOT}/usr/include -I${TOOLCHAIN_INC} -I${ANDROID_STL_INC}  -I${ANDROID_STL_INC2}"
export LDFLAGS="${LDFLAGS} -L${SYSROOT}/usr/lib -L${TOOLCHAIN_LIB}"

opts="--host=arm-linux-androideabi --with-sysroot=$SYSROOT "

./contrib/build_sdk.sh -n -y -r -a -q -e -f -g -x "$opts"
