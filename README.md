# MEGA SDK - Client Access Engine

MEGA --- _The Privacy Company_ --- is a Secure Cloud Storage provider
that protects your data thanks to end-to-end encryption. We call it User Controlled Encryption,
or UCE, and all our clients automatically manage it.

All files stored on MEGA are encrypted. All data transfers from and to MEGA are encrypted. And while
most cloud storage providers can and do claim the same, MEGA is different – unlike the industry norm
where the cloud storage provider holds the decryption key, with MEGA, you control the encryption,
you hold the keys, and you decide who you grant or deny access to your files.

This SDK brings you all the power of our client applications and lets you create
your own or analyze the security of our products. Are you ready to start? Please continue reading.

## SDK Contents

In this SDK, you can find our low level SDK, that was already released few months after the MEGA launch,
a new intermediate layer to make it easier to use and to bind with other programming languages, and
example apps for all our currently supported platforms (Android, GNU/Linux, iOS, macOS and Windows).

In the [examples](examples) folder you can find example apps using:

1. The low level SDK:
  - megacli (a powerful command line tool that allows to use all SDK features)

2. The public API:
  - A plain C++ example app in `examples/simple_client`
  - An example app for Android (using Java bindings based on SWIG) in `examples/android`
  - An example app for iOS (using Objective-C bindings) in `examples/iOS`

[MEGAcmd](https://github.com/meganz/megacmd), a higher level command line application that uses the SDK to provide interactive and scriptable access to MEGA. You can use it by running megacmd-server and talk to it from PHP/Python code, for instance.

## How to build the SDK library

For the SDK development and compilation we use CMake as the cross-platform project configuration tool. We also use VCPKG to manage the required dependencies to build the SDK in most platforms: GNU/Linux, macOS and Windows.

### Building tools

Some common development tools should be available in the system to be able to build the MEGA SDK and the needed dependencies:

- Git: Use the one from your system package manager or install it from https://git-scm.com
- CMake 3.19 or higher: Use the one from your system package manager or install it from https://cmake.org

#### Windows

Ensure you have installed Visual Studio, with the necessary components for building C++ sources, and the Windows SDK on your system:

 - [Visual Studio 2022](https://visualstudio.microsoft.com/vs/)
 - MSVC v142
 - Windows 10 SDK (10.0.19041.0)

#### MacOS

Xcode and the Developer tools are needed. To install the
Developer tools, run the following command and follow the instructions:

	$ xcode-select --install

The following packages should be available in the system as well:

 - autoconf, autoconf-archive, automake, pkg-config, nasm and libtool.

You can use any package manager if you have one installed or build and install them from sources

#### Linux

For debian-based distributions, you can install the needed compilers and tools using the following command:

	sudo apt install build-essential curl zip unzip autoconf autoconf-archive nasm libtool-bin

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

**Note**: VCPKG local repository needs to be updated from time to time. If never done, it will eventually fail to find new dependencies or others updated to versions newer than what it already had.
The solution is simple: go to VCPKG local repository and run `git pull`.

### Configuration

The following instructions are for configuring the project from the CLI, but cmake-gui or any editor or IDE
compatible with CMake should be suitable if the same CMake parameters are configured.

The SDK is configured like any other regular CMake project. The only parameter that is always needed is the VCPKG directory
to manage the third-party dependencies. To configure the SDK with the default options, from the workspace (`mega` directory), run CMake:

	cmake -DVCPKG_ROOT=vcpkg -DCMAKE_BUILD_TYPE=Debug -S sdk -B build_dir

**Note**: The `-DCMAKE_BUILD_TYPE=<Debug|Release>` may not be needed for multiconfig generators, like Visual Studio.

In the command above, relative paths have been used for simplicity. If you want to change the location of VCPKG, the SDK or the build directory, simply provide a valid relative or absolute path for any of them.

During the configuration of the project, VCPKG will build and configure the necessary libraries for the platform. It may take a while on the first run, but once the libraries are built, VCPKG will retrieve them from the binary cache.

Some options to configure the SDK library can be found in the [sdklib_options.cmake](cmake/modules/sdklib_options.cmake) file, like ENABLE_SYNC or USE_PDFIUM.
The options to manage the examples and tests are in the [CMakeLists.txt](CMakeLists.txt).

### Building the sources

Once the MEGA SDK is configured, simply build the complete project:

	cmake --build build_dir

You can specify `--target=<target>` like `SDKlib` or `megacli`, or just leave the command as it is to build all the tagets.
Additionally, `-j<N>` can be added to manage concurrency and speed up the build.

Once the build is finished, binaries will be available in the `build_dir`

### Run megacli

To run the example app `megacli`, go to the `examples/megacli` directory in the `build_dir` and execute the `megacli` binary.

## Minimum supported OS versions

### Android

- Android 8.0

### DMS

- DMS 7.2

### GNU/Linux

- Arch
- Debian 11
- Fedora 38
- OpenSUSE Leap 15.5
- Raspberry Pi OS Lite (Debian 11)
- Ubuntu 20.04 LTS

### iOS

- iOS 15

### macOS

- macOS 10.15 (Intel)
- macOS 11.1 (Apple silicon)

### Windows

- Windows 8
- Windows Server 2016

## Usage

The low level SDK doesn't have inline documentation yet. If you want to use it,
please check our example app `examples/megacli`.

The intermediate layer has been documented using Doxygen. The only public header that you need
to include to use is `include/megaapi.h`. You can read the documentation in that header file.

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
