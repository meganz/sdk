#!/bin/bash

##
 # @file contrib/build_static.sh
 # @brief Builds MEGA SDK static library and static examples
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

# global vars
use_local=0
make_opts=""

on_exit_error() {
    echo "ERROR! Please check log files. Exiting.."
}

on_exit_ok() {
    echo "Successfully compiled MEGA SDK!"
}

print_distro_help()
{
    # yum: CentOS, Fedora, RedHat
    type yum >/dev/null 2>&1
    local exit_code=$?
    if [ $exit_code -eq 0 ]; then
        echo "Please execute the following command:  sudo yum install gcc gcc-c++ libtool unzip autoconf make wget glibc-devel-static"
        return
    fi

    # apt-get: Debian, Ubuntu
    type apt-get >/dev/null 2>&1
    local exit_code=$?
    if [ $exit_code -eq 0 ]; then
        echo "Please execute the following command:  sudo apt-get install gcc c++ libtool unzip autoconf make wget"
        return
    fi
}

check_apps()
{
    if [ -z "${BASH}" ]
    then
        echo "Please run this script with the BASH shell"
        exit 1
    elif [ ${BASH_VERSINFO} -lt 3 ]
    then
        printf "BASH version 3 or greater is required"
        exit 1
    fi

    APPS=(bash gcc c++ libtool tar unzip autoconf make autoreconf wget automake m4)
    for app in ${APPS[@]}; do
        type ${app} >/dev/null 2>&1 || { echo "${app} is not installed. Please install it first and re-run the script."; print_distro_help; exit 1; }
        hash ${app} 2>/dev/null || { echo "${app} is not installed. Please install it first and re-run the script."; print_distro_help; exit 1; }
    done
}

package_download() {
    local name=$1
    local url=$2
    local file=$3

    if [ $use_local -eq 1 ]; then
        echo "Using local file for $name"
        return
    fi

    echo "Downloading $name"

    if [ -f $file ]; then
        rm -f $file || true
    fi

    wget --no-check-certificate -c $url -O $file --progress=bar:force || exit 1
}

package_extract() {
    local name=$1
    local file=$2
    local dir=$3

    echo "Extracting $name"

    local filename=$(basename "$file")
    local extension="${filename##*.}"

    if [ ! -f $file ]; then
        echo "File $file does not exist!"
    fi

    if [ -d $dir ]; then
        rm -fr $dir || exit 1
    fi

    if [ $extension == "gz" ]; then
        tar -xzf $file &> $name.extract.log || exit 1
    elif [ $extension == "zip" ]; then
        unzip $file -d $dir &> $name.extract.log || exit 1
    else
        echo "Unsupported extension!"
        exit 1
    fi
}

package_configure() {
    local name=$1
    local dir=$2
    local install_dir="$3"
    local params="$4"

    local conf_f1="./config"
    local conf_f2="./configure"

    echo "Configuring $name"

    local cwd=$(pwd)
    cd $dir || exit 1

    if [ -f $conf_f1 ]; then
        $conf_f1 --prefix=$install_dir $params &> ../$name.conf.log || exit 1
    elif [ -f $conf_f2 ]; then
        $conf_f2 --prefix=$install_dir $params &> ../$name.conf.log || exit 1
    else
        local exit_code=$?
        echo "Failed to configure $name, exit status: $exit_code"
        exit 1
    fi

    cd $cwd
}

package_build() {
    local name=$1
    local dir=$2

    if [ "$#" -eq 3 ]; then
        local target=$3
    else
        local target=""
    fi

    echo "Building $name"

    local cwd=$(pwd)
    cd $dir

    make $make_opts $target &> ../$name.build.log

    local exit_code=$?
    if [ $exit_code -ne 0 ]; then
        echo "Failed to build $name, exit status: $exit_code"
        exit 1
    fi
    cd $cwd
}

