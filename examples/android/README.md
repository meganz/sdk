# Android Example App

Eclipse project for developing a MEGA app for Android

## How to run the project:

You have two options, using a prebuilt native library or building it by yourself.
To use a prebuilt library (the easy way), follow these steps:

1. Download the SDK to a folder out of your Eclipse workspace
2. On Eclipse, click "New Project" -> "Android Project from Existing code"
3. Select the folder `examples/Android/ExampleApp`
4. Check the "Copy project into workspace" option
5. Download the prebuilt native libraries from this link:
https://mega.co.nz/#!cgkTVBDK!XmnRL-CzW6SLrnXR26FPHKaeBva_kJ6v9UIhFHO9TN8

6. Uncompress the libraries in a `libs` folder in your project.
You should end with `[Eclipse workspace]/[MEGA project folder]/libs/armeabi/libmega.so` and `[Eclipse workspace]/[MEGA project folder]/libs/x86/libmega.so`

7. Build and run the project in Eclipse

If you want to build the native library by yourself, the process is a 
bit different:

* Do not check the "Copy project into workspace" option to preserve the structure of the 
SDK repository. Otherwise, you will have to put a copy of the SDK in 
`examples/Android/ExampleApp/jni/mega/sdk` or enter the path of the MEGA C++ SDK after importing the project.

* Open the file `examples/Android/ExampleApp/jni/Makefile` and enter 
the path of your Android NDK and, if you changed the structure of the repo, the root of the MEGA C++ SDK (or put a copy in 
`jni/mega/sdk`)

* Open a terminal on `examples/Android/ExampleApp/jni` and type `make`
You will need some packages (`wget, swig, sha1sum, unzip`)

* After a successful build, run the app in Eclipse
