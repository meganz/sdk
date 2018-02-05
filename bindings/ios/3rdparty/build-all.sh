#!/bin/sh
		
set -e

# MEGA SDK deps 
sh build-cryptopp.sh
sh build-openssl.sh
sh build-cares.sh
sh build-curl.sh
sh build-libuv.sh
sh build-libsodium.sh

# MEGAchat deps
if [ "$1" == "--enable-chat" ]; then
sh build-libevent2.sh
sh build-libws.sh

# WebRTC deps
if [ "$2" == "--enable-webrtc" ]; then
sh build-webrtc.sh
fi

sh build-megachat.sh $2
fi

echo "Done."

