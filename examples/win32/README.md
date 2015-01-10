# Windows Example Apps

Visual Studio 2010 solutions with the MEGA C++ SDK and a console apps using the intermediate layer (`include/megaapi.h`).

There are two example apps:

- testmega

This example logs in to your MEGA account, gets your MEGA filesystem, shows the files/folders in your root folder and uploads an image.


- MEGAdokan

This example implements a filesystem driver that can mount a MEGA folders as a system drive. 
File writes aren't supported and the implementation doesn't make any local caching so the performance could
be poor in many cases. This isn't an application for final users, please use it for testing and developing purposes only.

## How to run the projects

1. Download the SDK or clone the SDK repository
2. Download the prebuilt third party dependencies from this link:
https://mega.co.nz/#!VsNmABiQ!RKobitbqZbUt1sEdiyJ_jOG3IJlC1zJG-YP2TRxYFd8

3. Extract and put the 3rdparty folder in `examples/win32/`.
4. Open the `MEGA.sln` file with Visual Studio 2010 or a posterior version of Visual Studio for Windows Desktop.
5. Make sure that the console project is selected as init or main project
6. Put the email/password of your MEGA account at the top of the source file
7. Build and run the application

## Notes

Third party dependencies have been built with Visual Studio 2010 SP1, so you will need the Visual C++ 2010 SP1 Redistributable Package to run applications linked to these libraries. It is installed along with Visual Studio, but you can also download it here: <br />
http://download.microsoft.com/download/C/6/D/C6D0FD4E-9E53-4897-9B91-836EBA2AACD3/vcredist_x86.exe

All these dependencies have been build using the original source code without any modification. You can build them by yourself by downloading them from their homepages:

- Crypto++ (used to encryption/decryption):
http://www.cryptopp.com/

- FreeImage (used to manage thumbnails/previews):
http://freeimage.sourceforge.net/

- Zlib (used to compress/uncompress network communications):
http://www.zlib.net/

- SQLite (used to manage the local cache of the MEGA filesystem):
http://www.sqlite.org/

- libsodium (almost not used for now, will be used by the upcoming chat/messaging features)
http://libsodium.org

- PCRE (not used for now, will be used by the upcoming advanced exclusion lists for the synchronization engine)
http://www.pcre.org/
