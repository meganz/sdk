#!/bin/bash

##
 # @file contrib/build_sdk.sh
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

# Warn about /bin/sh ins't bash.
if [ -z "$BASH_VERSION" ] ; then
    echo "WARNING: The shell running this script isn't bash."
fi

# global vars
verbose=1
use_local=0
use_dynamic=0
disable_freeimage=0
disable_ssl=0
disable_zlib=0
download_only=0
only_build_dependencies=0
enable_megaapi=0
make_opts=""
config_opts=""
no_examples=""
enable_drive_notifications=""
configure_only=0
disable_posix_threads=""
enable_sodium=0
enable_cares=0
enable_curl=0
enable_libuv=0
enable_libraw=0
android_build=0
readline_build=0
enable_cryptopp=0
disable_sqlite=0
disable_mediainfo=0
incremental=0
no_optimisation=0
extra_openssl_params=""
cross_compiling=0
configure_cross_options=""
openssl_cross_option=""
status_dir=""
persistent_path="/opt/persistent"
warning_as_error=0
build_gtest=0
build_tests=0

on_exit_error() {
    echo "ERROR! Please check log files. Exiting.."
}

on_exit_ok() {
    if [ $configure_only -eq 1 ]; then
        echo "Successfully configured MEGA SDK!"
    elif [ $download_only -eq 1 ]; then
        echo "Successfully downloaded MEGA SDK dependencies!"
    elif [ $only_build_dependencies -eq 1 ]; then
        echo "Successfully built MEGA SDK dependencies!"
    else
        echo "Successfully compiled MEGA SDK!"
    fi
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
        echo "Please execute the following command:  sudo apt-get install gcc g++ libtool-bin unzip autoconf m4 make wget"
        echo " (or 'libtool' on older Debian / Ubuntu distro versions)"
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
    local file=$local_dir/$3
    local md5sum=$4

    if [ $use_local -eq 1 ]; then
        echo "Using local file for $name"
        return
    fi


    if [ -f $file ]; then
        rm -f $file || true
    fi

    # use packages previously downloaded in /tmp/megasdkbuild folder
    # if not present download from URL specified
    # if wget fails, try curl
    mkdir -p /tmp/megasdkbuild/

#    cp /srv/dependencies_manually_downloaded/$3 $file 2>/dev/null || \

    if [ -e /tmp/megasdkbuild/$3 ] ; then
        echo "Using cached file /tmp/megasdkbuild/$3"
        cp /tmp/megasdkbuild/$3 $file 
    else
        echo "Downloading $name to get $3"
        wget --secure-protocol=TLSv1_2 --no-check-certificate -c $url -O $file --progress=bar:force -t 2 -T 30 || \
        curl -k $url > $file || exit 1
    fi
    
    echo "Checking MD5SUM for $file"
    if ! echo $md5sum \*$file | md5sum -c - ; then
        echo "Downloading $3 again"
        #rm /tmp/megasdkbuild/$3
        rm $file #this prevents unexpected "The file is already fully retrieved; nothing to do."
        wget --no-check-certificate -c $url -O $file --progress=bar:force -t 2 -T 30 || \
        curl -k $url > $file || exit 1
        
        echo "Checking (again) MD5SUM for $file"
        if ! echo $md5sum \*$file | md5sum -c - ; then
            echo "Aborting execution due to incorrect MD5SUM for $file. Expected: $md5sum. Calculated:"
            md5sum $file
            exit 1
        fi
    fi
    
    #copy to tmp download folder for next constructions
    cp $file /tmp/megasdkbuild/$3
    echo "Cached package file at /tmp/megasdkbuild/$3"
}

