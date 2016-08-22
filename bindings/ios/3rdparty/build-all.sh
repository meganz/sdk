#!/bin/sh
		
set -e

sh build-cryptopp.sh
sh build-openssl.sh
sh build-cares.sh
sh build-curl.sh
sh build-libuv.sh
sh build-libsodium.sh

echo "Done."
