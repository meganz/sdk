# Windows Example Apps

Visual Studio 2015 solutions with the MEGA C++ SDK and console apps using the intermediate layer (`include/megaapi.h`).

There are two example apps:

- testmega

This example logs in to your MEGA account, gets your MEGA filesystem, shows the files/folders in your root folder,
uploads an image and optionally starts a local HTTP proxy server to browse you MEGA account.

- MEGAdokan

This example implements a filesystem driver that can mount a MEGA folder as a system drive.
File writes aren't supported and the implementation doesn't make any local caching so the performance could
be poor in many cases. This isn't an application for final users, please use it for testing and developing purposes only.

Additionally to the instructions below, it's needed to install Dokan to run this example. You can download a compabible version here:

For Windows 10, Windows 8.1, Windows 8 and Windows 7
https://github.com/dokan-dev/dokany/releases/tag/v0.7.4

For Windows Vista and Windows XP
https://github.com/dokan-dev/dokany/releases/tag/0.6.0

Do not use a more recent version of Dokan because it wouldn't be compatible.

## How to run the projects

1. Download the SDK or clone the SDK repository
2. Download the prebuilt third party dependencies from this link:
https://mega.nz/#!gpt1yYhL!MbAyELMgxinOkZdLyM5zIdN6rq6OqrhhYfyVQovHYfI

3. Extract and put the 3rdparty folder in `examples/win32/`.
4. Open the `MEGA.sln` file with Visual Studio 2015
5. Make sure that the console project is selected as init or main project
6. Put the email/password of your MEGA account at the top of the source file
7. Build and run the application

## Notes

Third party dependencies have been built with Visual Studio 2015, so you will need the Visual C++ 2015 Redistributable Package to run applications linked to these libraries. It is installed along with Visual Studio, but you can also download it here: <br />
https://download.microsoft.com/download/6/D/F/6DF3FF94-F7F9-4F0B-838C-A328D1A7D0EE/vc_redist.x86.exe

All these dependencies have been build using the original source code without any modification. You can build them by yourself by downloading them from their homepages:

- Crypto++ (used for encryption/decryption):
http://www.cryptopp.com/

- FreeImage (used to manage thumbnails/previews):
http://freeimage.sourceforge.net/

- Zlib (used to compress/uncompress network communications):
http://www.zlib.net/

- SQLite (used to manage the local cache of the MEGA filesystem):
http://www.sqlite.org/

- libsodium (used for encryption/decryption):
http://libsodium.org

- libcURL (used for HTTP / HTTPS communications)
https://curl.haxx.se/libcurl/

- libuv (used for the local HTTP proxy server)
http://libuv.org/