package_extract() {
    local name=$1
    local file=$local_dir/$2
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

exitwithlog() {
    local logname=$1
    local exitcode=$2
    if [ $verbose -eq 1 ]; then
        cat $logname
    fi
    exit $exitcode
}

package_configure() {
    local name=$1
    local dir=$2
    local install_dir="$3"
    local params="$4"
    local extralibs="$5"

    local conf_f0="./Configure"  #OpenSSL has ./config which first guesses a cross-compiling then calls Configure.  To support reliable cross-compiling, we can call Configure directly with $CC $CXX set appropriately already
    local conf_f1="./config"
    local conf_f2="./configure"
    local conf=""
    local autogen="./autogen.sh"

    echo "Configuring $name"

    local cwd=$(pwd)
    cd $dir || exit 1

    if [ -f $autogen ]; then
        $autogen
    fi

    if [ $cross_compiling -eq 1 ] && [ -f $conf_f0 ]; then
        conf="$conf_f0 $openssl_cross_option"
    elif [ $cross_compiling -eq 0 ] && [ -f $conf_f1 ]; then  # ./config is used to figure out which compiler; if we are cross compiling then skip that and use configure directly, $CC $CXX etc specify the tools
        conf="$conf_f1"
    elif [ -f $conf_f2 ]; then
        conf="$conf_f2 $configure_cross_options"
    else
        local exit_code=$?
        echo "Failed to configure $name, exit status: $exit_code"
        exit 1
    fi

    echo -n "configuring $name with : $conf $config_opts --prefix=$install_dir $params"
    [ -z "$extralibs" ] && echo "" || echo " LIBS=$extralibs"

    if [ -z "$extralibs" ]; then
        $conf $config_opts --prefix=$install_dir $params &> ../$name.conf.log || exitwithlog ../$name.conf.log 1
    else
        $conf $config_opts --prefix=$install_dir $params LIBS="$extralibs" &> ../$name.conf.log || exitwithlog ../$name.conf.log 1
    fi

    if [ $no_optimisation -eq 1 ]; then
        # this one works for OpenSSL, other files may need to be adjusted differently
        sed -i -e "s/\(C[XP]*FLAGS\? *=.*\)-O[1-3]/\1-O0/" ./Makefile
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

    echo make $make_opts $target
    make $make_opts $target &> ../$name.build.log

    local exit_code=$?
    if [ $exit_code -ne 0 ]; then
        echo "Failed to build $name, exit status: $exit_code"
        exitwithlog ../$name.build.log 1
    fi
    cd $cwd
}

package_install() {
    local name=$1
    local dir=$2
    local install_dir=$3
    local md5=$4

    if [ "$#" -eq 5 ]; then
        local target=$5
    else
        local target=""
    fi

    echo "Installing $name"

    local cwd=$(pwd)
    cd $dir

    if [ $android_build -eq 1 ] && [[ $name == "Crypto++"* ]]; then
        echo make install-lib $target
        make install-lib $target &> ../$name.install.log || make install $target &> ../$name.install.log
    else
        echo make install $target
        make install $target &> ../$name.install.log
    fi
    local exit_code=$?
    if [ $exit_code -ne 0 ]; then
        echo "Failed to install $name, exit status: $exit_code"
        exitwithlog ../$name.install.log 1
        cd $cwd
    else
        cd $cwd
        echo $md5 > $status_dir/$name.success
    fi

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
    local openssl_ver="3.1.1"
    local openssl_rel=""
    local openssl_url="https://github.com/openssl/openssl/releases/download/openssl-${openssl_ver}${openssl_rel}/openssl-${openssl_ver}${openssl_rel}.tar.gz"
    local openssl_md5="1864b75e31fb4a6e0a07fd832529add3"
    local openssl_file="openssl-${openssl_ver}${openssl_rel}.tar.gz"
    local openssl_dir="openssl-${openssl_ver}${openssl_rel}"
    if [ $use_dynamic -eq 1 ]; then
    local openssl_params="--openssldir=$install_dir shared $extra_openssl_params"
    else
    local openssl_params="--openssldir=$install_dir no-shared $extra_openssl_params"
    fi
    local loc_make_opts=$make_opts

    if [ $incremental -eq 1 ] && [ -e $status_dir/$name.success ] && [ `cat $status_dir/$name.success` = $openssl_md5 ]; then
        echo "$name already built"
        return
    else
        rm -f $status_dir/$name.success
    fi

    package_download $name $openssl_url $openssl_file $openssl_md5
    if [ $download_only -eq 1 ]; then
        return
    fi

    package_extract $name $openssl_file $openssl_dir

    if [ $android_build -eq 1 ]; then
        echo "Configuring $name"
        local cwd=$(pwd)
        cd $openssl_dir
        perl -pi -e 's/install: all install_docs install_sw/install: install_docs install_sw/g' Makefile.org
        ./config shared no-ssl2 no-ssl3 no-comp no-hw no-engine --prefix=$install_dir
        make depend || exit 1
        cd $cwd
    else
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
            package_configure $name $openssl_dir $install_dir "$openssl_params" || exit 1
        fi
    fi

    # OpenSSL has issues with parallel builds, let's use the default options
    make_opts=""
    package_build $name $openssl_dir
    make_opts=$loc_make_opts

    package_install $name $openssl_dir $install_dir $openssl_md5
}

cryptopp_pkg() {
    local build_dir=$1
    local install_dir=$2
    local name="Crypto++"
    local cryptopp_ver="820"
    local cryptopp_url="http://www.cryptopp.com/cryptopp$cryptopp_ver.zip"
    local cryptopp_md5="8a8bcb436af83e16d2227bd4ac642243"
    local cryptopp_file="cryptopp$cryptopp_ver.zip"
    local cryptopp_dir="cryptopp$cryptopp_ver"

    if [ $incremental -eq 1 ] && [ -e $status_dir/$name.success ] && [ `cat $status_dir/$name.success` = $cryptopp_md5 ]; then
        echo "$name already built"
        return
    else
        rm -f $status_dir/$name.success
    fi

    package_download $name $cryptopp_url $cryptopp_file $cryptopp_md5
    if [ $download_only -eq 1 ]; then
        return
    fi

    package_extract $name $cryptopp_file $cryptopp_dir

    #modify Makefile so that it does not use specific cpu architecture optimizations
    sed "s#CXXFLAGS += -march=native#CXXFLAGS += #g" -i $cryptopp_dir/GNUmakefile
    sed -i -e "160,165d" $cryptopp_dir/GNUmakefile
    
    if [ $android_build -eq 1 ]; then
        cp ${ANDROID_NDK_ROOT}/sources/android/cpufeatures/cpu-features.h $cryptopp_dir/
        cp ${ANDROID_NDK_ROOT}/sources/android/cpufeatures/cpu-features.c $cryptopp_dir/
        package_build $name $cryptopp_dir "static -f GNUmakefile-cross"
        package_install $name $cryptopp_dir $install_dir $cryptopp_md5 "-f GNUmakefile-cross"
    else
        package_build $name $cryptopp_dir static
        package_install $name $cryptopp_dir $install_dir $cryptopp_md5
   fi
}

sodium_pkg() {
    local build_dir=$1
    local install_dir=$2
    local name="Sodium"
    local sodium_ver="1.0.18"
    local sodium_url="https://github.com/jedisct1/libsodium/archive/$sodium_ver.tar.gz"
    local sodium_md5="94a783f33ff8a97a09708bc61370d280"
    local sodium_file="$sodium_ver.tar.gz"
    local sodium_dir="libsodium-$sodium_ver"
    if [ $use_dynamic -eq 1 ]; then
        local sodium_params="--enable-shared"
    else
        local sodium_params="--disable-shared --enable-static --disable-pie"
    fi

    if [ $incremental -eq 1 ] && [ -e $status_dir/$name.success ] && [ `cat $status_dir/$name.success` = $sodium_md5 ]; then
        echo "$name already built"
        return
    else
        rm -f $status_dir/$name.success
    fi

    package_download $name $sodium_url $sodium_file $sodium_md5
    if [ $download_only -eq 1 ]; then
        return
    fi

    package_extract $name $sodium_file $sodium_dir
    package_configure $name $sodium_dir $install_dir "$sodium_params"
    package_build $name $sodium_dir
    package_install $name $sodium_dir $install_dir $sodium_md5
}

libuv_pkg() {
    local build_dir=$1
    local install_dir=$2
    local name="libuv"
    local libuv_ver="1.34.2"
    local libuv_url="https://github.com/libuv/libuv/archive/v${libuv_ver}.tar.gz"
    local libuv_md5="5b57d93320a4aac3e66de94a1d4da98d"
    local libuv_file="v${libuv_ver}.tar.gz"
    local libuv_dir="libuv-$libuv_ver"
    if [ $use_dynamic -eq 1 ]; then
        local libuv_params="--enable-shared"
    else
        local libuv_params="--disable-shared --enable-static"
    fi

    if [ $incremental -eq 1 ] && [ -e $status_dir/$name.success ] && [ `cat $status_dir/$name.success` = $libuv_md5 ]; then
        echo "$name already built"
        return
    else
        rm -f $status_dir/$name.success
    fi

    package_download $name $libuv_url $libuv_file $libuv_md5
    if [ $download_only -eq 1 ]; then
        return
    fi

    package_extract $name $libuv_file $libuv_dir

    local OLD_CFLAGS="$CFLAGS"

    # linking with static library requires -fPIC
    if [ $use_dynamic -eq 0 ]; then
        export CFLAGS="$CFLAGS -fPIC"
    fi
    package_configure $name $libuv_dir $install_dir "$libuv_params"

    export CFLAGS="$OLD_CFLAGS"

    package_build $name $libuv_dir
    package_install $name $libuv_dir $install_dir $libuv_md5
}

libraw_pkg() {
    local build_dir=$1
    local install_dir=$2
    local name="libraw"
    local libraw_ver="0.19.5"
    local libraw_url="https://www.libraw.org/data/LibRaw-$libraw_ver.tar.gz"
    local libraw_md5="865ab9a40910709ff86988e8c0a7d146"
    local libraw_file="libraw-$libraw_ver.tar.gz"
    local libraw_dir="LibRaw-$libraw_ver"
    if [ $use_dynamic -eq 1 ]; then
        local libraw_params="--enable-shared"
    else
        local libraw_params="--disable-shared --enable-static"
    fi

    if [ $incremental -eq 1 ] && [ -e $status_dir/$name.success ] && [ `cat $status_dir/$name.success` = $libraw_md5 ]; then
        echo "$name already built"
        return
    else
        rm -f $status_dir/$name.success
    fi

    package_download $name $libraw_url $libraw_file $libraw_md5
    if [ $download_only -eq 1 ]; then
        return
    fi

    package_extract $name $libraw_file $libraw_dir

    local OLD_LIBS="$LIBS"

    # linking with static library requires -lstdc++
    if [ $use_dynamic -eq 0 ]; then
        export LIBS="$LIBS -lstdc++"
    fi
    package_configure $name $libraw_dir $install_dir "$libraw_params"

    export LIBS="$OLD_LIBS"

    package_build $name $libraw_dir
    package_install $name $libraw_dir $install_dir $libraw_md5
}

zlib_pkg() {
    local build_dir=$1
    local install_dir=$2
    local name="Zlib"
    local zlib_ver="1.2.13"
    local zlib_url="https://zlib.net/fossils/zlib-$zlib_ver.tar.gz"
    local zlib_md5="9b8aa094c4e5765dabf4da391f00d15c"
    local zlib_file="zlib-$zlib_ver.tar.gz"
    local zlib_dir="zlib-$zlib_ver"
    local loc_conf_opts=$config_opts
    local loc_configure_cross_options=$configure_cross_options

    if [ $use_dynamic -eq 1 ]; then
        local zlib_params=""
    else
        local zlib_params="--static"
    fi

    if [ $incremental -eq 1 ] && [ -e $status_dir/$name.success ] && [ `cat $status_dir/$name.success` = $zlib_md5 ]; then
        echo "$name already built"
        return
    else
        rm -f $status_dir/$name.success
    fi

    package_download $name $zlib_url $zlib_file $zlib_md5
    if [ $download_only -eq 1 ]; then
        return
    fi

    package_extract $name $zlib_file $zlib_dir

    # doesn't recognize --host=xxx
    config_opts=""
    configure_cross_options=""

    # Windows must use Makefile.gcc
    if [ "$(expr substr $(uname -s) 1 10)" != "MINGW32_NT" ]; then
        package_configure $name $zlib_dir $install_dir "$zlib_params"
        package_build $name $zlib_dir
        package_install $name $zlib_dir $install_dir $zlib_md5
    else
        export BINARY_PATH=$install_dir/bin
        export INCLUDE_PATH=$install_dir/include
        export LIBRARY_PATH=$install_dir/lib
        package_build $name $zlib_dir "-f win32/Makefile.gcc"
        package_install $name $zlib_dir $install_dir $zlib_md5 "-f win32/Makefile.gcc"
        unset BINARY_PATH
        unset INCLUDE_PATH
        unset LIBRARY_PATH
    fi
    config_opts=$loc_conf_opts
    configure_cross_options=$loc_configure_cross_options

}

sqlite_pkg() {
    local build_dir=$1
    local install_dir=$2
    local name="SQLite"
    local sqlite_ver="3330000"
    local sqlite_url="http://www.sqlite.org/2020/sqlite-autoconf-$sqlite_ver.tar.gz"
    local sqlite_md5="842a8a100d7b01b09e543deb2b7951dd"
    local sqlite_file="sqlite-$sqlite_ver.tar.gz"
    local sqlite_dir="sqlite-autoconf-$sqlite_ver"
    if [ $use_dynamic -eq 1 ]; then
        local sqlite_params="--enable-shared"
    else
        local sqlite_params="--disable-shared --enable-static"
    fi

    if [ $incremental -eq 1 ] && [ -e $status_dir/$name.success ] && [ `cat $status_dir/$name.success` = $sqlite_md5 ]; then
        echo "$name already built"
        return
    else
        rm -f $status_dir/$name.success
    fi

    package_download $name $sqlite_url $sqlite_file $sqlite_md5
    if [ $download_only -eq 1 ]; then
        return
    fi

    package_extract $name $sqlite_file $sqlite_dir
    package_configure $name $sqlite_dir $install_dir "$sqlite_params"
    package_build $name $sqlite_dir
    package_install $name $sqlite_dir $install_dir $sqlite_md5
}

cares_pkg() {
    local build_dir=$1
    local install_dir=$2
    local name="c-ares"
    local cares_ver="1.14.0"
    local cares_url="http://c-ares.haxx.se/download/c-ares-$cares_ver.tar.gz"
    local cares_md5="e57b37a7c46283e83c21cde234df10c7"
    local cares_file="cares-$cares_ver.tar.gz"
    local cares_dir="c-ares-$cares_ver"
    if [ $use_dynamic -eq 1 ]; then
        local cares_params="--enable-shared"
    else
        local cares_params="--disable-shared --enable-static"
    fi

    if [ $incremental -eq 1 ] && [ -e $status_dir/$name.success ] && [ `cat $status_dir/$name.success` = $cares_md5 ]; then
        echo "$name already built"
        return
    else
        rm -f $status_dir/$name.success
    fi

    package_download $name $cares_url $cares_file $cares_md5
    if [ $download_only -eq 1 ]; then
        return
    fi

    package_extract $name $cares_file $cares_dir
    package_configure $name $cares_dir $install_dir "$cares_params"
    package_build $name $cares_dir
    package_install $name $cares_dir $install_dir $cares_md5
}

curl_pkg() {
    local build_dir=$1
    local install_dir=$2
    local name="cURL"
    local curl_ver="7.68.0"
    local curl_url="http://curl.haxx.se/download/curl-$curl_ver.tar.gz"
    local curl_md5="f68d6f716ff06d357f476ea4ea57a3d6"
    local curl_file="curl-$curl_ver.tar.gz"
    local curl_dir="curl-$curl_ver"
    local openssl_flags=""

    # use local or system OpenSSL
    if [ $disable_ssl -eq 0 ]; then
        openssl_flags="--with-ssl=$install_dir"
    else
        openssl_flags="--with-ssl"
    fi

    if [ $use_dynamic -eq 1 ]; then
        local curl_params="--disable-ftp --disable-file --disable-ldap --disable-ldaps --disable-rtsp --disable-dict \
            --disable-telnet --disable-tftp --disable-pop3 --disable-imap --disable-smtp --disable-gopher --disable-sspi \
            --without-librtmp --without-libidn --without-libidn2 --without-libssh2 --enable-ipv6 --disable-manual --without-nghttp2 --without-libpsl \
            --without-brotli --with-zlib=$install_dir --enable-ares=$install_dir $openssl_flags"
    else
        local curl_params="--disable-ftp --disable-file --disable-ldap --disable-ldaps --disable-rtsp --disable-dict \
            --disable-telnet --disable-tftp --disable-pop3 --disable-imap --disable-smtp --disable-gopher --disable-sspi \
            --without-librtmp --without-libidn --without-libidn2 --without-libssh2 --enable-ipv6 --disable-manual --without-nghttp2 --without-libpsl \
            --without-brotli --disable-shared --with-zlib=$install_dir --enable-ares=$install_dir $openssl_flags"
    fi

    if [ $incremental -eq 1 ] && [ -e $status_dir/$name.success ] && [ `cat $status_dir/$name.success` = $curl_md5 ]; then
        echo "$name already built"
        return
    else
        rm -f $status_dir/$name.success
    fi

    package_download $name $curl_url $curl_file $curl_md5
    if [ $download_only -eq 1 ]; then
        return
    fi

    package_extract $name $curl_file $curl_dir
    package_configure $name $curl_dir $install_dir "$curl_params" "-ldl"  
    package_build $name $curl_dir
    package_install $name $curl_dir $install_dir $curl_md5
}

readline_pkg() {
    local build_dir=$1
    local install_dir=$2
    local name="Readline"
    local readline_ver="7.0"
    local readline_url="https://ftp.gnu.org/gnu/readline/readline-$readline_ver.tar.gz"
    local readline_md5="205b03a87fc83dab653b628c59b9fc91"
    local readline_file="readline-$readline_ver.tar.gz"
    local readline_dir="readline-$readline_ver"
    if [ $use_dynamic -eq 1 ]; then
        local readline_params="--enable-shared"
    else
        local readline_params="--disable-shared --enable-static"
    fi

    if [ $incremental -eq 1 ] && [ -e $status_dir/$name.success ] && [ `cat $status_dir/$name.success` = $readline_md5 ]; then
        echo "$name already built"
        return
    else
        rm -f $status_dir/$name.success
    fi

    package_download $name $readline_url $readline_file $readline_md5
    if [ $download_only -eq 1 ]; then
        return
    fi

    package_extract $name $readline_file $readline_dir
    package_configure $name $readline_dir $install_dir "$readline_params"
    package_build $name $readline_dir
    package_install $name $readline_dir $install_dir $readline_md5
}

termcap_pkg() {
    local build_dir=$1
    local install_dir=$2
    local name="Termcap"
    local termcap_ver="1.3.1"
    local termcap_url="http://ftp.gnu.org/gnu/termcap/termcap-$termcap_ver.tar.gz"
    local termcap_md5="ffe6f86e63a3a29fa53ac645faaabdfa"
    local termcap_file="termcap-$termcap_ver.tar.gz"
    local termcap_dir="termcap-$termcap_ver"
    if [ $use_dynamic -eq 1 ]; then
        local termcap_params="--enable-shared"
    else
        local termcap_params="--disable-shared --enable-static"
    fi

    if [ $incremental -eq 1 ] && [ -e $status_dir/$name.success ] && [ `cat $status_dir/$name.success` = $termcap_md5 ]; then
        echo "$name already built"
        return
    else
        rm -f $status_dir/$name.success
    fi

    package_download $name $termcap_url $termcap_file $termcap_md5
    if [ $download_only -eq 1 ]; then
        return
    fi

    package_extract $name $termcap_file $termcap_dir
    local OLD_CPPFLAGS="$CPPFLAGS"

    # linking with static library requires -fPIC
    if [ $use_dynamic -eq 0 ]; then
        export CPPFLAGS="$CPPFLAGS -fPIC"
    fi
    package_configure $name $termcap_dir $install_dir "$termcap_params"
    package_build $name $termcap_dir

    export CPPFLAGS="$OLD_CPPFLAGS"

    package_install $name $termcap_dir $install_dir $termcap_md5
}

freeimage_pkg() {
    local build_dir=$1
    local install_dir=$2
    local cwd=$3
    local name="FreeImage"
    local freeimage_ver="3180"
    local freeimage_url="http://downloads.sourceforge.net/freeimage/FreeImage$freeimage_ver.zip"
    local freeimage_md5="f8ba138a3be233a3eed9c456e42e2578"
    local freeimage_file="freeimage-$freeimage_ver.zip"
    local freeimage_dir_extract="freeimage-$freeimage_ver"
    local freeimage_dir="freeimage-$freeimage_ver/FreeImage"

    if [ $incremental -eq 1 ] && [ -e $status_dir/$name.success ] && [ `cat $status_dir/$name.success` = $freeimage_md5 ]; then
        echo "$name already built"
        return
    else
        rm -f $status_dir/$name.success
    fi

    package_download $name $freeimage_url $freeimage_file $freeimage_md5
    if [ $download_only -eq 1 ]; then
        return
    fi

    package_extract $name $freeimage_file $freeimage_dir_extract
    
    #Fix issue with powf64 redefined in mathcalls.h in glibc 2.27
    find $freeimage_dir_extract/FreeImage/ -type f -print0 | xargs -0 sed -i "s#powf64#powf64freeimage#g"

    # Remove libewbp support in FreeImage
    rm -r $freeimage_dir_extract/FreeImage/Source/LibWebP
    rm -r $freeimage_dir_extract/FreeImage/Source/FreeImage/PluginWebP.cpp
    pushd $freeimage_dir_extract/FreeImage
    sh gensrclist.sh
    popd
    sed -i "s#./Source/FreeImage/PluginWebP.cpp##" $freeimage_dir_extract/FreeImage/Makefile.srcs
    sed -i -e '141d' $freeimage_dir_extract/FreeImage/Source/Plugin.h
    sed -i -e '274d' $freeimage_dir_extract/FreeImage/Source/FreeImage/Plugin.cpp

    sed -i "s#CFLAGS ?=#CFLAGS +=#g" $freeimage_dir_extract/FreeImage/Makefile.gnu
    #patch to fix problem with newest compilers
    sed -i "s#CXXFLAGS ?=#CXXFLAGS += -std=c++98#g" $freeimage_dir_extract/FreeImage/Makefile.gnu

    #freeimage uses some macros with a dollarsign in the name, and also has some constants that don't fit in a long
    #as gcc building for 32 bit linux has long as 32 bit.  Also some files have the utf-8 BOM which old gcc doesn't like
    export CFLAGS="$CFLAGS -fdollars-in-identifiers"
    export CXXFLAGS="$CXXFLAGS -fdollars-in-identifiers"

    find $freeimage_dir/Source/OpenEXR/IlmImf/ -name "*.cpp" | xargs sed -i -e "s/0xffffffffffffffffL/0xffffffffffffffffull/" 

    if command -v dos2unix; then
        find $freeimage_dir/Source/LibRawLite/internal/ -name "*.cpp" | xargs dos2unix
        find $freeimage_dir/Source/LibRawLite/internal/ -name "*.h" | xargs dos2unix
    else
        echo "Command dos2unix not found, skipping some fixes for FreeImage"
    fi

    # replace Makefile on MacOS
    if [ "$(uname)" == "Darwin" ]; then
        cp $cwd/contrib/FreeImage.Makefile.osx $freeimage_dir/Makefile.osx
    fi

    if [ $android_build -eq 1 ]; then
        sed -i '/#define HAVE_SEARCH_H 1/d' $freeimage_dir/Source/LibTIFF4/tif_config.h
    fi

    if [ $use_dynamic -eq 0 ]; then
        export FREEIMAGE_LIBRARY_TYPE=STATIC
    fi

    # freeimage's LibPNG has a problem with deciding to use neon on 64 bit arm, resulting in a missing symbol
    if [ "$ARCH" == "aarch64" -o "$ARCH" == "arm64" ]; then
        export CFLAGS="$CFLAGS -DPNG_ARM_NEON_OPT=0"
    fi 

    if [ "$(expr substr $(uname -s) 1 10)" != "MINGW32_NT" ]; then
        package_build $name $freeimage_dir
        # manually copy header and library
        cp $freeimage_dir/Dist/FreeImage.h $install_dir/include || exit 1
        cp $freeimage_dir/Dist/libfreeimage* $install_dir/lib || exit 1
        echo $freeimage_md5 > $status_dir/$name.success
    # MinGW
    else
        package_build $name $freeimage_dir "-f Makefile.mingw"
        # manually copy header and library
        cp $freeimage_dir/Dist/FreeImage.h $install_dir/include || exit 1
        # ignore if not present
        cp $freeimage_dir/Dist/FreeImage.dll $install_dir/lib || 1
        cp $freeimage_dir/Dist/FreeImage.lib $install_dir/lib || 1
        cp $freeimage_dir/Dist/libFreeImage.a $install_dir/lib || 1
        echo $freeimage_md5 > $status_dir/$name.success
    fi
}

mediainfo_pkg() {
    local build_dir=$1
    local install_dir=$2
    local cwd=$3
    local zenlib_name="ZenLib"
    local zenlib_ver="6694a744d82d942c4a410f25f916561270381889"
    local zenlib_url="https://github.com/MediaArea/ZenLib/archive/${zenlib_ver}.tar.gz"
    local zenlib_md5="02a78d1d18ce163483d8e01961f983f4"
    local zenlib_file="$zenlib_ver.tar.gz"
    local zenlib_dir_extract="ZenLib-$zenlib_ver"
    local zenlib_dir="ZenLib-$zenlib_ver/Project/GNU/Library"

    local mediainfolib_name="MediaInfoLib"
    local mediainfolib_ver="4ee7f77c087b29055f48d539cd679de8de6f9c48"
    local mediainfolib_url="https://github.com/meganz/MediaInfoLib/archive/${mediainfolib_ver}.tar.gz"
    local mediainfolib_md5="5214341153298077b9e711c181742fe3"
    local mediainfolib_file="$mediainfolib_ver.tar.gz"
    local mediainfolib_dir_extract="MediaInfoLib-$mediainfolib_ver"
    local mediainfolib_dir="MediaInfoLib-$mediainfolib_ver/Project/GNU/Library"

    if [ $incremental -eq 1 ] && [ -e $status_dir/$mediainfolib_name.success ] && [ `cat $status_dir/$mediainfolib_name.success` = $mediainfolib_md5 ]; then
        echo "$mediainfolib_name already built"
        return
    else
        rm -f $status_dir/$mediainfolib_name.success
    fi

    package_download $zenlib_name $zenlib_url $zenlib_file $zenlib_md5
    package_download $mediainfolib_name $mediainfolib_url $mediainfolib_file $mediainfolib_md5
    if [ $download_only -eq 1 ]; then
        return
    fi

    package_extract $zenlib_name $zenlib_file $zenlib_dir_extract
    ln -sfr $zenlib_dir_extract $build_dir/ZenLib || ln -sf $zenlib_dir_extract $build_dir/ZenLib
    package_extract $mediainfolib_name $mediainfolib_file $mediainfolib_dir_extract

    local zenlib_params="--enable-static --disable-shared"

    local mediainfolib_params="--disable-shared --enable-minimize-size --enable-minimal --disable-archive \
    --disable-image --disable-tag --disable-text --disable-swf --disable-flv --disable-hdsf4m --disable-cdxa \
    --disable-dpg --disable-pmp --disable-rm --disable-wtv --disable-mxf --disable-dcp --disable-aaf --disable-bdav \
    --disable-bdmv --disable-dvdv --disable-gxf --disable-mixml --disable-skm --disable-nut --disable-tsp \
    --disable-hls --disable-dxw --disable-dvdif --disable-dashmpd --disable-aic --disable-avsv --disable-canopus \
    --disable-ffv1 --disable-flic --disable-huffyuv --disable-prores --disable-y4m --disable-adpcm --disable-amr \
    --disable-amv --disable-ape --disable-au --disable-la --disable-celt --disable-midi --disable-mpc --disable-openmg \
    --disable-pcm --disable-ps2a --disable-rkau --disable-speex --disable-tak --disable-tta --disable-twinvq \
    --disable-references --enable-staticlibs"

    if [ $disable_zlib -eq 0 ]; then
        mediainfolib_params="$mediainfolib_params --with-libz-static"
        mkdir -p $build_dir/Shared/Source/zlib
        #~ ln -sfr $(find $install_dir -name zlib.a) $build_dir/Shared/Source/zlib/libz.a
        ln -sfr $install_dir/lib/libz.a $build_dir/Shared/Source/zlib/libz.a || ln -sf $install_dir/lib/libz.a $build_dir/Shared/Source/zlib/libz.a
        ln -sfr $install_dir/include/zlib.h $build_dir/Shared/Source/zlib/zlib.h || ln -sf $install_dir/include/zlib.h $build_dir/Shared/Source/zlib/zlib.h
        ln -sfr $install_dir/include/zconf.h $build_dir/Shared/Source/zlib/zconf.h || ln -sf $install_dir/include/zconf.h $build_dir/Shared/Source/zlib/zconf.h
    fi

    package_configure $zenlib_name $zenlib_dir $install_dir "$zenlib_params" #TODO: tal vez install dir ha de ser ./ZenLib!!! casi 100% seguro
    #~ package_configure $zenlib_name $zenlib_dir "$build_dir/ZenLib" "$zenlib_params" #TODO: tal vez install dir ha de ser ./ZenLib!!! casi 100% seguro

    package_build $zenlib_name $zenlib_dir
    #package_install $zenlib_name $zenlib_dir $install_dir
    package_install $zenlib_name $zenlib_dir "$build_dir/ZenLib" $zenlib_md5

    package_configure $mediainfolib_name $mediainfolib_dir $install_dir "$mediainfolib_params" #TODO: tal vez install dir ha de ser ./ZenLib!!! casi 100% seguro
    CR=$(printf '\r')
    cat << EOF > $build_dir/mediainfopatch
--- MediaInfo_Config.cpp
+++ MediaInfo_Config_new.cpp
@@ -1083,7 +1083,7 @@
     }$CR
     if (Option_Lower==__T("maxml_fields"))$CR
     {$CR
-        #if MEDIAINFO_ADVANCED$CR
+        #if MEDIAINFO_ADVANCED && defined(MEDIAINFO_XML_YES)$CR
             return MAXML_Fields_Get(Value);$CR
         #else // MEDIAINFO_ADVANCED$CR
             return __T("advanced features are disabled due to compilation options");$CR
@@ -2652,6 +2652,7 @@
 #endif // MEDIAINFO_ADVANCED$CR
 $CR
 #if MEDIAINFO_ADVANCED$CR
+#ifdef MEDIAINFO_XML_YES$CR
 extern Ztring Xml_Name_Escape_0_7_78 (const Ztring &Name);$CR
 Ztring MediaInfo_Config::MAXML_Fields_Get (const Ztring &StreamKind_String)$CR
 {$CR
@@ -2691,6 +2692,7 @@
     List.Separator_Set(0, __T(","));$CR
     return List.Read();$CR
 }$CR
+#endif$CR
 #endif // MEDIAINFO_ADVANCED$CR
 $CR
 //***************************************************************************$CR
EOF
    (cd $mediainfolib_dir/../../../Source/MediaInfo; patch MediaInfo_Config.cpp < $build_dir/mediainfopatch)

    package_build $mediainfolib_name $mediainfolib_dir

    pushd $build_dir/ZenLib >/dev/null
    package_install $mediainfolib_name $build_dir/$mediainfolib_dir $install_dir $mediainfolib_md5
    popd

}

# we can't build vanilla ReadLine under MinGW
readline_win_pkg() {
    local build_dir=$1
    local install_dir=$2
    local name="Readline"
    local readline_ver="5.0.1"
    local readline_url="http://downloads.sourceforge.net/project/gnuwin32/readline/5.0-1/readline-5.0-1-bin.zip?r=&ts=1468492036&use_mirror=freefr"
    local readline_md5="91beae8726edd7ad529f67d82153e61a"
    local readline_file="readline-bin.zip"
    local readline_dir="readline-bin"

    if [ $incremental -eq 1 ] && [ -e $status_dir/$name.success ] && [ `cat $status_dir/$name.success` = $readline_md5 ]; then
        echo "$name already built"
        return
    else
        rm -f $status_dir/$name.success
    fi

    package_download $name $readline_url $readline_file $readline_md5
    if [ $download_only -eq 1 ]; then
        return
    fi

    package_extract $name $readline_file $readline_dir

    # manually copy binary files
    cp -R $readline_dir/include/* $install_dir/include/ || exit 1
    # fix library name
    cp $readline_dir/lib/libreadline.dll.a $install_dir/lib/libreadline.a || exit 1
    echo $readline_md5 > $status_dir/$name.success
}

already_built()
{
    local hash=$2
    local name=$1
    local file=$status_dir/$name.success

    test $incremental -eq 1 \
         -a "$(cat $file 2> /dev/null)" = "$hash" \
      && echo "$name already built" \
      && return

    rm -f $file

    false
}

gtest_pkg() {
    local build_debug=$3
    local build_directory=$1
    local hash="e82199374acdfda3f425331028eb4e2a"
    local install_directory=$2
    local name="gtest"
    local version="1.12.1"
    local file="release-$version.tar.gz"
    local directory="googletest-${file%.tar.gz}"
    local uri="https://github.com/google/googletest/archive/refs/tags/$file"

    already_built $name $hash && return

    package_download $name $uri $file $hash

    test $download_only -eq 1 && return

    package_extract $name $file $directory

    rm -f $build_directory/$name
    ln -rs $directory $build_directory/$name

    build_directory=$build_directory/$name/build

    build_debug="$(test -n "$build_debug" && echo Debug || echo Release)"

    rm -rf $build_directory

    mkdir $build_directory
    pushd $build_directory

    cmake -D CMAKE_BUILD_TYPE="$build_debug" \
          -D CMAKE_INSTALL_LIBDIR="$install_directory/lib" \
          -D CMAKE_INSTALL_PREFIX="$install_directory" \
          ..

    make
    make install

    popd
}

build_sdk() {
    local install_dir=$1
    local debug=$2
    local static_flags=""
    local readline_flags=""
    local freeimage_flags=""
    local libuv_flags=""
    local libraw_flags=""
    local megaapi_flags=""
    local openssl_flags=""
    local sodium_flags="--without-sodium"
    local cwd=$(pwd)
    local gtest_flags=""
    local test_flags=""

    if [ $incremental -eq 1 ] && [ -e $status_dir/MegaSDK.success ]; then
        echo "MegaSDK already built"
        return
    else
        rm -f $status_dir/MegaSDK.success
    fi

    echo "Configuring MEGA SDK"

    ./autogen.sh || exit 1

    # use either static build (by the default) or dynamic
    if [ $use_dynamic -eq 1 ]; then
        static_flags="--enable-shared"
    else
        static_flags="--disable-shared --enable-static"
    fi

    # disable freeimage
    if [ $disable_freeimage -eq 0 ]; then
        freeimage_flags="--with-freeimage=$install_dir"
    else
        freeimage_flags="--without-freeimage"
    fi

    # disable external sqlite
    if [ $disable_sqlite -eq 0 ]; then
        sqlite_flags="--with-sqlite=$install_dir"
    fi

    # enable libuv
    if [ $enable_libuv -eq 1 ]; then
        libuv_flags="--with-libuv=$install_dir"
    else
        libuv_flags="--without-libuv"
    fi

    # enable libraw
    if [ $enable_libraw -eq 1 ]; then
        libraw_flags="--with-libraw=$install_dir"
    fi

    # use local or system MediaInfo
    local mediainfo_flags=""
    if [ $disable_mediainfo -eq 0 ]; then
        mediainfo_flags="--with-libzen=$install_dir --with-libmediainfo=$install_dir"
    fi

    # enable megaapi
    if [ $enable_megaapi -eq 0 ]; then
        megaapi_flags="--disable-megaapi"
    fi

    # add readline and termcap flags if building examples
    if [ $readline_build -eq 1 ] || [ -z "$no_examples" ]; then
        readline_flags=" \
            --with-readline=$install_dir \
            --with-termcap=$install_dir \
            "
    fi

    if [ $disable_ssl -eq 0 ]; then
        openssl_flags="--with-openssl=$install_dir"
    fi

    if [ $enable_sodium -eq 1 ]; then
        sodium_flags="--with-sodium=$install_dir"
    fi

    test $build_gtest -eq 1 && gtest_flags="--with-gtest=$install_dir"
    test $build_tests -eq 1 && test_flags="--enable-tests"

    if [ "$(expr substr $(uname -s) 1 10)" != "MINGW32_NT" ]; then
        # Gcc, CLang warnings as errors
        if [ ${warning_as_error} -eq 1 ];
        then
            export CXXFLAGS="${CXXFLAGS} -Werror"
        fi
        local configure_flags="\
            $configure_cross_options \
            $static_flags \
            --disable-silent-rules \
            --disable-curl-checks \
            $megaapi_flags \
            $openssl_flags \
            --with-cryptopp=$install_dir \
            $sodium_flags \
            --with-zlib=$install_dir \
            $sqlite_flags \
            --with-cares=$install_dir \
            --with-curl=$install_dir \
            $freeimage_flags \
            $libuv_flags \
            $libraw_flags \
            $readline_flags \
            $enable_drive_notifications \
            $disable_posix_threads \
            $no_examples \
            $config_opts \
            $mediainfo_flags \
            $gtest_flags \
            $test_flags \
            --prefix=$install_dir \
            $debug"
        echo "running: ./configure $configure_flags"
        ./configure $configure_flags || exit 1
    # Windows (MinGW) build, uses WinHTTP instead of cURL + c-ares, without OpenSSL
    else
        ./configure \
            $static_flags \
            --disable-silent-rules \
            --without-openssl \
            $megaapi_flags \
            --with-cryptopp=$install_dir \
            $sodium_flags \
            --with-zlib=$install_dir \
            $sqlite_flags \
            --without-cares \
            --without-curl \
            --with-winhttp=$cwd \
            $freeimage_flags \
            $libuv_flags \
            $libraw_flags \
            $readline_flags \
            $enable_drive_notifications \
            $disable_posix_threads \
            $no_examples \
            $config_opts \
            $mediainfo_flags \
            $gtest_flags \
            $test_flags \
            --prefix=$install_dir \
            $debug || exit 1
    fi

    echo "MEGA SDK is configured"

    if [ $configure_only -eq 0 ]; then
        echo "Building MEGA SDK"
        make clean
        if [ "$(expr substr $(uname -s) 1 10)" != "MINGW32_NT" ]; then
            make -j9 || exit 1
        else
            make
        fi
        make install
        local exit_code=$?
        if [ $exit_code -eq 0 ]; then
            echo $exit_code > $status_dir/MegaSDK.success
        fi
    fi
}

display_help() {
    local app=$(basename "$0")
    echo ""
    echo "Usage:"
    echo " $app [-a] [-c] [-h] [-d] [-e] [-f] [-g] [-l] [-L] [-m opts] [-n] [-N] [-o path] [-p path] [-q] [-r] [-s] [-t] [-w] [-x opts] [-y] [z] [-0] [-E]"
    echo ""
    echo "By the default this script builds static megacli executable."
    echo "This script can be run with numerous options to configure and build MEGA SDK."
    echo ""
    echo "Options:"
    echo " -a : Enable MegaApi"
    echo " -b : Only build dependencies"
    echo " -c : Configure MEGA SDK and exit, do not build it"
    echo " -d : Enable debug build"
    echo " -e : Enable cares"
    echo " -f : Disable FreeImage"
    echo " -g : Enable curl"
    echo " -i : Disable external media info"
    echo " -I : Incremental build.  Already built dependencies will be skipped"
    echo " -l : Use local software archive files instead of downloading"
    echo " -L : Disable external sqlite3"
    echo " -n : Disable example applications"
    echo " -N : Enable Drive Notifications (libudev / wbemuuid)"
    echo " -s : Disable OpenSSL"
    echo " -r : Enable Android build"
    echo " -R : Build ReadLine too (even with example apps disabled)"
    echo " -t : Disable POSIX Threads support"
    echo " -u : Enable Sodium cryptographic library"
    echo " -v : Enable libuv"
    echo " -w : Download software archives and exit"
    echo " -y : Build dynamic library and executable (instead of static)"
    echo " -m [opts]: make options"
    echo " -x [opts]: configure options"
    echo " -X [args]: Indicate that we are cross-compiling (and so don't call any ./config scripts)"
    echo " -C [args]: cross-compile flags (--host etc) for 'configure' scripts (when using -X)"
    echo " -O [arg]: pass the os/compiler flag for OpenSSL (when using -X)"
    echo " -o [path]: Directory to store and look for downloaded archives"
    echo " -p [path]: Installation directory"
    echo " -q : Use Crypto++"
    echo " -z : Disable libz"
    echo " -0 : Turn off optimisations (in case of issues on old compilers)"  
    echo " -E : Treat compiler warnings as errors for the SDK code)"
    echo " -G : Build GoogleTest (if CMake is present)"
    echo " -T : Build integration tests"
    echo " -F : Enable FUSE support (libfuse)"
    echo ""
}

main() {
    local cwd=$(pwd)
    local work_dir=$cwd"/sdk_build"
    local build_dir=$work_dir/"build"
    local install_dir=$work_dir/"install"
    local debug=""
    # by the default store archives in work_dir
    local_dir=$work_dir
    status_dir=$work_dir

    while getopts ":habcdefgiIlLm:nNo:p:rRsS:tuvyx:XC:O:wWqz0EGTF" opt; do
        case $opt in
            h)
                display_help $0
                exit 1
                ;;
            a)
                echo "* Enabling MegaApi"
                enable_megaapi=1
                ;;
            b)
                only_build_dependencies=1
                echo "* Building dependencies only."
                ;;
            c)
                echo "* Configure only"
                configure_only=1
                ;;
            d)
                echo "* DEBUG build"
                debug="--enable-debug"
                ;;
            e)
                echo "* Enabling external c-ares"
                enable_cares=1
                ;;
            f)
                echo "* Disabling external FreeImage"
                disable_freeimage=1
                ;;
            g)
                echo "* Enabling external Curl"
                enable_curl=1
                ;;
            i)
                echo "* Disabling external MediaInfo"
                disable_mediainfo=1
                ;;
            I)
                echo "* Incremental build - skipping already built/downloaded dependencies"
                [ -d $persistent_path ] && echo "* Using $persistent_path as a backup for dependecies."
                incremental=1
                ;;
            l)
                echo "* Using local files"
                use_local=1
                ;;
            L)
                echo "* Disabling external sqlite3"
                disable_sqlite=1
                ;;
            m)
                make_opts="$OPTARG"
                ;;
            n)
                no_examples="--disable-examples"
                ;;
            N)
                enable_drive_notifications="--enable-drive-notifications"
                echo "* Enabling Drive Notifications (libudev / wbemuuid)."
                ;;
            o)
                local_dir=$(readlink -f $OPTARG)
                if [ ! -d $local_dir ]; then
                    mkdir -p $local_dir || exit 1
                fi
                echo "* Storing local archive files in $local_dir"
                ;;
            p)
                install_dir=$(readlink -f $OPTARG)
                echo "* Installing into $install_dir"
                ;;
            q)
                echo "* Enabling external Crypto++"
                enable_cryptopp=1
                ;;
            r)
                echo "* Building for Android"
                android_build=1
                ;;
            R)
                echo "* Building readline for clients"
                readline_build=1
                ;;
            s)
                echo "* Disabling OpenSSL"
                disable_ssl=1
                ;;
            S)
                echo "* extra OpenSSL config params: $OPTARG"
                extra_openssl_params="$OPTARG"
                ;;
            t)
                disable_posix_threads="--disable-posix-threads"
                ;;
            u)
                enable_sodium=1
                echo "* Enabling external Sodium."
                ;;
            v)
                enable_libuv=1
                echo "* Enabling external libuv."
                ;;
            w)
                download_only=1
                echo "* Downloading software archives only."
                ;;
            W)
                enable_libraw=1
                echo "* Enabling external libraw."
                ;;
            x)
                config_opts="$OPTARG"
                echo "* Using configuration options: $config_opts"
                ;;
            X)
                cross_compiling=1
                echo "* cross-compiling"
                ;;
            C)
                configure_cross_options="$OPTARG"
                echo "* configure cross compile options: $configure_cross_options"
                ;;
            O)
                openssl_cross_option="$OPTARG"
                echo "* OpenSSL Configure option: $openssl_cross_option"
                ;;
            y)
                use_dynamic=1
                echo "* Building dynamic library and executable."
                ;;
            z)
                disable_zlib=1
                echo "* Disabling external libz."
                ;;
            0)
                no_optimisation=1
                echo "* Disabling compiler optimisations."  # some older versions of gcc have optimisations problems with eg. OpenSSL - rsa_test suite can fail
                ;;
            E)
                warning_as_error=1
                echo "* Treat Compiler Warnings as Errors for the SDK code"
                ;;
            \?)
                display_help $0
                exit 1
                ;;
            G)
                build_gtest=1
                ;;
            T)
                build_gtest=1
                build_tests=1
                ;;
            *)
                display_help $0
                exit 1
                ;;
        esac
    done
    shift $((OPTIND-1))

    check_apps

    if [ "$(expr substr $(uname -s) 1 10)" = "MINGW32_NT" ]; then
        if [ ! -f "$cwd/winhttp.h" -o ! -f "$cwd/winhttp.lib"  ]; then
            echo "ERROR! Windows build requires WinHTTP header and library to be present in MEGA SDK project folder!"
            echo "Please get both winhttp.h and winhttp.lib files an put them into the MEGA SDK project's root folder."
            exit 1
        fi
    fi

    trap on_exit_error EXIT

    if [ $download_only -eq 0 ]; then
        if [ ! -d $build_dir ]; then
            mkdir -p $build_dir || exit 1
        fi
        if [ ! -d $install_dir ]; then
            mkdir -p $install_dir || exit 1
        fi

        cd $build_dir
    fi

    if [ $incremental -eq 0 ]; then
        rm -fr *.log
    else
        # Check if backup exists on persistent path and restore previous status.
        if [ -d $persistent_path/sdk/ ];then
            echo "Recovering previous libs and staus, if any."
            if [ -d $persistent_path/sdk/3rdparty ]; then
                cp --preserve=timestamps -r $persistent_path/sdk/3rdparty/* $install_dir/
                sed -i "s#/[^ ]*/lib\([^[:alnum:]]\)#$install_dir/lib\1#g" $install_dir/lib*/*.la
            fi
            [ -d $persistent_path/sdk/3rdparty_status ] && cp --preserve=timestamps $persistent_path/sdk/3rdparty_status/* $status_dir/
        fi
    fi

    export PREFIX=$install_dir
    local old_pkg_conf=$PKG_CONFIG_PATH
    export PKG_CONFIG_PATH=$install_dir/lib/pkgconfig/
    export LD_LIBRARY_PATH="$install_dir/lib"
    export LD_RUN_PATH="$install_dir/lib"

    if [ $android_build -eq 1 ]; then
        echo "SYSROOT: $SYSROOT"
    fi

    if [ "$(expr substr $(uname -s) 1 10)" != "MINGW32_NT" ]; then
        if [ $disable_ssl -eq 0 ]; then
            openssl_pkg $build_dir $install_dir
        fi
    fi

    if [ $enable_cryptopp -eq 1 ]; then
        cryptopp_pkg $build_dir $install_dir
    fi
   
    if [ $enable_sodium -eq 1 ]; then
        sodium_pkg $build_dir $install_dir
    fi

    if [ $disable_zlib -eq 0 ]; then
        zlib_pkg $build_dir $install_dir
    fi
    
    if [ $disable_sqlite -eq 0 ]; then
        sqlite_pkg $build_dir $install_dir
    fi
       
    if [ $enable_cares -eq 1 ]; then
        cares_pkg $build_dir $install_dir
    fi

    if [ $enable_curl -eq 1 ]; then
        curl_pkg $build_dir $install_dir
    fi

    if [ $enable_libuv -eq 1 ]; then
        libuv_pkg $build_dir $install_dir
    fi

    if [ $enable_libraw -eq 1 ]; then
        libraw_pkg $build_dir $install_dir
    fi

    if [ $disable_freeimage -eq 0 ]; then
        freeimage_pkg $build_dir $install_dir $cwd
    fi

    if [ $disable_mediainfo -eq 0 ]; then
        mediainfo_pkg $build_dir $install_dir $cwd
    fi

    # Build readline and termcap if no_examples isn't set
    if [ $readline_build -eq 1 ] || [ -z "$no_examples" ]; then
        if [ "$(expr substr $(uname -s) 1 10)" != "MINGW32_NT" ]; then
            readline_pkg $build_dir $install_dir
            termcap_pkg $build_dir $install_dir
        else
           readline_win_pkg  $build_dir $install_dir
       fi
    fi

    if [ $build_gtest -eq 1 ]; then
        if ! command -v cmake > /dev/null 2>&1; then
            build_gtest=0
            echo "Can't build gtest as cmake isn't present.";
        fi
    fi

    test $build_gtest -eq 1 && gtest_pkg $build_dir $install_dir "$debug"

    if [ $build_tests -eq 1 ]; then
        if [ $build_gtest -eq 0 ]; then
            build_tests=0
            echo "Can't build tests as gtest couldn't be built.";
        fi
    fi

    if [ $download_only -eq 0 ] && [ $only_build_dependencies -eq 0 ]; then
        cd $cwd
    
        #fix libtool bug (prepends some '=' to certain paths)    
        for i in `find $install_dir -name "*.la"`; do sed -i "s#=/#/#g" $i; done
            
        if [ $android_build -eq 1 ]; then
            export "CXXFLAGS=$CXXFLAGS -std=c++11"
        fi
        export "CXXFLAGS=$CXXFLAGS -DCRYPTOPP_MAINTAIN_BACKWARDS_COMPATIBILITY_562"
        build_sdk $install_dir $debug
    fi

    if [ $incremental -eq 1 ]; then
        # Check if persistent path exists to backup current build status.
        if [ -d $persistent_path ];then
            # Creating directories to store current libraries and status.
            mkdir -p $persistent_path/sdk/3rdparty
            mkdir -p $persistent_path/sdk/3rdparty_status
            echo "Using $persistent_path/sdk/ as persistent path to store current build status."
            cp --preserve=timestamps -r $install_dir/* $persistent_path/sdk/3rdparty/
            rm -f $persistent_path/sdk/3rdparty_status/*
            cp --preserve=timestamps $status_dir/*.success $persistent_path/sdk/3rdparty_status/
        fi
    fi

    unset PREFIX
    unset LD_RUN_PATH
    unset LD_LIBRARY_PATH
    export PKG_CONFIG_PATH=$old_pkg_conf
    trap on_exit_ok EXIT
}

main "$@"
