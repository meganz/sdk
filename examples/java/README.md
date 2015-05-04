# Java Example App

Eclipse project for developing a MEGA app for Java.

This example shows a simple GUI (Swing) to allow the user log into MEGA. If login is successfull, it retrieves the MEGA filesystem and lists files/folders in the root folder.

## How to run the project:

You have two options, using a prebuilt native library or building it by yourself.
To use a prebuilt library (the easy way), follow these steps:

1. Download, build and install the SDK

    ```
    git clone https://github.com/meganz/sdk
    cd sdk
    sh autogen.sh
    ./configure --enable-java
    make
    sudo make install
    ```

2. On Eclipse, click "Import" -> "Existing Projects into Workspace" and select the root directory: `sdk/examples/java/JavaBindingSample`
3. Open the "Properties" of the project and go to "Java Build Path" -> "Source"
4. Click "Link Source..." and select the folder `sdk/bindings/java`
5. Copy the library `libmegajava.so` into the `libs` folder in your project.
    
    ```
    cp sdk/bindings/java/.lib/libmegajava.so sdk/examples/java/libs
    ```
    
6. Build and run the project in Eclipse

## Notes

The file `nz/mega/sdk/MegaApiJava.java` contains Javadoc documentation. It is recommended to use the `MegaApiSwing` subclass instead because it makes some needed initialization and sends callbacks to the UI thread, but the documentation in `MegaApiJava` applies also for `MegaApiSwing`.
