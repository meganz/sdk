# Android Example App for Eclipse + ADT

Eclipse project for developing a MEGA app for Android

## How to run the project:

You have two options, using a prebuilt native library or building it by yourself.
To use a prebuilt library (the easy way), follow these steps:

1. Download the SDK to a folder out of your Eclipse workspace
2. On Eclipse, click "New Project" -> "Android Project from Existing code"
3. Select the folder `examples/android/ExampleApp_ADT`
4. Check the "Copy project into workspace" option
5. Download the prebuilt native libraries (`libmega.so`) along with its corresponding Java classes from [here](https://mega.nz/#!FwF2TLTA!Uv3bvU3I0N1f-TD31K8kqxgPSsvdEFT7HB_hJL5uy3g). Commit: 0755b29c4c33ae6bf07f546c8497da57e201d48a
6. Extract the content into the root folder of the project, keeping the folder structure.
You should end with `[Eclipse workspace]/[MEGA project folder]/libs/armeabi/libmega.so`, `[Eclipse workspace]/[MEGA project folder]/libs/x86/libmega.so` and all the auto-generated Java bindings under `[Eclipse workspace]/[MEGA project folder]/src/nz/mega/sdk/`.
7. Copy the static Java bindings from the SDK folder (`[sdk folder]/bindings/java/nz/mega/sdk`) into the project source path (`[Eclipse workspace]/[MEGA project folder]/src/nz/mega/sdk`). Remove the unnecessary `MegaApiSwing.java` from the added sources.
8. Build and run the project in Eclipse

If you want to build the native library by yourself, the process is a 
bit different:

* Do not check the "Copy project into workspace" option to preserve the structure of the 
SDK repository. Otherwise, you will have to put a copy of the SDK in 
`examples/Android/ExampleApp/jni/mega/sdk` or enter the path of the MEGA C++ SDK after importing the project.

* Open the file `examples/android/ExampleApp_ADT/jni/Makefile` and enter 
the path of your Android NDK and, if you changed the structure of the repo, the root of the MEGA C++ SDK (or put a copy in 
`jni/mega/sdk`)

* Open a terminal on `examples/Android/ExampleApp_ADT/jni` and type `make`
You will need some packages (`wget, swig, sha1sum, unzip`)

* Copy the static Java bindings from the SDK folder (`[sdk folder]/bindings/java/nz/mega/sdk`) into the project source path (`[MEGA project folder]/src/nz/mega/sdk`). Remove the unnecessary `MegaApiSwing.java` from the added sources.

* After a successful build, run the app in Eclipse

The file `nz/mega/sdk/MegaApiJava.java` contains Javadoc documentation. It is recommended to use the `MegaApiAndroid` subclass instead because it makes some needed initialization and sends callbacks to the UI thread, but the documentation in `MegaApiJava` applies also for `MegaApiAndroid`.
