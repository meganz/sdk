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
    cp bindings/java/.lib/libmegajava.so examples/java/JavaBindingSample/libs
    ```
    
3. On Eclipse, click "Import" -> "Existing Projects into Workspace" and select the root directory: `sdk/examples/java/JavaBindingSample`
4. Open the "Properties" of the project and go to "Java Build Path" -> "Source"
5. Click "Link Source..." and select the folder `sdk/bindings/java`
6. Build and run the project in Eclipse

## How to run the project in WINDOWS

You have two options, using a prebuilt native library or building it by yourself.

### To use a prebuilt library (the easy way), follow these steps:

1. Download and extract the SDK ([link](https://github.com/meganz/sdk/archive/master.zip)).
2. Download and copy the prebuilt library `mega.dll` into `sdk/examples/java/JavaBindingSample/libs` ([link](https://mega.nz/#!jglhVaIL!XXqiSH883mDvyLAQ8f8dHBC9ivABGOUfYWDr1uX0Y8g)).
3. On Eclipse, click "Import" -> "Existing Projects into Workspace" and select the root directory: `sdk/examples/java/JavaBindingSample`
4. Open the "Properties" of the project and go to "Java Build Path" -> "Source"
5. Click "Link Source..." and select the folder `sdk/bindings/java`
6. Build and run the project in Eclipse

### To build the library by yourself

Instead of downloading the prebuilt library, you can build it directly from the sources.

1. Download and extract the SDK ([link](https://github.com/meganz/sdk/archive/master.zip)).
2. Follow the instructions in [this guide](https://github.com/meganz/sdk/bindings/java/vs2010/README.md).
3. Copy the new file `mega.dll` into `sdk/examples/java/JavaBindingSample/libs`.
4. On Eclipse, click "Import" -> "Existing Projects into Workspace" and select the root directory: `sdk/examples/java/JavaBindingSample`
5. Open the "Properties" of the project and go to "Java Build Path" -> "Source"
6. Click "Link Source..." and select the folder `sdk/bindings/java`
7. Build and run the project in Eclipse


## Notes

The file `nz/mega/sdk/MegaApiJava.java` contains Javadoc documentation. It is recommended to use the `MegaApiSwing` subclass instead because it makes some needed initialization and sends callbacks to the UI thread, but the documentation in `MegaApiJava` applies also for `MegaApiSwing`.
