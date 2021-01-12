#!/bin/bash -i
set -e

##################################################
### SET THE PATH TO YOUR ANDROID NDK DIRECTORY ###
##################################################
if [ -z "$NDK_ROOT" ]; then
    NDK_ROOT=${HOME}/android-ndk
fi
##################################################
# PATH OF THE MEGA SDK RELATIVE TO jni/mega/sdk OR ABSOLUTE
MEGASDK_ROOT=../../../../../../../../..
##################################################
##################################################
# LIST OF ARCHS TO BE BUILT.
if [ -z "${BUILD_ARCHS}" ]; then
    # If no environment variable is defined, use all archs.
    BUILD_ARCHS="x86 armeabi-v7a x86_64 arm64-v8a"
fi
##################################################

if [ ! -d "${NDK_ROOT}" ]; then
    echo "* NDK_ROOT not set. Please download ndk r16b and export NDK_ROOT variable or create a link at ${HOME}/android-ndk and try again."
    exit 1
fi

NDK_BUILD=${NDK_ROOT}/ndk-build
JNI_PATH=`pwd`
LIBDIR=${JNI_PATH}/../obj/local/armeabi
JAVA_OUTPUT_PATH=${JNI_PATH}/../java
APP_PLATFORM=`grep APP_PLATFORM Application.mk | cut -d '=' -f 2`
ANDROID_API=21
LOG_FILE=/dev/null

CRYPTOPP=cryptopp
CRYPTOPP_VERSION=820
CRYPTOPP_SOURCE_FILE=cryptopp${CRYPTOPP_VERSION}.zip
CRYPTOPP_SOURCE_FOLDER=${CRYPTOPP}/${CRYPTOPP}
CRYPTOPP_DOWNLOAD_URL=http://www.cryptopp.com/${CRYPTOPP_SOURCE_FILE}
CRYPTOPP_SHA1="b042d2f0c93410abdec7c12bcd92787d019f8da1"

SQLITE=sqlite
SQLITE_VERSION=3300100
SQLITE_YEAR=2019
SQLITE_BASE_NAME=sqlite-amalgamation-${SQLITE_VERSION}
SQLITE_SOURCE_FILE=${SQLITE_BASE_NAME}.zip
SQLITE_SOURCE_FOLDER=${SQLITE}/${SQLITE}
SQLITE_DOWNLOAD_URL=https://www.sqlite.org/${SQLITE_YEAR}/${SQLITE_SOURCE_FILE}
SQLITE_SHA1="ff9b4e140fe0764bc7bc802facf5ac164443f517"

CURL=curl
CURL_VERSION=7.67.0
C_ARES_VERSION=1.15.0
CURL_EXTRA="--disable-smb --disable-ftp --disable-file --disable-ldap --disable-ldaps --disable-rtsp --disable-proxy --disable-dict --disable-telnet --disable-tftp --disable-pop3 --disable-imap --disable-smtp --disable-gopher --disable-sspi"
CURL_SOURCE_FILE=curl-${CURL_VERSION}.tar.gz
CURL_SOURCE_FOLDER=curl-${CURL_VERSION}
CURL_DOWNLOAD_URL=http://curl.haxx.se/download/${CURL_SOURCE_FILE}
CURL_SHA1="a91652f1eaa810866dce55b2d177c5b20f4aa7a7"

ARES_SOURCE_FILE=c-ares-${C_ARES_VERSION}.tar.gz
ARES_SOURCE_FOLDER=c-ares-${C_ARES_VERSION}
ARES_CONFIGURED=${CURL}/${ARES_SOURCE_FOLDER}/Makefile.inc
ARES_DOWNLOAD_URL=http://c-ares.haxx.se/download/${ARES_SOURCE_FILE}
ARES_SHA1="74a50c02b7f051c4fb66c0f60f187350f196d908"

