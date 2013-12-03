MEGA SDK - Client Access Engine
===============================

Building
--------

    sh autogen.sh
    ./configure
    make
    sudo make install


Usage
-----

Take a look at the sample project in `doc/example` on how to use Mega
SDK in your applications.  In order to compile and link your
application with Mega SDK library you can use `pkg-config` script:

    g++ $(pkg-config --cflags libmega) -o main.o -c main.cpp
    g++ $(pkg-config --libs libmega) -o app main.o


Platform Dependencies
---------------------

The following notes are intended for building and running build
artefacts.


### POSIX (Linux/MacOS/BSD/...)

Install the following development packages, if available, or download
and compile their respective sources (given package names are for
Debian and Red Hat derivatives respectively):

* Crypto++ (`libcrypto++-dev`, `cryptopp-deve`l)
* cURL (`libcurl-dev`, `curl-devel`)

The following might be additionally required for the reference CLI
(command line interface) client implementation:

* GNU Readline (`libreadline-dev`, `readline-devel`)
* FreeImage (`libfreeimage-dev`, `freeimage-devel`)
* Berkeley DB C++ (`libdb++-dev`, `db4-devel`)

CAUTION: Verify that the installed `libcurl` uses c-ares for
asynchronous name resolution.  If that is not the case, compile it
from the original sources with `--enable-ares`.  Do *NOT* use
`--enable-threaded-resolver`, which will cause the engine to hang.
Bear in mind that by not enabling asynchronous DNS resolving at all,
the engine is no longer non-blocking.

Ensure that your terminal supports UTF-8 if you want to see and
manipulate non-ASCII filenames.


### Windows

To build megaclient.exe under Windows, you'll need to following:

* Crypto++ (original sources or precompiled)
* A Windows-native C++ development environment (e.g. MinGW or Visual Studio)

(You do not need cURL, as megaclient's Win32 version relies on WinHTTP
for network access.)

The following might be additionally required for the reference CLI
(command line interface) client implementation:

* GNU Readline/Termcap (original sources or precompiled)
* Berkeley DB C++ (original sources or precompiled)
* FreeImage (original sources or precompiled)

First, compile the components that you have obtained as source code.

NOTE: There are currently some limitations of the Windows version.
