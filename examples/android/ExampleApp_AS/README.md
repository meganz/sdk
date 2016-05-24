# Android Example App for AndroidStudio

AndroidStudio project for developing a MEGA app for Android

## Setup development environment

* [Android Studio](http://developer.android.com/intl/es/sdk/index.html)

* [Android SDK Tools](http://developer.android.com/intl/es/sdk/index.html#Other)

* [Android NDK](http://developer.android.com/intl/es/ndk/downloads/index.html): this is only required if you want to build the native libraries and Java bindings by yourself.

## Build & Run the example:

You have two options, using a prebuilt native library or building it by yourself (only for Linux).

### To use a prebuilt library (the easy way), follow these steps:

* Download and extract the SDK to a folder in your computer: 
```
git clone --recursive https://github.com/meganz/sdk.git
```
* Download the prebuilt libraries (`libmega.so`) along with its corresponding Java classes from [here](https://mega.nz/#!OksR3JhY!DYe4OqBzh9owM410vlyT_JTiZ-kfcfk7HKJCs88VwSQ).
* Extract the content into `app/src/main`, keeping the folder structure.
* Open the file `app/build.gradle` and comment the line `compileTask -> compileTask.dependsOn megaSDK` to skip the building of the SDK library and its bindings.
* Open the project with Android Studio, let it build the project and hit _*Run*_

### To build the library by yourself

Instead of downloading the prebuilt library, you can build it directly from the sources.

* Download and extract the SDK to a folder in your computer: 
```
git clone --recursive https://github.com/meganz/sdk.git
```
* Configure the variable `NDK_ROOT` to point to your Android NDK installation path at `app/src/main/jni/Makefile`.
* Open the project with Android Studio, let it build the project and hit _*Run*_

### Notes

To compile the MEGA SDK (required for this example), the building scripts consider that the Android example is located inside the SDK folder: `<sdk>/examples/android/ExampleApp_AS`. In case you want to copy the example to a different path of your choice, you need to place a copy of the SDK in the folder `<your_path>/ExampleApp_AS/app/src/main/jni/mega` (or you can clone the repository, so you can keep the SDK up to date).
