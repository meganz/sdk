#!/bin/sh
		
set -e

sh build-cryptopp.sh
sh build-openssl.sh
sh build-cares.sh
sh build-curl.sh
sh build-libuv.sh
sh build-libsodium.sh

if [ "$1" == "--enable-chat"]; then
  sh build-expat.sh
  sh build-libevent2.sh
  sh build-libws.sh
  sh build-megachat.sh
fi


echo "Done."

