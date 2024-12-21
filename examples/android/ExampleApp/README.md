# Android Example App for AndroidStudio

AndroidStudio project for developing a MEGA app for Android

## Setup development environment

* [Android Studio and SDK tools](https://developer.android.com/studio)

* [Android NDK](https://developer.android.com/ndk/downloads): this is only required if you want to build the native libraries and Java bindings by yourself. **Required version of NDK**: 27.1.12297006 or newer.

## Build the third party libraries and the MEGA SDK

* Ensure that you have installed `git`, `swig`, `autotools` (`automake`, `autoconf`), `libtool` and other common tools (`wget`, `curl`, `unzip`, `tar`, ...).
* Clone the MEGA SDK repository to a folder in your computer:
```
git clone https://github.com/meganz/sdk.git
```
### Configure the environment:

To build the third party libraries and the MEGA SDK to create the `libmega.so` library, you'll have to set up some environment variables or set symbolic links as follows:

* **NDK_ROOT**: Export `NDK_ROOT` environment variable or create a symbolic link at `${HOME}/android-ndk` pointing to your Android NDK installation path.
* **ANDROID_HOME**: Export `ANDROID_HOME` environment variable or create a symbolic link at `${HOME}/android-sdk` pointing to your Android SDK installation path.
* **JAVA_HOME**: Export `JAVA_HOME` environment variable or create a symbolic link at `${HOME}/android-java` pointing to your Java installation path.

Example:
```
export NDK_ROOT=/path/to/ndk
export ANDROID_HOME=/path/to/sdk
export JAVA_HOME=/path/to/java
```
or
```
ln -s /path/to/ndk ${HOME}/android-ndk
ln -s /path/to/ndk ${HOME}/android-sdk
ln -s /path/to/ndk ${HOME}/android-java
```

### Run the build script
Open a terminal in the path `examples/android/ExampleApp/app/src/main/jni/` and run `./build.sh all` to build the required libraries and the SDK. You can also run `./build.sh clean` to clean previous executions.

**IMPORTANT:** Wait for the `"Task finished OK"` message at the end to ensure everything went well. If you want a more verbose output to inspect the output looking for build errors, change the `LOG_FILE` variable in the `build.sh` script from `/dev/null` to `/dev/stdout` or to a file like `/tmp/build.log`

## Build the Android Example Application
Open the project with Android Studio, let it build the project and hit _*Run*_

### Notes

To compile the MEGA SDK (required for this example), the building scripts consider that the Android example is located inside the SDK folder: `<sdk>/examples/android/ExampleApp`. In case you want to copy the example to a different path of your choice, you need to place a copy of the SDK in the folder `<your_path>/ExampleApp/app/src/main/jni/mega` (or you can clone the repository, so you can keep the SDK up to date).
