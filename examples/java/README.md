# Java Example App

This example shows a simple GUI (Swing) to allow the user log into MEGA. If login is successfull, it retrieves the MEGA filesystem and lists files/folders in the root folder.

Below, you will find instructions to generate the required sources and library (prerequisites) for developing a MEGA app in Java. Afterwards, the Example application can be compiled and executed.

## Prerequisites in Linux:

1. Download, build and install the SDK

    ```
    git clone https://github.com/meganz/sdk
    cd sdk
    sh autogen.sh
    ./configure --enable-java
    make
    ```

    It might need to be necessary to pass the Java include directory for
    `jni.h` to the `configure` command, e. g.

    `--with-java-include-dir=/usr/lib/jvm/java-8-openjdk-amd64/include`

2. Copy the library `libmegajava.so` into the `libs` folder in your project.
    
    ```
    mkdir examples/java/libs
    cp bindings/java/.libs/libmegajava.so examples/java/libs/
    ```
    
3. Copy the Java classes into the `src` folder in your project.

    ```
    cp -a bindings/java/nz/mega/sdk examples/java/src/nz/mega/
    ```

## Prerequisites in Windows:

You have two options, using a prebuilt native library or building it by yourself.

### To use a prebuilt library (the easy way), follow these steps:

1. Download and extract the SDK to a folder in your computer ([link](https://github.com/meganz/sdk/archive/master.zip)).
2. Download the prebuilt library (`mega.dll`) along with its corresponding Java classes from [here](https://mega.nz/#!vsMCWJbJ!WmvaOaat1ccHbi1dQyhOk9_zj4xVO09R4NnIYPUrzlE).
3. Extract the content into `sdk/examples/java/`, keeping the folder structure.

### To build the library by yourself

Instead of downloading the prebuilt library, you can build it directly from the sources.

1. Download and extract the SDK to a folder in your computer ([link](https://github.com/meganz/sdk/archive/master.zip)).
2. Follow the instructions in [this guide](https://github.com/meganz/sdk/bindings/java/contrib/vs2010/README.md).
3. Copy the new file `mega.dll` from `sdk/bindings/java/contrib/vs2010/Debug` into `sdk/examples/java/libs`.
4. Copy the Java classes from `sdk/bindings/java/nz/mega/sdk` into `sdk/bindings/java/src/nz/mega/sdk`.

## How to run the application

At this point, you should have all the required source code and the MEGA library in `src` and `libs` folders respectively.

You can simply compile the application and execute it:

	```
        mkdir bin
	javac -d bin -sourcepath src/ src/nz/mega/bindingsample/MainWindow.java
	java -cp bin nz.mega.bindingsample.MainWindow
	```

Or you might prefer to use Eclipse:

1. Create a new Java project.
2. Copy `src` and `libs` folders into the folder of your project.
3. Build and run the project in Eclipse.

## Notes

The file `nz/mega/sdk/MegaApiJava.java` contains JavaDoc documentation. It is recommended to use the `MegaApiSwing` subclass instead because it makes some needed initialization and sends callbacks to the UI thread, but the documentation in `MegaApiJava` applies also for `MegaApiSwing`.

