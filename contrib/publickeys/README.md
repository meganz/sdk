Public keys of MEGA SSL certificates to be used with the option `USE_CURL_PUBLIC_KEY_PINNING` 
of the cURL network layer available at compilation time.

That option removes the direct dependency from OpenSSL and uses the new cURL function
`CURLOPT_PINNEDPUBLICKEY` (since cURL 7.39.0) to do public key pinning.

You can read more about this cURL option and the supported SSL backends here:
http://curl.haxx.se/libcurl/c/CURLOPT_PINNEDPUBLICKEY.html

When using that option, files in this folder should be in 
the working directory of the app, or you should change the paths in the option
`CURLOPT_PINNEDPUBLICKEY` on posix/net.cpp to point to the files in this folder.