OPENSSL=openssl
OPENSSL_VERSION=1.1.1d
OPENSSL_SOURCE_FILE=openssl-${OPENSSL_VERSION}.tar.gz
OPENSSL_SOURCE_FOLDER=${OPENSSL}-${OPENSSL_VERSION}
OPENSSL_DOWNLOAD_URL=https://www.openssl.org/source/old/${OPENSSL_VERSION%?}/${OPENSSL_SOURCE_FILE}
OPENSSL_PREFIX=${JNI_PATH}/${OPENSSL}/${OPENSSL_SOURCE_FOLDER}
OPENSSL_SHA1="056057782325134b76d1931c48f2c7e6595d7ef4"

SODIUM=sodium
SODIUM_VERSION=1.0.18
SODIUM_SOURCE_FILE=libsodium-${SODIUM_VERSION}.tar.gz
SODIUM_SOURCE_FOLDER=libsodium-${SODIUM_VERSION}
SODIUM_DOWNLOAD_URL=https://download.libsodium.org/libsodium/releases/${SODIUM_SOURCE_FILE}
SODIUM_SHA1="795b73e3f92a362fabee238a71735579bf46bb97"

LIBUV=libuv
LIBUV_VERSION=1.8.0
LIBUV_SOURCE_FILE=libuv-v${LIBUV_VERSION}.tar.gz
LIBUV_SOURCE_FOLDER=libuv-v${LIBUV_VERSION}
LIBUV_DOWNLOAD_URL=http://dist.libuv.org/dist/v${LIBUV_VERSION}/${LIBUV_SOURCE_FILE}
LIBUV_SHA1="91ea51844ec0fac1c6358a7ad3e8bba128e9d0cc"

MEDIAINFO=mediainfo
MEDIAINFO_VERSION=4ee7f77c087b29055f48d539cd679de8de6f9c48
MEDIAINFO_SOURCE_FILE=${MEDIAINFO_VERSION}.zip
MEDIAINFO_SOURCE_FOLDER=MediaInfoLib-${MEDIAINFO_VERSION}
MEDIAINFO_DOWNLOAD_URL=https://github.com/meganz/MediaInfoLib/archive/${MEDIAINFO_SOURCE_FILE}
MEDIAINFO_SHA1="30927c761418e807d8d3b64e171a6c9ab9659c2e"

ZENLIB=ZenLib
ZENLIB_VERSION=6694a744d82d942c4a410f25f916561270381889
ZENLIB_SOURCE_FILE=${ZENLIB_VERSION}.zip
ZENLIB_SOURCE_FOLDER=ZenLib-${ZENLIB_VERSION}
ZENLIB_DOWNLOAD_URL=https://github.com/MediaArea/ZenLib/archive/${ZENLIB_SOURCE_FILE}
ZENLIB_SHA1="1af04654c9618f54ece624a0bad881a3cfef3692"

function downloadCheckAndUnpack()
{
    local URL=$1
    local FILENAME=$2
    local SHA1=$3
    local TARGETPATH=$4
    
    if [[ -f ${FILENAME} ]]; then
        echo "* Already downloaded: '${FILENAME}'"
        local CURRENTSHA1=`sha1sum ${FILENAME} | cut -d " " -f 1`
        if [ "${SHA1}" != "${CURRENTSHA1}" ]; then
            echo "* Invalid hash. Redownloading..."
            wget --no-check-certificate -O ${FILENAME} ${URL} &>> ${LOG_FILE}
        fi
    else
        echo "* Downloading '${FILENAME}' ..."
        wget --no-check-certificate -O ${FILENAME} ${URL} &>> ${LOG_FILE}
    fi

    local NEWSHA1=`sha1sum ${FILENAME} | cut -d " " -f 1`
    if [ "${SHA1}" != "${NEWSHA1}" ]; then
        echo "* Invalid hash. It is ${NEWSHA1} but it should be ${SHA1}. Aborting..."
        exit 1
    fi

    if [[ "${FILENAME}" =~ \.tar\.[^\.]+$ ]]; then
        echo "* Extracting TAR file..."
        tar --overwrite -xf ${FILENAME} -C ${TARGETPATH} &>> ${LOG_FILE}
    elif [[ "${FILENAME}" =~ \.zip$ ]]; then
        echo "* Extracting ZIP file..."
    	unzip -o ${FILENAME} -d ${TARGETPATH} &>> ${LOG_FILE}
    else
        echo "* Dont know how to extract '${FILENAME}'"
        exit 1
    fi

    echo "* Extraction finished"
}

