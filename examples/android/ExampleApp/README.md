# Android Example App for AndroidStudio

AndroidStudio project for developing a MEGA app for Android

## Setup development environment

* [Android Studio](http://developer.android.com/intl/es/sdk/index.html)

* [Android SDK Tools](http://developer.android.com/intl/es/sdk/index.html#Other)

* [Android NDK](http://developer.android.com/intl/es/ndk/downloads/index.html): this is only required if you want to build the native libraries and Java bindings by yourself. **Required version of NDK**: r14b or newer.

## Build & Run the example:

You have two options, using a prebuilt native library or building it by yourself (only for Linux).

### To use a prebuilt library (the easy way), follow these steps:

* Download and extract the SDK to a folder in your computer: 
```
git clone --recursive https://github.com/meganz/sdk.git
```
* Download the prebuilt libraries (`libmega.so`) along with its corresponding Java classes from [here](https://mega.nz/#!AkMXUBiL!CXiHUPCo2EV6TllFr-k1fMHnZM5_VfgDAzTmoAoNRoo). Generated with commit: 84a546f81e04ca2845fbfceae5634ed57eeef957
* Extract the content into `app/src/main`, keeping the folder structure.
* Open the project with Android Studio, let it build the project and hit _*Run*_

### To build the library by yourself

Instead of downloading the prebuilt library, you can build it directly from the sources.

* Ensure that you have installed `git`, `swig`, `autotools` (`automake`, `autoconf`) and other common tools (`wget`, `unzip`, `tar`, ...).
* Download and extract the SDK to a folder in your computer: 
```
git clone --recursive https://github.com/meganz/sdk.git
```
* Configure the variable `NDK_ROOT` to point to your Android NDK installation path at `examples/android/ExampleApp/app/src/main/jni/build.sh`. Remember NDK r14b or newer is required.
* Open a terminal in the path `examples/android/ExampleApp/app/src/main/jni/` and run `./build.sh all` to build the native library.
* Open the project with Android Studio, let it build the project and hit _*Run*_

### Notes

To compile the MEGA SDK (required for this example), the building scripts consider that the Android example is located inside the SDK folder: `<sdk>/examples/android/ExampleApp`. In case you want to copy the example to a different path of your choice, you need to place a copy of the SDK in the folder `<your_path>/ExampleApp/app/src/main/jni/mega` (or you can clone the repository, so you can keep the SDK up to date).
