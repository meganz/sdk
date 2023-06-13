# Android Example App for AndroidStudio

AndroidStudio project for developing a MEGA app for Android

## Setup development environment

* [Android Studio and SDK tools](https://developer.android.com/studio)

* [Android NDK](https://developer.android.com/ndk/downloads): this is only required if you want to build the native libraries and Java bindings by yourself. **Required version of NDK**: r21d or newer.

## Build & Run the example:

You have two options, using a prebuilt native library or building it by yourself (only for Linux).

### To use a prebuilt library (the easy way), follow these steps:

* Clone the SDK repository to a folder in your computer:
```
git clone https://github.com/meganz/sdk.git
```
* Download the prebuilt libraries (`libmega.so`) along with its corresponding Java classes from [here](https://mega.nz/file/9txTTTCb#h6xAYt4ltggKTvDuKs5EFU-szq7N5DZx16Pa1X0lN1g). Generated with commit: f81e7c5a7ad3e0de2f9bbdc0e84e5701d2f668db
* Extract the content into `app/src/main`, keeping the folder structure.
* Open the project with Android Studio, let it build the project and hit _*Run*_

### To build the library by yourself

Instead of downloading the prebuilt library, you can build it directly from the sources.

* Ensure that you have installed `git`, `swig`, `autotools` (`automake`, `autoconf`) and other common tools (`wget`, `unzip`, `tar`, ...).
* Clone the SDK repository to a folder in your computer:
```
git clone https://github.com/meganz/sdk.git
```
* Export the environment variable `NDK_ROOT` pointing to your Android NDK installation path or create a symbolic link called `android-ndk` in your `$HOME` folder pointing to it.
* Open a terminal in the path `examples/android/ExampleApp/app/src/main/jni/` and run `./build.sh all` to build the native libraries. Wait for the `"Task finished OK"` message at the end to ensure everything went well. If you want a more verbose output, change the `LOG_FILE` variable in the `build.sh` script from `/dev/null` to `/dev/stdout` or to a file like `/tmp/build.log`
* Open the project with Android Studio, let it build the project and hit _*Run*_

### Notes

To compile the MEGA SDK (required for this example), the building scripts consider that the Android example is located inside the SDK folder: `<sdk>/examples/android/ExampleApp`. In case you want to copy the example to a different path of your choice, you need to place a copy of the SDK in the folder `<your_path>/ExampleApp/app/src/main/jni/mega` (or you can clone the repository, so you can keep the SDK up to date).
