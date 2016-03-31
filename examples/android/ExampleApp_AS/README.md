# Android Example App

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
* Download the prebuilt libraries (`libmega.so`) along with its corresponding Java classes from [here](https://mega.nz/#!zktVFAyS!ZzvOYnU-I6cmKgc3_rA-UIJh98suyLAM6BPF7_57BoM).
* Extract the content into `app/src/main`, keeping the folder structure.
* Open the project with Android Studio, let it build the project and hit _*Run*_

### To build the library by yourself

Instead of downloading the prebuilt library, you can build it directly from the sources.

* Download and extract the SDK to a folder in your computer: 
```
git clone --recursive https://github.com/meganz/sdk.git
```
* Configure the variable `NDK_ROOT` to point to your Android NDK installation path at `app/src/main/jni/Makefile`.
* Run the previous `Makefile` in order to build the MEGA SDK, its dependencies and the required bindings for Java.
* Open the project with Android Studio, let it build the project and hit _*Run*_
