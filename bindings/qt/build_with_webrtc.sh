#/bin/bash -i
set -e

# Path of the src directory of WebRTC
WEBRTC_SRC="${HOME}/webrtc/src"

ARCH=`uname -m`
CURRENTPATH=`pwd`/3rdparty
CURL_VERSION="7.58.0"
LIBWEBSOCKETS_BRANCH="v2.4-stable"
OPENSSL_PREFIX="${CURRENTPATH}"
QTPATH="$CURRENTPATH/../../../../.."

if [ ! -d "${WEBRTC_SRC}" ]; then
    echo "* WEBRTC_SRC not correctly set. Please edit this file to configure it or put WebRTC in the default path: ${HOME}/webrtc"
    exit 1
fi

if (( $# < 1  || $# > 2)); then
    echo "Usage: $0 <all | clean> [withExamples]";
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

pushd "${WEBRTC_SRC}" > /dev/null
if [ "c1a58bae4196651d2f7af183be1878bb00d45a57" != "`git rev-parse HEAD`" ]; then
  echo ""
  echo "* WARNING!!"
  echo "* You are not using our recommended commit of WebRTC: c1a58bae4196651d2f7af183be1878bb00d45a57"
  echo "* Please consider to switch to that commit this way (in the src folder of WebRTC):"
  echo ""
  echo "  git checkout c1a58bae4196651d2f7af183be1878bb00d45a57"
  echo "  gclient sync"
  echo ""
  read -p "* Do you want to continue anyway? (y|N) " -n 1 c
  echo ""
  if [ "$c" != "y" ]; then
    exit 0
  fi
fi
popd > /dev/null

mkdir -p ${CURRENTPATH}

echo "* Setting up WebRTC"
if [ ! -d "${CURRENTPATH}/webrtc" ]; then

  if [ ! -e "${WEBRTC_SRC}/out/Release-${ARCH}/obj/webrtc/libwebrtc.a" ]; then
    pushd ${WEBRTC_SRC}
    gn gen "out/Release-${ARCH}" --args="is_debug=false is_component_build=false use_custom_libcxx=false is_clang=false use_sysroot=false treat_warnings_as_errors=false"
    ninja -C "out/Release-${ARCH}" webrtc
    popd
  fi

  mkdir -p ${CURRENTPATH}/webrtc
  ln -sf "${WEBRTC_SRC}" ${CURRENTPATH}/webrtc/include

  mkdir -p ${CURRENTPATH}/include
  ln -sf "${WEBRTC_SRC}/third_party/boringssl/src/include/openssl" ${CURRENTPATH}/include/openssl

  mkdir -p ${CURRENTPATH}/lib
  ln -sf "${WEBRTC_SRC}/out/Release-${ARCH}/obj/webrtc/libwebrtc.a" ${CURRENTPATH}/lib/libssl.a
  ln -sf "${WEBRTC_SRC}/out/Release-${ARCH}/obj/webrtc/libwebrtc.a" ${CURRENTPATH}/lib/libcrypto.a
  ln -sf "${WEBRTC_SRC}/out/Release-${ARCH}/obj/webrtc/libwebrtc.a" ${CURRENTPATH}/lib/libwebrtc.a
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
  sed -i 's/\#define USE_RESOLVE_ON_IPS 1//' lib/curl_setup.h

  LIBS=-lpthread ./configure --prefix="${CURRENTPATH}" --enable-static --disable-shared --with-ssl=${OPENSSL_PREFIX} --with-zlib --disable-manual --disable-ftp --disable-file --disable-ldap --disable-ldaps --disable-rtsp --disable-proxy --disable-dict --disable-telnet --disable-tftp --disable-pop3 --disable-imap --disable-smtp --disable-gopher --disable-sspi --enable-ipv6 --disable-smb

  make -j8
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

  cmake . -DCMAKE_INSTALL_PREFIX=${CURRENTPATH} -DCMAKE_LIBRARY_PATH=${CURRENTPATH}/lib -DCMAKE_INCLUDE_PATH=${CURRENTPATH}/include -DOPENSSL_INCLUDE_DIR=${OPENSSL_PREFIX}/include -DOPENSSL_SSL_LIBRARY=${OPENSSL_PREFIX}/lib/libssl.a -DOPENSSL_CRYPTO_LIBRARY=${OPENSSL_PREFIX}/lib/libcrypto.a -DOPENSSL_ROOT_DIR=${OPENSSL_PREFIX} -DLWS_WITH_LIBUV=1 -DLWS_IPV6=ON -DLWS_SSL_CLIENT_USE_OS_CA_CERTS=0 -DLWS_WITH_SHARED=OFF -DLWS_WITHOUT_TESTAPPS=ON -DLWS_WITHOUT_SERVER=ON

  make -j8
  make install
  popd
  popd
  echo "* libwebsockets is ready"

else
  echo "* libwebsockets already configured"
fi

if [ "$2" == "withExamples" ]; then
	if [[ ! -e $QTPATH/build ]]; then
	    mkdir $QTPATH/build
	fi

	cd $QTPATH/build
    qmake $QTPATH/contrib/QtCreator/MEGAchat.pro -spec linux-g++ CONFIG+=qml_debug CONFIG+=force_debug_info CONFIG+=separate_debug_info && /usr/bin/make qmake_all
	cd $QTPATH/build/MEGAChatQt/
	make -j8
fi