function createMEGABindings
{
    mkdir mega/sdk &>> ${LOG_FILE} || true
    rm mega/sdk/src &>> ${LOG_FILE} || true
    ln -s ${MEGASDK_ROOT}/src mega/sdk/src &>> ${LOG_FILE} || true
    rm mega/sdk/include &>> ${LOG_FILE} || true
    ln -s ${MEGASDK_ROOT}/include mega/sdk/include &>> ${LOG_FILE} || true
    rm mega/sdk/bindings &>> ${LOG_FILE} || true
    ln -s ${MEGASDK_ROOT}/bindings mega/sdk/bindings &>> ${LOG_FILE} || true
    rm mega/sdk/third_party &>> ${LOG_FILE} || true
    ln -s ${MEGASDK_ROOT}/third_party mega/sdk/third_party &>> ${LOG_FILE} || true

    echo "* Creating MEGA Java bindings"
    mkdir -p ../java/nz/mega/sdk
    swig -c++ -Imega/sdk/include -java -package nz.mega.sdk -outdir ${JAVA_OUTPUT_PATH}/nz/mega/sdk -o bindings/megasdk.cpp -DHAVE_LIBUV -DENABLE_CHAT mega/sdk/bindings/megaapi.i &>> ${LOG_FILE}
}


if (( $# != 1 )); then
    echo "Usage: $0 <all | bindings | clean | clean_mega>";
    exit 0 
fi

if [ "$1" == "bindings" ]; then
    createMEGABindings
    echo "* Bindings ready!"
    echo "* Running ndk-build"
    ${NDK_BUILD} -j8
    echo "* ndk-build finished"
    echo "* Task finished OK"
    exit 0
fi

if [ "$1" == "clean_mega" ]; then
    echo "* Deleting Java bindings"
    make -C mega -f MakefileBindings clean JAVA_BASE_OUTPUT_PATH=${JAVA_OUTPUT_PATH} &>> ${LOG_FILE}
    rm -rf megachat/megachat.cpp megachat/megachat.h
    echo "* Deleting tarballs"
    rm -rf ../obj/local/armeabi
    rm -rf ../obj/local/x86
    rm -rf ../obj/local/arm64-v8a
    rm -rf ../obj/local/x86_64
    echo "* Task finished OK"
    exit 0
fi

if [ "$1" == "clean" -o "$1" == "clean_all" ]; then
    echo "* Deleting Java bindings"
    make -C mega -f MakefileBindings clean JAVA_BASE_OUTPUT_PATH=${JAVA_OUTPUT_PATH} &>> ${LOG_FILE}
    rm -rf megachat/megachat.cpp megachat/megachat.h
    
    echo "* Deleting source folders"    
    rm -rf ${CRYPTOPP_SOURCE_FOLDER}
    rm -rf ${SQLITE_SOURCE_FOLDER} ${SQLITE}/${SQLITE_BASE_NAME}
    rm -rf ${CURL}/${CURL_SOURCE_FOLDER}
    rm -rf ${CURL}/${CURL}
    rm -rf ${CURL}/${ARES_SOURCE_FOLDER}
    rm -rf ${CURL}/ares
    rm -rf ${OPENSSL}/${OPENSSL_SOURCE_FOLDER}
    rm -rf ${OPENSSL}/${OPENSSL}
    rm -rf ${SODIUM}/${SODIUM_SOURCE_FOLDER}
    rm -rf ${SODIUM}/${SODIUM}
    rm -rf ${LIBUV}/${LIBUV_SOURCE_FOLDER}
    rm -rf ${LIBUV}/${LIBUV}
    rm -rf ${MEDIAINFO}/${ZENLIB_SOURCE_FOLDER}
    rm -rf ${MEDIAINFO}/${ZENLIB}
    rm -rf ${MEDIAINFO}/${MEDIAINFO_SOURCE_FOLDER}
    rm -rf ${MEDIAINFO}/${MEDIAINFO}

    if [ "$1" == "clean_all" ]; then
        echo "* Deleting tarballs"
        rm -rf ${CRYPTOPP}/${CRYPTOPP_SOURCE_FILE}
        rm -rf ${SQLITE}/${SQLITE_SOURCE_FILE}
        rm -rf ${CURL}/${CURL_SOURCE_FILE}
        rm -rf ${CURL}/${ARES_SOURCE_FILE}
        rm -rf ${OPENSSL}/${OPENSSL_SOURCE_FILE}
        rm -rf ${SODIUM}/${SODIUM_SOURCE_FILE}
        rm -rf ${LIBUV}/${LIBUV_SOURCE_FILE}
        rm -rf ${MEDIAINFO}/${ZENLIB_SOURCE_FILE}
        rm -rf ${MEDIAINFO}/${MEDIAINFO_SOURCE_FILE}
    fi
    rm -rf ${CRYPTOPP}/${CRYPTOPP_SOURCE_FILE}.ready
    rm -rf ${SQLITE}/${SQLITE_SOURCE_FILE}.ready
    rm -rf ${CURL}/${CURL_SOURCE_FILE}.ready
    rm -rf ${OPENSSL}/${OPENSSL_SOURCE_FILE}.ready
    rm -rf ${SODIUM}/${SODIUM_SOURCE_FILE}.ready
    rm -rf ${LIBUV}/${LIBUV_SOURCE_FILE}.ready
    rm -rf ${MEDIAINFO}/${ZENLIB_SOURCE_FILE}.ready
    rm -rf ${MEDIAINFO}/${MEDIAINFO_SOURCE_FILE}.ready

    echo "* Deleting object files"
    rm -rf ../obj/local/armeabi-v7a
    rm -rf ../obj/local/arm64-v8a
    rm -rf ../obj/local/x86
    rm -rf ../obj/local/x86_64

    echo "* Deleting libraries"
    rm -rf ../libs/armeabi-v7a
    rm -rf ../libs/arm64-v8a
    rm -rf ../libs/x86
    rm -rf ../libs/x86_64

    echo "* Task finished OK"
    exit 0
fi

if [ "$1" != "all" ]; then
    echo "Usage: $0 <all | bindings | clean | clean_all | clean_mega>";
    exit 1
fi

echo "* Building ${BUILD_ARCHS} arch(s)"

echo "* Setting up MEGA"
createMEGABindings
echo "* MEGA is ready"

echo "* Setting up libsodium"
if [ ! -f ${SODIUM}/${SODIUM_SOURCE_FILE}.ready ]; then
    downloadCheckAndUnpack ${SODIUM_DOWNLOAD_URL} ${SODIUM}/${SODIUM_SOURCE_FILE} ${SODIUM_SHA1} ${SODIUM}
    ln -sf ${SODIUM_SOURCE_FOLDER} ${SODIUM}/${SODIUM}
    pushd ${SODIUM}/${SODIUM} &>> ${LOG_FILE}
    export ANDROID_NDK_HOME=${NDK_ROOT}
    export NDK_PLATFORM=${APP_PLATFORM}
    ./autogen.sh &>> ${LOG_FILE}
    echo "#include <limits.h>" >>  src/libsodium/include/sodium/export.h
    sed -i 's/enable-minimal/enable-minimal --disable-pie/g' dist-build/android-build.sh

    if [ -n "`echo ${BUILD_ARCHS} | grep -w armeabi-v7a`" ]; then
        echo "* Prebuilding libsodium for ARMv7"
        dist-build/android-armv7-a.sh &>> ${LOG_FILE}
        ln -sf libsodium-android-armv7-a libsodium-android-armeabi-v7a
    fi

    if [ -n "`echo ${BUILD_ARCHS} | grep -w arm64-v8a`" ]; then
        echo "* Prebuilding libsodium for ARMv8"
        dist-build/android-armv8-a.sh &>> ${LOG_FILE}
        ln -sf libsodium-android-armv8-a libsodium-android-arm64-v8a
    fi

    if [ -n "`echo ${BUILD_ARCHS} | grep -w x86`" ]; then
        echo "* Prebuilding libsodium for x86"
        dist-build/android-x86.sh &>> ${LOG_FILE}
        ln -sf libsodium-android-i686 libsodium-android-x86
    fi

    if [ -n "`echo ${BUILD_ARCHS} | grep -w x86_64`" ]; then
        echo "* Prebuilding libsodium for x86_64"
        dist-build/android-x86_64.sh &>> ${LOG_FILE}
        ln -sf libsodium-android-westmere libsodium-android-x86_64
    fi

    popd &>> ${LOG_FILE}
    touch ${SODIUM}/${SODIUM_SOURCE_FILE}.ready
fi
echo "* libsodium is ready"

echo "* Setting up Crypto++"
if [ ! -f ${CRYPTOPP}/${CRYPTOPP_SOURCE_FILE}.ready ]; then
    mkdir -p ${CRYPTOPP}/${CRYPTOPP}
    downloadCheckAndUnpack ${CRYPTOPP_DOWNLOAD_URL} ${CRYPTOPP}/${CRYPTOPP_SOURCE_FILE} ${CRYPTOPP_SHA1} ${CRYPTOPP}/${CRYPTOPP}
    cp ${NDK_ROOT}/sources/android/cpufeatures/cpu-features.h ${CRYPTOPP}/${CRYPTOPP}/
    touch ${CRYPTOPP}/${CRYPTOPP_SOURCE_FILE}.ready
fi
echo "* Crypto++ is ready"

echo "* Setting up SQLite"
if [ ! -f ${SQLITE}/${SQLITE_SOURCE_FILE}.ready ]; then
    downloadCheckAndUnpack ${SQLITE_DOWNLOAD_URL} ${SQLITE}/${SQLITE_SOURCE_FILE} ${SQLITE_SHA1} ${SQLITE}
    ln -fs ${SQLITE_BASE_NAME} ${SQLITE_SOURCE_FOLDER}
    touch ${SQLITE}/${SQLITE_SOURCE_FILE}.ready
fi
echo "* SQLite is ready"

echo "* Setting up libuv"
if [ ! -f ${LIBUV}/${LIBUV_SOURCE_FILE}.ready ]; then
    downloadCheckAndUnpack ${LIBUV_DOWNLOAD_URL} ${LIBUV}/${LIBUV_SOURCE_FILE} ${LIBUV_SHA1} ${LIBUV}
    ln -sf ${LIBUV_SOURCE_FOLDER} ${LIBUV}/${LIBUV}
    touch ${LIBUV}/${LIBUV_SOURCE_FILE}.ready
fi
echo "* libuv is ready"

echo "* Setting up ZenLib"
if [ ! -f ${MEDIAINFO}/${ZENLIB_SOURCE_FILE}.ready ]; then
    downloadCheckAndUnpack ${ZENLIB_DOWNLOAD_URL} ${MEDIAINFO}/${ZENLIB_SOURCE_FILE} ${ZENLIB_SHA1} ${MEDIAINFO}
    ln -sf ${ZENLIB_SOURCE_FOLDER} ${MEDIAINFO}/${ZENLIB}
    cp mega/sdk/include/mega/mega_glob.h ${MEDIAINFO}/${ZENLIB}/Source/ZenLib/glob.h
    touch ${MEDIAINFO}/${ZENLIB_SOURCE_FILE}.ready
fi
echo "* ZenLib is ready"

echo "* Setting up MediaInfo"
if [ ! -f ${MEDIAINFO}/${MEDIAINFO_SOURCE_FILE}.ready ]; then
    downloadCheckAndUnpack ${MEDIAINFO_DOWNLOAD_URL} ${MEDIAINFO}/${MEDIAINFO_SOURCE_FILE} ${MEDIAINFO_SHA1} ${MEDIAINFO}
    ln -sf ${MEDIAINFO_SOURCE_FOLDER} ${MEDIAINFO}/${MEDIAINFO}
    touch ${MEDIAINFO}/${MEDIAINFO_SOURCE_FILE}.ready
fi
echo "* MediaInfo is ready"

echo "* Setting up OpenSSL"
if [ ! -f ${OPENSSL}/${OPENSSL_SOURCE_FILE}.ready ]; then
    downloadCheckAndUnpack ${OPENSSL_DOWNLOAD_URL} ${OPENSSL}/${OPENSSL_SOURCE_FILE} ${OPENSSL_SHA1} ${OPENSSL}
    ln -sf ${OPENSSL_SOURCE_FOLDER} ${OPENSSL}/${OPENSSL}
    pushd ${OPENSSL}/${OPENSSL} &>> ${LOG_FILE}
    ORIG_PATH=$PATH

    if [ -n "`echo ${BUILD_ARCHS} | grep -w x86`" ]; then
        echo "* Prebuilding OpenSSL for x86"
        export ANDROID_NDK_HOME=${NDK_ROOT}
        PATH=$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/linux-x86_64/bin/:$ORIG_PATH
        mkdir openssl-android-x86/
        ./Configure android-x86 -D__ANDROID_API__=${ANDROID_API} --openssldir=${PWD}/openssl-android-x86/ --prefix=${PWD}/openssl-android-x86/ &>> ${LOG_FILE}
        make -j8 &>> ${LOG_FILE}
        make install &>> ${LOG_FILE}
        make clean &>> ${LOG_FILE}
    fi

    if [ -n "`echo ${BUILD_ARCHS} | grep -w armeabi-v7a`" ]; then
        echo "* Prebuilding OpenSSL for ARMv7"
        export ANDROID_NDK_HOME=${NDK_ROOT}
        PATH=$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/linux-x86_64/bin/:$ORIG_PATH
        mkdir openssl-android-armeabi-v7a
        ./Configure android-arm -latomic -D__ANDROID_API__=${ANDROID_API} --openssldir=${PWD}/openssl-android-armeabi-v7a/ --prefix=${PWD}/openssl-android-armeabi-v7a/ &>> ${LOG_FILE}
        make -j8 &>> ${LOG_FILE}
        make install &>> ${LOG_FILE}
        make clean &>> ${LOG_FILE}
    fi

    if [ -n "`echo ${BUILD_ARCHS} | grep -w x86_64`" ]; then
        echo "* Prebuilding OpenSSL for x86_64"
        export ANDROID_NDK_HOME=${NDK_ROOT}
        PATH=$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/linux-x86_64/bin/:$ORIG_PATH
        mkdir openssl-android-x86_64/
        ./Configure android-x86_64 -D__ANDROID_API__=${ANDROID_API} --openssldir=${PWD}/openssl-android-x86_64/ --prefix=${PWD}/openssl-android-x86_64/ &>> ${LOG_FILE}
        make -j8 &>> ${LOG_FILE}
        make install &>> ${LOG_FILE}
        make clean &>> ${LOG_FILE}
    fi

    if [ -n "`echo ${BUILD_ARCHS} | grep -w arm64-v8a`" ]; then
        echo "* Prebuilding OpenSSL for ARMv8"
        export ANDROID_NDK_HOME=${NDK_ROOT}
        PATH=$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/linux-x86_64/bin/:$ORIG_PATH
        mkdir -p openssl-android-arm64-v8a
        ./Configure android-arm64 -latomic -D__ANDROID_API__=${ANDROID_API} --openssldir=${PWD}/openssl-android-arm64-v8a/ --prefix=${PWD}/openssl-android-arm64-v8a/ &>> ${LOG_FILE}
        make -j8 &>> ${LOG_FILE}
        make install &>> ${LOG_FILE}
        make clean &>> ${LOG_FILE}
    fi

    popd &>> ${LOG_FILE}
    PATH=$ORIG_PATH
    touch ${OPENSSL}/${OPENSSL_SOURCE_FILE}.ready
fi
echo "* OpenSSL is ready"

echo "* Setting up cURL with c-ares"
if [ ! -f ${CURL}/${CURL_SOURCE_FILE}.ready ]; then
    echo "* Setting up cURL"
    downloadCheckAndUnpack ${CURL_DOWNLOAD_URL} ${CURL}/${CURL_SOURCE_FILE} ${CURL_SHA1} ${CURL}
    ln -sf ${CURL_SOURCE_FOLDER} ${CURL}/${CURL}
    echo "* cURL is ready"

    echo "* Setting up c-ares"
    downloadCheckAndUnpack ${ARES_DOWNLOAD_URL} ${CURL}/${ARES_SOURCE_FILE} ${ARES_SHA1} ${CURL}
    ln -sf ${ARES_SOURCE_FOLDER} ${CURL}/ares
    ln -sf ../${ARES_SOURCE_FOLDER} ${CURL}/${CURL_SOURCE_FOLDER}/ares
    echo "* c-ares is ready"
    touch ${CURL}/${CURL_SOURCE_FILE}.ready
fi
echo "* cURL with c-ares is ready"

echo "* All dependencies are prepared!"

echo "* Building SDK"
rm -rf ../tmpLibs
mkdir ../tmpLibs
if [ -n "`echo ${BUILD_ARCHS} | grep -w x86`" ]; then
    echo "* Running ndk-build x86"
    ${NDK_BUILD} V=1 -j8 APP_ABI=x86 &>> ${LOG_FILE}
    mv ../libs/x86 ../tmpLibs/
    echo "* ndk-build finished for x86"
fi

if [ -n "`echo ${BUILD_ARCHS} | grep -w armeabi-v7a`" ]; then
    echo "* Running ndk-build arm 32bits (armeabi-v7a)"
    ${NDK_BUILD} V=1 -j8 APP_ABI=armeabi-v7a &>> ${LOG_FILE}
    mv ../libs/armeabi-v7a ../tmpLibs/
    echo "* ndk-build finished for arm 32bits (armeabi-v7a)"
fi

if [ -n "`echo ${BUILD_ARCHS} | grep -w x86_64`" ]; then
    echo "* Running ndk-build x86_64"
    ${NDK_BUILD} V=1 -j8 APP_ABI=x86_64 &>> ${LOG_FILE}
    mv ../libs/x86_64 ../tmpLibs/
    echo "* ndk-build finished for x86_64"
fi

if [ -n "`echo ${BUILD_ARCHS} | grep -w arm64-v8a`" ]; then
    echo "* Running ndk-build arm 64bits (arm64-v8a)"
    ${NDK_BUILD} V=1 -j8 APP_ABI=arm64-v8a &>> ${LOG_FILE}
    mv ../libs/arm64-v8a ../tmpLibs/
    echo "* ndk-build finished for arm 64bits (arm64-v8a)"
fi
mv ../tmpLibs/* ../libs/
rmdir ../tmpLibs/

echo "* Task finished OK"
