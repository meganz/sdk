# MEGA SDK - Client Access Engine <a href="https://scan.coverity.com/projects/4315"><img alt="Coverity Scan Build Status" src="https://scan.coverity.com/projects/4315/badge.svg"/></a>

MEGA --- _The Privacy Company_ --- is a Secure Cloud Storage
provider that protects your data thanks to end-to-end encryption. We call it User Controlled Encryption,
or UCE, and all our clients automatically manage it.

All files stored on MEGA are encrypted. All data transfers from and to MEGA are encrypted. And while
most cloud storage providers can and do claim the same, MEGA is different – unlike the industry norm
where the cloud storage provider holds the decryption key, with MEGA, you control the encryption,
you hold the keys, and you decide who you grant or deny access to your files.

This SDK brings you all the power of our client applications and let you create
your own or analyze the security of our products. Are you ready to start? Please continue reading.

SDK Contents
------------

In this SDK, you can find our low level SDK, that was already released few months after the MEGA launch,
a new intermediate layer to make it easier to use and to bind with other programming languages, and
example apps for all our currently supported platforms (Windows, Linux, OSX, Android, iOS and Windows Phone).

In the `examples` folder you can find example apps using:

1. The low level SDK:
  - megacli (a powerful command line tool that allows to use all SDK features)
  - megasimplesync (a command line tool that allows to use the synchronization engine)

2. The intermediate layer:
  - An example app for Visual Studio in `examples/win32`
  - An example app for Android (using Java bindings based on SWIG) in `examples/android`
  - An example app for iOS (using Objective-C bindings) in `examples/iOS`
  - An example app for Windows Phone (using Windows Phone bindings) in `examples/wp8`

