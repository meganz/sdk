#!/bin/sh
		
set -e

# MEGA SDK deps 
sh build-cryptopp.sh
sh build-openssl.sh
sh build-cares.sh
sh build-curl.sh
sh build-libuv.sh
sh build-libsodium.sh
sh build-mediainfolib.sh

# MEGAchat deps
if [ "$1" == "--enable-chat" ]; then
sh build-libwebsockets.sh
fi

echo "Done."

