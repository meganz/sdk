[[_TOC_]]

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

## SDK Contents

In this SDK, you can find our low level SDK, that was already released few months after the MEGA launch,
a new intermediate layer to make it easier to use and to bind with other programming languages, and
example apps for all our currently supported platforms (Windows, Linux, OSX, Android and iOS).

In the `examples` folder you can find example apps using:

1. The low level SDK:
  - megacli (a powerful command line tool that allows to use all SDK features)

2. The intermediate layer:
  - An example app for Visual Studio in `examples/win32`
  - An example app for Android (using Java bindings based on SWIG) in `examples/android`
  - An example app for iOS (using Objective-C bindings) in `examples/iOS`

[MEGAcmd](examples/megacmd), a higher level command line application that uses the SDK to provide interactive and scriptable access to MEGA, can be found [here](https://github.com/meganz/megacmd).


## How to build the SDK library

For the SDK development and compilation we mainly use CMake as the cross-platform project configuration tool. We also use VCPKG to manage the required dependencies to build the SDK in most platforms: Windows, MacOS and Linux.

The prior autotools and qmake build systems are still available but obsolete, so their usage is discouraged.

### Building tools

Some common development tools should be available in the system to be able to build the MEGA SDK and the needed dependencies:

- Git: Use the one from your system package manager or install it from https://git-scm.com
- CMake 3.18 or higher: Use the one from your system package manager or install it from https://cmake.org

#### Windows

Ensure you have installed Visual Studio, with the necessary components for building C++ sources, and the Windows SDK on your system.

We recommend using the following version of the above mentioned tools:
 - Visual Studio 2019
 - MSVC v142
 - Windows 10 SDK (10.0.19041.0)

Other versions could work to compile the project but have not been fully tested

You can download Visual Studio 2019 here: https://visualstudio.microsoft.com/vs/older-downloads/

#### MacOS

Xcode and the Developer tools are needed. To install the
Developer tools, run the following command and follow the instructions:

	$ xcode-select --install

The following packages should be available in the system as well:

 - autoconf, autoconf-archive, automake, pkg-config and nasm.

You can use any package manager if you have one installed or build and install them from sources

#### Linux

For debian-based distributions, you can install the needed compilers and tools using the following command:

	sudo apt install build-essential curl zip unzip autoconf autoconf-archive nasm

Package names may vary for other Linux distros, but it should build successfully with similar packages to the ones listed above.

### Prepare the sources

First of all, prepare a directory of your choice to work with the MEGA SDK. The `mega` directory
will be used as the workspace directory in the examples in this document.

	mkdir mega
	cd mega

Then, clone the MEGA SDK repository to obtain the source code for the MEGA SDK.

	git clone https://github.com/meganz/sdk

Next to the MEGA SDK, clone the VCPKG repository. If you are already using VCPKG and have a local clone of the VCPKG repository, you can skip this step and use the VCPKG you already have in your system.

	git clone https://github.com/microsoft/vcpkg

### Configuration

The following instructions are for configuring the project from the CLI, but cmake-gui or any editor or IDE
compatible with CMake should be suitable if the same CMake parameters are configured.

The SDK is configured likne any other regular CMake project. The only parameter that is always needed is the VCPKG directory
to manage the third-party dependencies. To configure the SDK with the dafault options, from the workspace (`mega` directory), run CMake:

	cmake -DVCPKG_ROOT=vcpkg -DCMAKE_BUILD_TYPE=Debug -S sdk -B build_dir

**Note**: The `-DCMAKE_BUILD_TYPE=<Debug|Release>` may not be needed for multiconfig generators, like Visual Studio.

In the command above, relative paths have been used for simplicity. If you want to change the location of VCPKG, the SDK or the build directory, simply provide a valid relative or absolute path for any of them.

During the configuration of the project, VCPKG will build and configure the necessary libraries for the platform. It may take a while on the first run, but once the libraries are built, VCPKG will retrieve them from the binary cache.

Some options to configure the SDK library can be found in the [sdklib_options.cmake](sdk/contrib/cmake/modules/sdklib_options.cmake) file, like ENABLE_SYNC or USE_PDFIUM.
The options to manage the examples and tests are in the [CMakeLists.txt](CMakeLists.txt).

### Building the sources

Once the MEGA SDK is configured, simply build the complete project:

	cmake --build build_dir

You can specify `--target=<target>` like `SDKlib` or `megacli`, or just leave the command as it is to build all the tagets.
Additionally, `-j<N>` can be added to manage concurrency and speed up the build.

Once the build is finished, binaries will be available in the `build_dir`

### Run megacli

To run the example app `megacli`, go to the `examples` directory in the `build_dir` and execute the `megacli` binary.

## How to build the SDK library (Obsolete methods)

### Build the SDK and 3rdParty Dependencies with vcpkg + cmake
* The steps to build the SDK are already prepared in the build_from_scratch.cmake script.  It contains instructions too.
* To get started with it, eg for windows, follow these steps:
	* mkdir mybuild
	* cd mybuild
	* git clone https://github.com/meganz/sdk.git
	* cd sdk\contrib\cmake
	* <on Win, choose VS version by editing vcpkg_extra_triplets\xNN-windows-mega.cmake>
	* cmake -DTRIPLET=x64-windows-mega -DEXTRA_ARGS="-DUSE_PDFIUM=0" -P build_from_scratch.cmake
* Visual Studio solution is generated at mybuild\sdk\build-x64-windows-mega
* That folder contains Debug and Release subfolders which contain build products
* Similar steps work for other platforms too (Linux with triplet x64-linux-mega (including WSL), Mac with triplet x64-osx-mega or arm64-osx-mega).

### Building with POSIX Autotools  (Linux/Darwin/BSD/OSX ...)

For platforms with Autotools, first set up needed libraries and then the generic way to build and install it is:

	sh autogen.sh
	./configure
	make
	sudo make install

Notice that you would need Autotools installed in your system (in Linux this normally entails having `autoconf` and `libtool` packages installed).

That compilation will include the example using our low level SDK: `megacli`.
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

Optional dependencies:
* Libraw (`libraw-dev`, `libraw-devel`)
* Sodium (`libsodium-dev`, `libsodium-devel`), configure `--with-sodium`
* MediaInfoLib (optional, see third_party/README_MediaInfo.txt)
* libudev (`libudev-dev`, `libudev-devel`)

Filesystem event monitoring: The provided filesystem layer implements the Linux `inotify` and the MacOS `fsevents` interfaces.

PDF thumbnail generation: The PDFium library is detected automatically by the configure step if installed. There is a helper script located at `contrib/build_pdfium` to install it in a Linux system (`/usr`). To download, build and install it in the system, run the following:

	cd contrib/build_pdfium
	build.sh -b
	sudo build.sh -i

Library will be installed under `/usr/lib/` and headers under `/usr/include/`. Once installed, the generated workspace folder could be removed to free up space.

To build the reference `megacli` example, you may also need to install:
* GNU Readline (`libreadline-dev`, `readline-devel`)
on Mac, you will probably need to download the source and build it yourself, and adjust the project to refer to that version.

For Android, we provide an additional implementation of the graphics subsystem using Android libraries.

For iOS, we provide an additional implementation of the graphics subsystem using Objective C frameworks.

## Usage

The low level SDK doesn't have inline documentation yet. If you want to use it,
please check our example app `examples/megacli`.

The new intermediate layer has been documented using Doxygen. The only public header that you need
to include to use is `include/megaapi.h`. You can read the documentation in that header file,
or download the same documentation in HTML format from this link:

https://mega.nz/#!7glwEQBT!Fy9cwPpCmuaVdEkW19qwBLaiMeyufB1kseqisOAxfi8

## Additional info

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