[MEGAcmd](examples/megacmd), a higher level command line application that uses the SDK to provide interactive and scriptable access to MEGA, can be found [here](https://github.com/meganz/megacmd).

Building
--------

There are two methods - the one we are using now for most platforms is with vcpkg and cmake, and the prior system based on autotools still works for POSIX based systems.

### Platform Dependencies

Some dependencies are different for each platform because the SDK uses generic interfaces to get some features and they have different implementations.
- Network (cURL with OpenSSL/c-ares or WinHTTP)
- Filesystem access (Posix or Win32)
- Graphics management (FreeImage or iOS frameworks)
- Threads/mutexes (pthread threads, or C++11)
- Drive Notifications (udev for Posix, WMI/WBEM for Win32, or Apple frameworks)

### Building with vcpkg and cmake 

This is the method that works for Windows - it also works for other platforms (and for those, autotools is also an option).
We use vcpkg, cmake, and provide scripts to build the 3rd party libraries and set up the project.

#### Build the SDK and 3rdParty Dependencies with vcpkg + cmake
* The steps to do so are already prepared in the build_from_scratch.cmake script.  It contains instructions too.
* To get started with it, eg for windows, follow these steps:
	* mkdir mybuild
	* cd mybuild
	* git clone https://github.com/meganz/sdk.git
	* cd sdk\contrib\cmake
	* <edit preferred-ports-sdk.txt and comment out the two lines for pdfium>
	* <on Win, choose VS version by editing vcpkg_extra_triplets\xNN-windows-mega.cmake>
	* cmake -DTRIPLET=x64-windows-mega -DEXTRA_ARGS="-DUSE_PDFIUM=0" -P build_from_scratch.cmake
* Visual Studio solution is generated at mybuild\sdk\build-x64-windows-mega
* That folder contains Debug and Release subfolders which contain build products
* Later, if you want to include pdfium (which is used to generate thumbnails and previews for .pdf files), see 3rdparty_deps.txt.
* Similar steps work for other platforms too (Linux with triplet x64-linux (including WSL), Mac with triplet x64-osx-mega).

### Building with POSIX Autotools  (Linux/Darwin/BSD/OSX ...)

For platforms with Autotools, first set up needed libraries and then the generic way to build and install it is:

	sh autogen.sh
	./configure
	make
	sudo make install

Notice that you would need Autotools installed in your system (in Linux this normally entails having `autoconf` and `libtool` packages installed).

That compilation will include the examples using our low level SDK (`megacli` and `megasimplesync`). 
You also have specific build instructions for OSX (`doc/OSX.txt`) and FreeBSD (`doc/FreeBSD.txt`)
and a build script to automatically download and build the SDK along with all its dependencies (`contrib/build_sdk.sh`)

For other platforms, or if you want to see how to use the new intermediate layer,
the easiest way is to get a smooth start is to build one of the examples in subfolders
of the `examples` folder.

All these folders contains a README.md file with information about how to get the project up and running,
including the installation of all required dependencies.

#### Dependencies for POSIX Autotools:

Install the following development packages, if available, or download
and compile their respective sources (package names are for
Debian and RedHat derivatives, respectively):

* cURL (`libcurl4-openssl-dev`, `libcurl-devel`), compiled with `--enable-ssl`
* c-ares (`libc-ares-dev`, `libcares-devel`, `c-ares-devel`)
* OpenSSL (`libssl-dev`, `openssl-devel`)
* Crypto++ (`libcrypto++-dev`, `libcryptopp-devel`)
* zlib (`zlib1g-dev`, `zlib-devel`)
* SQLite (`libsqlite3-dev`, `sqlite-devel`) or configure `--without-sqlite`
* FreeImage (`libfreeimage-dev`, `freeimage-devel`) or configure `--without-freeimage`
* pthread

Optional dependency:
* Libraw (`libraw-dev`, `libraw-devel`)
* Sodium (`libsodium-dev`, `libsodium-devel`), configure `--with-sodium`
* MediaInfoLib (optional, see third_party/README_MediaInfo.txt)
* libudev (`libudev-dev`, `libudev-devel`)

Filesystem event monitoring: The provided filesystem layer implements
the Linux `inotify` and the MacOS `fsevents` interfaces.

To build the reference `megacli` example, you may also need to install:
* GNU Readline (`libreadline-dev`, `readline-devel`)
on Mac, you will probably need to download the source and build it yourself, and adjust the project to refer to that version.

For Android, we provide an additional implementation of the graphics subsystem using Android libraries.

For iOS, we provide an additional implementation of the graphics subsystem using Objective C frameworks.


Usage
-----

The low level SDK doesn't have inline documentation yet. If you want to use it,
please check one of our example apps (`examples/megacli`, `examples/megasimplesync`).

The new intermediate layer has been documented using Doxygen. The only public header that you need
to include to use is `include/megaapi.h`. You can read the documentation in that header file,
or download the same documentation in HTML format from this link:

https://mega.nz/#!7glwEQBT!Fy9cwPpCmuaVdEkW19qwBLaiMeyufB1kseqisOAxfi8

Additional info
---------------

### Folder syncing

In this version, the sync functionality is limited in scope and functionality:

* There is no locking between clients accessing the same remote folder.
Concurrent creation of identically named files and folders can result in
server-side dupes.

* Syncing between clients with differing filesystem naming semantics can
lead to loss of data, e.g. when syncing a folder containing `ABC.TXT` and
`abc.txt` with a Windows client.

* On POSIX platforms, filenames are assumed to be encoded in UTF-8. Invalid
byte sequences can lead to undefined behaviour.

* Local filesystem items must not be exposed to the sync subsystem more
than once. Any dupes, whether by nesting syncs or through filesystem links,
will lead to unexpected results and loss of data.

* No in-place versioning. Deleted remote files can be found in
`//bin/SyncDebris` (only when syncing to the logged in account's own
cloud drive - there is no SyncDebris facility on syncs to inbound
shares), deleted local files in a sync-specific hidden debris
folder located in the local sync's root folder.

* No delta writes. Changed files are always overwritten as a whole, which
means that it is not a good idea to sync e.g. live database tables.

* No direct peer-to-peer syncing. Even two machines in the same local subnet
will still sync via the remote storage infrastructure.

* No support for unidirectional syncing (backup-only, restore-only).
Syncing to an inbound share requires it to have full access rights.

### `megacli` on Windows

The `megacli` example is currently not handling console Unicode
input/output correctly if run in `cmd.exe`.

Filename caveats: Please prefix all paths with `\\?\` to avoid the following
issues:

* The `MAX_PATH` (260 character) length limitation, which would make it
impossible to access files in deep directory structures

* Prohibited filenames (`con`/`prn`/`aux`/`clock$`/`nul`/`com1`...`com9`/`lpt1`...`lpt9`).
Such files and folders will still be inaccessible through e.g. Explorer!

Also, disable automatic short name generation to eliminate the risk of
clashes with existing short names.
