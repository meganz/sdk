#!/bin/bash -i
set -e

# Path of the src directory of WebRTC
[ -z ${WEBRTC_SRC+x} ] && WEBRTC_SRC="${HOME}/webrtc/src"

ARCH=`uname -m`
CURRENTPATH=`pwd`/3rdparty
CURL_VERSION="7.58.0"
LIBWEBSOCKETS_BRANCH="v4.2-stable"
OPENSSL_PREFIX="${CURRENTPATH}"
QTPATH="$CURRENTPATH/../../../../.."

if [ ! -d "${WEBRTC_SRC}" ]; then
    echo "* WEBRTC_SRC not correctly set. Please edit this file to configure it, put WebRTC in the default path: ${HOME}/webrtc or set WEBRTC_SRC environment variable"
    exit 1
fi

if (( $# < 1  || $# > 2)); then
    echo "Usage: $0 <all | clean>";
    exit 0
fi

if [ "$1" == "clean" ]; then
    rm -rf ${CURRENTPATH}/curl-*
    rm -rf ${CURRENTPATH}/webrtc
    rm -rf ${CURRENTPATH}/include/openssl
    rm -rf ${CURRENTPATH}/lib/libssl.a
    rm -rf ${CURRENTPATH}/lib/libcrypto.a
    rm -rf ${CURRENTPATH}/lib/libwebrtc.a

    rm -rf ${CURRENTPATH}/include/curl
    rm -rf ${CURRENTPATH}/lib/libcurl*
    rm -rf ${CURRENTPATH}/lib/pkgconfig
    rm -rf ${CURRENTPATH}/bin
    rm -rf ${CURRENTPATH}/share

    rm -rf ${CURRENTPATH}/lib/libwebsockets*
    rm -rf ${CURRENTPATH}/libwebsockets
    rm -rf ${CURRENTPATH}/lib/cmake
    rm -rf ${CURRENTPATH}/include/libwebsockets.h
    rm -rf ${CURRENTPATH}/include/lws*
    exit 0
fi

mkdir -p ${CURRENTPATH}

echo "* Setting up WebRTC"
pushd "${WEBRTC_SRC}" > /dev/null
if [ "93081d594f7efff72958a79251f53731b99e902b" != "`git rev-parse HEAD`" ]; then
  echo ""
  echo "* WARNING!!"
  echo "* You are not using our recommended commit of WebRTC: 93081d594f7efff72958a79251f53731b99e902b (branch-heads/5359 release 108)"
  echo "* Please consider to switch to that commit this way (in the src folder of WebRTC):"
  echo ""
  echo "  git checkout 93081d594f7efff72958a79251f53731b99e902b"
  echo "  gclient sync"
  echo ""
  read -p "* Do you want to continue anyway? (y|N) " -n 1 c
  echo ""
  if [ "$c" != "y" ]; then
    exit 0
  fi
fi
popd > /dev/null

if [ ! -d "${CURRENTPATH}/webrtc" ] ; then

  if [ ! -e "${WEBRTC_SRC}/out/Release-${ARCH}/obj/libwebrtc.a" ]; then
    pushd ${WEBRTC_SRC}
    gn gen "out/Release-${ARCH}" --args="is_debug=false is_component_build=false use_custom_libcxx=false is_clang=false use_sysroot=false treat_warnings_as_errors=false fatal_linker_warnings=false rtc_include_tests=false"  
    ninja -C "out/Release-${ARCH}" webrtc
    popd
  fi

  mkdir -p ${CURRENTPATH}/webrtc
  ln -sf "${WEBRTC_SRC}" ${CURRENTPATH}/webrtc/include

  mkdir -p ${CURRENTPATH}/include
  rm -rf ${CURRENTPATH}/include/openssl
  ln -sf "${WEBRTC_SRC}/third_party/boringssl/src/include/openssl" ${CURRENTPATH}/include/openssl
  mkdir -p ${CURRENTPATH}/lib
  # use libssl and libcrypto that have been embedded into libwebrtc
  ln -sf "${WEBRTC_SRC}/out/Release-${ARCH}/obj/libwebrtc.a" ${CURRENTPATH}/lib/libssl.a
  ln -sf "${WEBRTC_SRC}/out/Release-${ARCH}/obj/libwebrtc.a" ${CURRENTPATH}/lib/libcrypto.a
  ln -sf "${WEBRTC_SRC}/out/Release-${ARCH}/obj/libwebrtc.a" ${CURRENTPATH}/lib/libwebrtc.a
  echo "* WebRTC is ready"
else
  echo "* WebRTC already configured"
fi

if [ ! -e ${CURRENTPATH}/libs ]; then
  ln -sf lib ${CURRENTPATH}/libs
fi

echo "* Setting up cURL"
if [ ! -e "${CURRENTPATH}/lib/libcurl.a" ]; then

  pushd ${CURRENTPATH}
  if [ ! -e "curl-${CURL_VERSION}.tar.gz" ]; then
    echo "Downloading cURL"
    curl -LO "https://curl.haxx.se/download/curl-${CURL_VERSION}.tar.gz"
  fi

  echo "Building cURL"
  rm -rf curl-${CURL_VERSION}
  tar zxf curl-${CURL_VERSION}.tar.gz
  pushd "curl-${CURL_VERSION}"

  # Do not resolve IPs!!
  sed -i 's/\#define USE_RESOLVE_ON_IPS 1//' lib/curl_setup.h || sed -i'.bak' 's/\#define USE_RESOLVE_ON_IPS 1//' lib/curl_setup.h

  echo "Openssl prefix"
  echo ${OPENSSL_PREFIX}
  echo "CC=gcc -lstdc++ LIBS=-lpthread ./configure --prefix="${CURRENTPATH}" --enable-static --disable-shared --with-ssl=${OPENSSL_PREFIX} --with-zlib --disable-manual --disable-ftp --disable-file --disable-ldap --disable-ldaps --disable-rtsp --disable-proxy --disable-dict --disable-telnet --disable-tftp --disable-pop3 --disable-imap --disable-smtp --disable-gopher --disable-sspi --enable-ipv6 --disable-smb --without-libidn2"

  if [[ $(uname) == 'Darwin' ]]; then
	CC="gcc -lstdc++" LIBS=-lpthread ./configure --prefix="${CURRENTPATH}" --enable-static --disable-shared --without-brotli --with-ssl=${OPENSSL_PREFIX} --with-zlib --disable-manual --disable-ftp --disable-file --disable-ldap --disable-ldaps --disable-rtsp --disable-proxy --disable-dict --disable-telnet --disable-tftp --disable-pop3 --disable-imap --disable-smtp --disable-gopher --disable-sspi --enable-ipv6 --disable-smb --without-libidn2
    make -j `sysctl -n hw.physicalcpu`
  else
	CC="gcc" LIBS="-lpthread -lstdc++" ./configure --prefix="${CURRENTPATH}" --enable-static --disable-shared --without-brotli --with-ssl=${OPENSSL_PREFIX} --with-zlib --disable-manual --disable-ftp --disable-file --disable-ldap --disable-ldaps --disable-rtsp --disable-proxy --disable-dict --disable-telnet --disable-tftp --disable-pop3 --disable-imap --disable-smtp --disable-gopher --disable-sspi --enable-ipv6 --disable-smb --without-libidn2
    make -j `nproc`
  fi
  make install
  popd
  popd
  echo "* cURL is ready"

else
  echo "* cURL already configured"
fi

echo "* Setting up libwebsockets"
if [ ! -e "${CURRENTPATH}/lib/libwebsockets.a" ]; then
  pushd ${CURRENTPATH}
  if [ ! -d "libwebsockets" ]; then
    git clone --depth=1 -b ${LIBWEBSOCKETS_BRANCH} https://github.com/warmcat/libwebsockets.git
  fi

  pushd libwebsockets
  git reset --hard && git clean -dfx

  # To build libwebsockets in Debug mode, add -DCMAKE_BUILD_TYPE=DEBUG to the cmake command.
  #
  # Note: This will build in debug mode, but apparently without defining _DEBUG (which influences at least
  # the default log level, see lws-logs.h). If this symbol is required, try adding -D_DEBUG too.
  #
  #
  # It may happen that compilation fails due to warnings that are treated as errors, which is the default. To
  # disable that, add -DDISABLE_WERROR=ON to cmake command.
  if [[ $(uname) == 'Darwin' ]]; then
    cmake . -DCMAKE_INSTALL_PREFIX=${CURRENTPATH} -DCMAKE_LIBRARY_PATH=${CURRENTPATH}/lib -DCMAKE_INCLUDE_PATH=${CURRENTPATH}/include -DLWS_WITH_LIBUV=1 -DLWS_IPV6=ON -DLWS_SSL_CLIENT_USE_OS_CA_CERTS=0 -DLWS_WITH_SHARED=OFF -DLWS_WITHOUT_TESTAPPS=ON -DLWS_WITHOUT_SERVER=ON -DLIBUV_INCLUDE_DIRS=${CURRENTPATH}/include/libuv -DLWS_OPENSSL_INCLUDE_DIRS=${OPENSSL_PREFIX}/include -DLWS_OPENSSL_LIBRARIES="${OPENSSL_PREFIX}/lib/libssl.a;${OPENSSL_PREFIX}/lib/libcrypto.a" -DLWS_WITH_HTTP2=0 -DLWS_WITH_BORINGSSL=ON
    make -j `sysctl -n hw.physicalcpu`
  else
    cmake . -DCMAKE_INSTALL_PREFIX=${CURRENTPATH} -DCMAKE_LIBRARY_PATH=${CURRENTPATH}/lib -DCMAKE_INCLUDE_PATH=${CURRENTPATH}/include -DLWS_WITH_LIBUV=1 -DLWS_IPV6=ON -DLWS_SSL_CLIENT_USE_OS_CA_CERTS=0 -DLWS_WITH_SHARED=OFF -DLWS_WITHOUT_TESTAPPS=ON -DLWS_WITHOUT_SERVER=ON -DLWS_OPENSSL_INCLUDE_DIRS=${OPENSSL_PREFIX}/include -DLWS_OPENSSL_LIBRARIES="${OPENSSL_PREFIX}/lib/libssl.a;${OPENSSL_PREFIX}/lib/libcrypto.a" -DLWS_WITH_HTTP2=0 -DLWS_WITH_BORINGSSL=ON
    make -j `nproc`
  fi
  make install
  popd
  popd
  echo "* libwebsockets is ready"

else
  echo "* libwebsockets already configured"
fi

#link lib/* into libs if libs is not symlink
if [ ! -L ${CURRENTPATH}/libs ]; then
  pushd ${CURRENTPATH}/libs
  ln -sf ../lib/lib* ./
  popd
fi

