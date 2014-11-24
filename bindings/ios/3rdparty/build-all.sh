#!/bin/sh
		
set -e

./build-cryptopp.sh
./build-openssl.sh
./build-cares.sh
./build-curl.sh

echo "Done."
