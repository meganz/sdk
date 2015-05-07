# Java SDK bindings for Windows

Visual Studio solution for compiling the SDK library.

This solution, created with Visual Studio 2010 SP1 C++ Express edition, allows to compile `mega.dll` for Windows 32 bits platform. Such library is required to run Java applications using the MEGA SDK.

## How to run the project:

1. Download the 3rd-party libraries from this [link](https://mega.nz/#!OtEgkDLS!xxHrPgAISI6NZzsH6Q_U4l9i0dVRAYUQEa1ZoouatsY).

2. Extract the content to `sdk/bindings/java/vs2010/`.

2. Open the solution `MEGAdll.sln` and hit "Build". 

3. If success, you should find a `mega.dll` library in `sdk/bindings/java/vs2010/Debug/` ready to use.

## Notes

The provided VS solution uses the files `megaapi_wrap.h` and `megaapi_wrap.c` located in the folder `sdk/bindings/java`.
Should you need an updated version of those files, you need to build the SDK under Linux by following the instructions [here](https://github.com/sergiohs84/sdk#building).
