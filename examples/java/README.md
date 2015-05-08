# Java Example App

Eclipse project for developing a MEGA app in Java.

This example shows a simple GUI (Swing) to allow the user log into MEGA. If login is successfull, it retrieves the MEGA filesystem and lists files/folders in the root folder.

## How to run the project in LINUX:

1. Download, build and install the SDK

    ```
    git clone https://github.com/meganz/sdk
    cd sdk
    sh autogen.sh
    ./configure --enable-java
    make
    sudo make install
    ```

2. Copy the library `libmegajava.so` into the `libs` folder in your project.
    
    ```
    cp bindings/java/.libs/libmegajava.so examples/java/JavaBindingSample/libs
    ```
    
3. Copy the Java classes into the `src` folder in your project.

    ```
    cp bindings/java/nz/mega/sdk/*.java examples/java/JavaBindingSample/src/nz/mega/sdk
    ```
    
4. In Eclipse, click "Import" -> "Existing Projects into Workspace" and select the root directory: `sdk/examples/java`
5. Build and run the project in Eclipse

## How to run the project in WINDOWS

You have two options, using a prebuilt native library or building it by yourself.

### To use a prebuilt library (the easy way), follow these steps:

1. Download and extract the SDK to a folder in your computer ([link](https://github.com/meganz/sdk/archive/master.zip)).
2. Download the prebuilt library (`mega.dll`) along with its corresponding Java classes from [here](INCLUDE THE MEGA LINK TO dll + bindings/java/nz/mega/sdk/*.java).
3. Extract the content into `sdk/examples/java/JavaBindingSample`, keeping the folder structure. 
4. In Eclipse, click "Import" -> "Existing Projects into Workspace" and select the root directory: `sdk/examples/java`.
5. Build and run the project in Eclipse.

### To build the library by yourself

Instead of downloading the prebuilt library, you can build it directly from the sources.

1. Download and extract the SDK to a folder in your computer ([link](https://github.com/meganz/sdk/archive/master.zip)).
2. Follow the instructions in [this guide](https://github.com/meganz/sdk/bindings/java/vs2010/README.md).
3. Copy the new file `mega.dll` from `sdk/bindings/java/vs2010/Debug` into `sdk/examples/java/JavaBindingSample/libs`.
4. Copy the Java classes from `sdk/bindings/java/nz/mega/sdk` into `sdk/bindings/java/JavaBindingSample/src/nz/mega/sdk`.
5. In Eclipse, click "Import" -> "Existing Projects into Workspace" and select the root directory: `sdk/examples/java`.
6. Build and run the project in Eclipse.

## Notes

The file `nz/mega/sdk/MegaApiJava.java` contains Javadoc documentation. It is recommended to use the `MegaApiSwing` subclass instead because it makes some needed initialization and sends callbacks to the UI thread, but the documentation in `MegaApiJava` applies also for `MegaApiSwing`.
