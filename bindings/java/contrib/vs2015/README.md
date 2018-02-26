# Java SDK bindings for Windows

Visual Studio solution for compiling the SDK library.

This solution, created with Visual Studio 2015 Express edition, allows to compile `mega.dll` for Windows

Such library is required to run Java applications using the MEGA SDK and must be provided with its corresponding Java clases.


## How to run the project:

1. Download the 3rd-party libraries from this [link](https://mega.nz/#!IwMkyYzK!0tW9hv6m0qZ7ysjwPOTPPktVI0lDQfXaCIa8Pj3DtBA).

2. Extract the content to `sdk/bindings/java/contrib/vs2015/`.

3. Download the Java bindings files created by SWIG from this [link](https://mega.nz/#!g4kHgZ6C!Gn9GFnEUSZwNiW-9j--Ojp7vdUjusTyAtsJuSzITxfQ).

4. Extract the content to `sdk/bindings/java/`

5. Open the solution `MEGAdll.sln` and hit "Build".

6. If success, you should find a `mega.dll` library in `sdk/bindings/java/contrib/vs2015/Release/`.


## Notes

The provided VS solution uses the files `megaapi_wrap.h` and `megaapi_wrap.cpp` located in the folder `sdk/bindings/java`. The generated DLL must be provided with the Java clases in `sdk/bindings/java/nz/mega/sdk/`.

If you need an updated version of any of those files (`megaapi_wrap.cpp`, `megaapi_wrap.h` or any of the Java classes), then you will have to generate them by following the instructions [here](https://github.com/meganz/sdk#building), adding the option `--enable-java` to your configure command. Due to build tool requirements it might be easiest to do this on a different platform (e.g. a Linux system).

``` 
./configure --enable-java
```