package_install() {
    local name=$1
    local dir=$2
    local install_dir=$3

    if [ "$#" -eq 4 ]; then
        local target=$4
    else
        local target=""
    fi

    echo "Installing $name"

    local cwd=$(pwd)
    cd $dir
    make install $target &> ../$name.install.log
    local exit_code=$?
    if [ $exit_code -ne 0 ]; then
        echo "Failed to install $name, exit status: $exit_code"
        exit 1
    fi
    cd $cwd

    # some packages install libraries to "lib64" folder
    local lib64=$install_dir/lib64
    local lib=$install_dir/lib
    if [ -d $lib64 ]; then
        cp -f $lib64/* $lib/
    fi
}

openssl_pkg() {
    local build_dir=$1
    local install_dir=$2
    local name="OpenSSL"
    local openssl_ver="1.0.1j"
    local openssl_url="https://www.openssl.org/source/openssl-$openssl_ver.tar.gz"
    local openssl_file="openssl-$openssl_ver.tar.gz"
    local openssl_dir="openssl-$openssl_ver"
    local openssl_params="--openssldir=$install_dir no-shared shared"
    local loc_make_opts=$make_opts

    package_download $name $openssl_url $openssl_file
    package_extract $name $openssl_file $openssl_dir

    # handle MacOS
    if [ "$(uname)" == "Darwin" ]; then
        # OpenSSL compiles 32bit binaries, we need to explicitly tell to use x86_64 mode
        if [ "$(uname -m)" == "x86_64" ]; then
            echo "Configuring $name"
            local cwd=$(pwd)
            cd $openssl_dir
            ./Configure darwin64-x86_64-cc --prefix=$install_dir $openssl_params &> ../$name.conf.log || exit 1
            cd $cwd
        else
            package_configure $name $openssl_dir $install_dir "$openssl_params"
        fi
    else
        package_configure $name $openssl_dir $install_dir "$openssl_params"
    fi

    # OpenSSL has issues with parallel builds, let's use the default options
    make_opts=""
    package_build $name $openssl_dir
    make_opts=$loc_make_opts

    package_install $name $openssl_dir $install_dir
}

cryptopp_pkg() {
    local build_dir=$1
    local install_dir=$2
    local name="Crypto++"
    local cryptopp_ver="562"
    local cryptopp_url="http://www.cryptopp.com/cryptopp$cryptopp_ver.zip"
    local cryptopp_file="cryptopp$cryptopp_ver.zip"
    local cryptopp_dir="cryptopp$cryptopp_ver"

    package_download $name $cryptopp_url $cryptopp_file
    package_extract $name $cryptopp_file $cryptopp_dir
    package_build $name $cryptopp_dir static
    package_install $name $cryptopp_dir $install_dir
}

sodium_pkg() {
    local build_dir=$1
    local install_dir=$2
    local name="Sodium"
    local sodium_ver="1.0.1"
    local sodium_url="https://download.libsodium.org/libsodium/releases/libsodium-$sodium_ver.tar.gz"
    local sodium_file="sodium-$sodium_ver.tar.gz"
    local sodium_dir="libsodium-$sodium_ver"
    local sodium_params="--disable-shared --enable-static"

    package_download $name $sodium_url $sodium_file
    package_extract $name $sodium_file $sodium_dir
    package_configure $name $sodium_dir $install_dir "$sodium_params"
    package_build $name $sodium_dir
    package_install $name $sodium_dir $install_dir
}

zlib_pkg() {
    local build_dir=$1
    local install_dir=$2
    local name="Zlib"
    local zlib_ver="1.2.8"
    local zlib_url="http://zlib.net/zlib-$zlib_ver.tar.gz"
    local zlib_file="zlib-$zlib_ver.tar.gz"
    local zlib_dir="zlib-$zlib_ver"
    local zlib_params="--static"

    package_download $name $zlib_url $zlib_file
    package_extract $name $zlib_file $zlib_dir
    # Windows must use Makefile.gcc
    if [ "$(expr substr $(uname -s) 1 10)" != "MINGW32_NT" ]; then
        package_configure $name $zlib_dir $install_dir "$zlib_params"
        package_build $name $zlib_dir
        package_install $name $zlib_dir $install_dir
    else
        export BINARY_PATH=$install_dir/bin
        export INCLUDE_PATH=$install_dir/include
        export LIBRARY_PATH=$install_dir/lib
        package_build $name $zlib_dir "-f win32/Makefile.gcc"
        package_install $name $zlib_dir $install_dir "-f win32/Makefile.gcc"
        unset BINARY_PATH
        unset INCLUDE_PATH
        unset LIBRARY_PATH
    fi
}

sqlite_pkg() {
    local build_dir=$1
    local install_dir=$2
    local name="SQLite"
    local sqlite_ver="3080704"
    local sqlite_url="http://www.sqlite.org/2014/sqlite-autoconf-$sqlite_ver.tar.gz"
    local sqlite_file="sqlite-$sqlite_ver.tar.gz"
    local sqlite_dir="sqlite-autoconf-$sqlite_ver"
    local sqlite_params="--disable-shared --enable-static"

    package_download $name $sqlite_url $sqlite_file
    package_extract $name $sqlite_file $sqlite_dir
    package_configure $name $sqlite_dir $install_dir "$sqlite_params"
    package_build $name $sqlite_dir
    package_install $name $sqlite_dir $install_dir
}

cares_pkg() {
    local build_dir=$1
    local install_dir=$2
    local name="c-ares"
    local cares_ver="1.10.0"
    local cares_url="http://c-ares.haxx.se/download/c-ares-$cares_ver.tar.gz"
    local cares_file="cares-$cares_ver.tar.gz"
    local cares_dir="c-ares-$cares_ver"
    local cares_params="--disable-shared --enable-static"

    package_download $name $cares_url $cares_file
    package_extract $name $cares_file $cares_dir
    package_configure $name $cares_dir $install_dir "$cares_params"
    package_build $name $cares_dir
    package_install $name $cares_dir $install_dir
}

curl_pkg() {
    local build_dir=$1
    local install_dir=$2
    local name="cURL"
    local curl_ver="7.39.0"
    local curl_url="http://curl.haxx.se/download/curl-$curl_ver.tar.gz"
    local curl_file="curl-$curl_ver.tar.gz"
    local curl_dir="curl-$curl_ver"
    local curl_params="--disable-ftp --disable-file --disable-ldap --disable-ldaps --disable-rtsp --disable-dict \
        --disable-telnet --disable-tftp --disable-pop3 --disable-imap --disable-smtp --disable-gopher --disable-sspi \
        --without-librtmp --without-libidn --without-libssh2 --enable-ipv6 --disable-manual \
        --disable-shared --with-zlib=$install_dir --enable-ares=$install_dir --with-ssl=$install_dir"

    package_download $name $curl_url $curl_file
    package_extract $name $curl_file $curl_dir
    package_configure $name $curl_dir $install_dir "$curl_params"
    package_build $name $curl_dir
    package_install $name $curl_dir $install_dir
}

readline_pkg() {
    local build_dir=$1
    local install_dir=$2
    local name="Readline"
    local readline_ver="6.3"
    local readline_url="ftp://ftp.cwru.edu/pub/bash/readline-$readline_ver.tar.gz"
    local readline_file="readline-$readline_ver.tar.gz"
    local readline_dir="readline-$readline_ver"
    local readline_params="--disable-shared --enable-static"

    package_download $name $readline_url $readline_file
    package_extract $name $readline_file $readline_dir
    package_configure $name $readline_dir $install_dir "$readline_params"
    package_build $name $readline_dir
    package_install $name $readline_dir $install_dir
}

termcap_pkg() {
    local build_dir=$1
    local install_dir=$2
    local name="Termcap"
    local termcap_ver="1.3.1"
    local termcap_url="http://ftp.gnu.org/gnu/termcap/termcap-$termcap_ver.tar.gz"
    local termcap_file="termcap-$termcap_ver.tar.gz"
    local termcap_dir="termcap-$termcap_ver"
    local termcap_params="--disable-shared --enable-static"

    package_download $name $termcap_url $termcap_file
    package_extract $name $termcap_file $termcap_dir
    package_configure $name $termcap_dir $install_dir "$termcap_params"
    package_build $name $termcap_dir
    package_install $name $termcap_dir $install_dir
}

freeimage_pkg() {
    local build_dir=$1
    local install_dir=$2
    local cwd=$3
    local name="FreeImage"
    local freeimage_ver="3160"
    local freeimage_url="http://downloads.sourceforge.net/freeimage/FreeImage$freeimage_ver.zip"
    local freeimage_file="freeimage-$freeimage_ver.zip"
    local freeimage_dir_extract="freeimage-$freeimage_ver"
    local freeimage_dir="freeimage-$freeimage_ver/FreeImage"
    local freeimage_params="--disable-shared --enable-static"

    package_download $name $freeimage_url $freeimage_file
    package_extract $name $freeimage_file $freeimage_dir_extract

    # replace Makefile on MacOS
    if [ "$(uname)" == "Darwin" ]; then
        cp $cwd/contrib/FreeImage.Makefile.osx $freeimage_dir/Makefile.osx
    fi

    if [ "$(expr substr $(uname -s) 1 10)" != "MINGW32_NT" ]; then
        package_build $name $freeimage_dir
        # manually copy header and library
        cp $freeimage_dir/Dist/FreeImage.h $install_dir/include || exit 1
        cp $freeimage_dir/Dist/libfreeimage* $install_dir/lib || exit 1

    # it doesn't detect MinGW
    else
        package_build $name $freeimage_dir "-f Makefile.mingw"
        # manually copy header and library
        cp $freeimage_dir/Dist/FreeImage.h $install_dir/include || exit 1
        cp $freeimage_dir/Dist/FreeImage.dll $install_dir/lib || exit 1
        cp $freeimage_dir/Dist/FreeImage.lib $install_dir/lib || exit 1
    fi
}

# we can't build vanilla ReadLine under MinGW
readline_win_pkg() {
    local build_dir=$1
    local install_dir=$2
    local name="Readline"
    local readline_ver="5.0"
    local readline_url="http://gnuwin32.sourceforge.net/downlinks/readline-bin-zip.php"
    local readline_file="readline-bin.zip"
    local readline_dir="readline-bin"

    package_download $name $readline_url $readline_file
    package_extract $name $readline_file $readline_dir

    # manually copy binary files
    cp -R $readline_dir/include/* $install_dir/include/ || exit 1
    cp $readline_dir/lib/* $install_dir/lib/ || exit 1
}

build_sdk() {
    local install_dir=$1
    local debug=$2
    local no_examples=$3

    echo "Configuring MEGA SDK"

    ./autogen.sh || exit 1

    if [ "$(expr substr $(uname -s) 1 10)" != "MINGW32_NT" ]; then
        ./configure \
            --disable-shared --enable-static \
            --disable-silent-rules \
            --disable-curl-checks \
            --disable-megaapi \
            --with-openssl=$install_dir \
            --with-cryptopp=$install_dir \
            --with-sodium=$install_dir \
            --with-zlib=$install_dir \
            --with-sqlite=$install_dir \
            --with-cares=$install_dir \
            --with-curl=$install_dir \
            --with-freeimage=$install_dir \
            --with-readline=$install_dir \
            --with-termcap=$install_dir \
            $no_examples \
            $debug || exit 1
    else
        ./configure \
            --disable-shared --enable-static \
            --disable-silent-rules \
            --disable-curl-checks \
            --without-openssl \
            --disable-megaapi \
            --with-cryptopp=$install_dir \
            --with-sodium=$install_dir \
            --with-zlib=$install_dir \
            --with-sqlite=$install_dir \
            --without-cares \
            --without-curl \
            --with-freeimage=$install_dir \
            --with-readline=$install_dir \
            $no_examples \
            $debug || exit 1
    fi

    echo "Building MEGA SDK"

    make clean
    make -j9 || exit 1
}

display_help() {
    local app=$(basename "$0")
    echo ""
    echo "Usage:"
    echo " $app [-h --help] [-d --debug] [-l --local] [-n --no_examples] [--make_opts] [-p --prefix]"
    echo ""
    echo "Options:"
    echo " -d, --debug: Enable debug build"
    echo " -l, --local: Use local software archive files instead of downloading"
    echo " -n, --no_examples: Disable example applications"
    echo " --make_opts=[opts]: make options"
    echo " --prefix=[path]: Installation directory"
    echo ""
}

main() {
    local cwd=$(pwd)
    local work_dir=$cwd"/static_build/"
    local build_dir=$work_dir"build/"
    local install_dir=$work_dir"install/"
    local debug=""
    local no_examples=""

    OPTS=`getopt -o dhln -l debug,no_examples,help,local,make_opts:,prefix: -- "$@"`
    eval set -- "$OPTS"
    while true ; do
        case "$1" in
            -h)
                display_help $0
                exit
                shift;;
            --help)
                display_help $0
                exit
                shift;;
            -d)
                echo "DEBUG build"
                debug="--enable-debug"
                shift;;
            --debug)
                echo "DEBUG build"
                debug="--enable-debug"
                shift;;
            -l)
                echo "Using local files"
                use_local=1
                shift;;
            --local)
                echo "Using local files"
                use_local=1
                shift;;
            --make_opts)
                make_opts="$2"
                shift 2;;
            -n)
                echo "DEBUG build"
                no_examples="--disable-examples"
                shift;;
            --no_examples)
                no_examples="--disable-examples"
                shift;;
            --prefix)
                install_dir=$(readlink -f $2)
                echo "Installing into $install_dir"
                shift 2;;
            --)
                shift;
                break;;
        esac
    done

    check_apps

    trap on_exit_error EXIT

    if [ ! -d $build_dir ]; then
        mkdir -p $build_dir || exit 1
    fi
    if [ ! -d $install_dir ]; then
        mkdir -p $install_dir || exit 1
    fi

    cd $build_dir

    rm -fr *.log

    export PREFIX=$install_dir
    local old_pkg_conf=$PKG_CONFIG_PATH
    export PKG_CONFIG_PATH=$install_dir/lib/pkgconfig/
    export LD_LIBRARY_PATH=$install_dir
    export LD_RUN_PATH=$install_dir

    if [ "$(expr substr $(uname -s) 1 10)" != "MINGW32_NT" ]; then
        openssl_pkg $build_dir $install_dir
    fi
    cryptopp_pkg $build_dir $install_dir
    sodium_pkg $build_dir $install_dir
    zlib_pkg $build_dir $install_dir
    sqlite_pkg $build_dir $install_dir
    if [ "$(expr substr $(uname -s) 1 10)" != "MINGW32_NT" ]; then
        cares_pkg $build_dir $install_dir
        curl_pkg $build_dir $install_dir
    fi
    freeimage_pkg $build_dir $install_dir $cwd

    # Build readline and termcap if no_examples isn't set
    if [ -z "$no_examples" ]; then
        if [ "$(expr substr $(uname -s) 1 10)" != "MINGW32_NT" ]; then
            readline_pkg $build_dir $install_dir
            termcap_pkg $build_dir $install_dir
        else
           readline_win_pkg  $build_dir $install_dir
       fi
   fi

    cd $cwd

    build_sdk $install_dir $debug $no_examples

    unset PREFIX
    unset LD_RUN_PATH
    unset LD_LIBRARY_PATH
    export PKG_CONFIG_PATH=$old_pkg_conf
    trap on_exit_ok EXIT
}

main $@
