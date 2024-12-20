## CREDITS

--------------------------------------------------------------------
#### Dependencies of the MEGA C++ SDK
Here is a brief description of all of them:

#### c-ares:
Copyright 1998 by the Massachusetts Institute of Technology.

c-ares is a C library for asynchronous DNS requests (including name resolves)

http://c-ares.haxx.se/

License: MIT license

http://c-ares.haxx.se/license.html

#### libcurl
Copyright (C) 1998 - 2016, Daniel Stenberg, <daniel@haxx.se>, et al.

The multiprotocol file transfer library

https://curl.haxx.se/libcurl/

License:  MIT/X derivate license

https://curl.haxx.se/docs/copyright.html

#### Crypto++
Copyright (c) 1995-2013 by Wei Dai. (for the compilation) and public domain (for individual files)

Crypto++ Library is a free C++ class library of cryptographic schemes.

https://www.cryptopp.com/

License: Crypto++ Library is copyrighted as a compilation and (as of version 5.6.2) 

licensed under the Boost Software License 1.0, while the individual files in 
the compilation are all public domain.

#### OpenSSL
Copyright (c) 1998-2016 The OpenSSL Project.  All rights reserved.

A toolkit implementing SSL v2/v3 and TLS protocols with full-strength cryptography world-wide.

https://www.openssl.org/

License: OpenSSL License

https://github.com/openssl/openssl/blob/master/LICENSE

#### libuv
Copyright Joyent, Inc. and other Node contributors. All rights reserved.

libuv is a multi-platform support library with a focus on asynchronous I/O.

https://github.com/libuv/libuv

License: MIT

https://github.com/libuv/libuv/blob/v1.x/LICENSE

#### freeimage
Copyright (c) 2003-2015 by FreeImage. All rights reserved.

FreeImage is an Open Source library project for developers who would like
 to support popular graphics image formats like PNG, BMP, JPEG, TIFF and 
 others as needed by today's multimedia applications.

This software uses the FreeImage open source image library. 
See http://freeimage.sourceforge.net for details.

License: FreeImage Public License - Version 1.0. 

http://freeimage.sourceforge.net/freeimage-license.txt

#### SQLite
SQLite is an in-process library that implements a self-contained, serverless, zero-configuration, transactional SQL database engine.

http://www.sqlite.org/

License: Public Domain

http://www.sqlite.org/copyright.html

### Libraries included in this repo

#### utf8proc
A clean C library for processing UTF-8 Unicode data

https://julialang.org/utf8proc/

License: MIT "expat" license

https://github.com/JuliaLang/utf8proc/blob/master/LICENSE.md

Files included in this repository based on `utf8proc`:
- `src/mega_utf8proc.cpp` (based on `utf8proc.c`)
- `src/mega_utf8proc_data.c` (based on `utf8proc_data.c`)
- `include/mega/mega_utf8proc.h` (based on `utf8proc.h`)
- `third_party/utf8proc/LICENSE` (copy of the licence of `utf8proc`)

#### Cron expression parsing in ANSI C
A clean C library for processing cron expresions and obtaining epoch times

https://github.com/staticlibs/ccronexpr

License:  Apache License 2.0

Files included in this repository based on `ccronexpr`:
- `third_party/ccronexpr/mega_ccronexpr.cpp` (based on `ccronexpr.c`)
- `third_party/ccronexpr/mega_ccronexpr.h` (based on `ccronexpr.h`)
- `third_party/ccronexpr/LICENSE` (copy of the licence of `ccronexpr`)

#### http_parser
HTTP request/response parser for C

https://github.com/nodejs/http-parser

License: MIT

https://github.com/nodejs/http-parser/blob/master/LICENSE-MIT

Files included in this repository based on `http_parser`:
- `src/mega_http_parser.cpp` (based on `http_parser.c`)
- `include/mega/mega_http_parser.h` (based on `http_parser.h`)
- `third_party/http_parser/AUTHORS` (copy of the `AUTHORS` file of `http_parser`)
- `third_party/http_parser/LICENSE-MIT` (copy of the licence of `http_parser`)

#### zxcvbn-c
C/C++ version of the zxcvbn password strength estimator

https://github.com/tsyrogit/zxcvbn-c

License: MIT

https://github.com/tsyrogit/zxcvbn-c/blob/master/LICENSE.txt

Files included in this repository based on `zxcvbn-c`:
- `src/mega_zxcvbn.cpp` (based on `zxcvbn.c`)
- `include/mega/mega_zxcvbn.h` (based on `zxcvbn.h`)
- `include/mega/mega_dict-src.h` (dictionary file generated with the same wordlist as our webclient)
- `third_party/zxcvbn-c/README.md` (copy of the `README.MD` file of `zxcvbn-c`)
- `third_party/zxcvbn-c/LICENSE.txt` (copy of the licence of `zxcvbn-c`)

#### evt-tls
evt-tls is an abstraction layer of OpenSSL using bio pair to expose callback based asynchronous API
 and should integrate easily with any event based networking library like libuv,
 libevent and libev or any other network library which want to use OpenSSL as an state machine

License: MIT

Copyright (c) 2015 Devchandra M. Leishangthem

https://github.com/deleisha/evt-tls

Files included in this repository based on `evt-tls`:
- `src/mega_evt_tls.cpp` (based on `evt_tls.c`)
- `include/mega/mega_evt_tls.h` (based on `evt_tls.h`)
- `include/mega/mega_evt_queue.h` (based on `queue.h`)

#### vincentlaucsb/csv-parser
A high-performance, fully-featured CSV parser and serializer for modern C++.

https://github.com/vincentlaucsb/csv-parser

License: MIT

https://github.com/vincentlaucsb/csv-parser/blob/master/LICENSE

Files included in this repository based on `vincentlaucsb/csv-parser`:
- `include/mega/mega_csv.h` (based on `csv.hpp`)
