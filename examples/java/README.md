# Java Example App

There are two examples provided for the Java bindings:

* A simple GUI (Swing) to allow the user log into MEGA. If login is successfull, it retrieves the MEGA filesystem and lists files/folders in the root folder.
* A simple console example that will demonstrate a simple "CRUD" (create, read, update delete) workflow, using this README file to upload and download. This example uses no GUI, and will display all interactions on the console using the Java provided logging framework.

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

2. Copy or symlink the library `libmegajava.so` into the `libs` folder in your project.
    
    ```
    mkdir examples/java/libs
    cp bindings/java/.libs/libmegajava.so examples/java/libs/
    # Alternatively for linking:
    ln -s ../../../bindings/java/.libs/libmegajava.so examples/java/libs/.
    ```
    
3. Copy or symlink the Java classes into the `src` folder in your project.

    ```
    cp -a bindings/java/nz/mega/sdk examples/java/src/nz/mega/
    # Alternatively for linking:
    ln -s ../../../../../bindings/java/nz/mega/sdk examples/java/src/nz/mega/.
    ```

## Prerequisites in Windows:

You have two options, using a prebuilt native library or building it by yourself.

### To use a prebuilt library (the easy way), follow these steps:

1. Download and extract the SDK to a folder in your computer ([link](https://github.com/meganz/sdk/archive/master.zip)).
2. Download the prebuilt library (`mega.dll`) along with its corresponding Java classes from [here](https://mega.nz/#!B4cx1KoA!GWYOcP0mp5_n6ouUEv27BKSkRcPGj9x17Eos6S72NB8).
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
        # For the Java Swing GUI example:
	javac -d bin -sourcepath src/ src/nz/mega/bindingsample/SwigExample.java
	java -cp bin nz.mega.bindingsample.SwigExample

        # For the Java console example (`-D` parameter is to configure logging):
	javac -d bin -sourcepath src/ src/nz/mega/bindingsample/CrudExample.java
        java -cp bin -Djava.util.logging.config.file=logging.properties \
            nz.mega.bindingsample.CrudExample
	```

Note: The console example will not work from some IDEs (like Eclipse) as it requires the hidden password entry using the console directly.

Or you might prefer to use Eclipse:

1. Create a new Java project.
2. Copy `src` and `libs` folders into the folder of your project.
3. Build and run the project in Eclipse.

## Notes

The file `nz/mega/sdk/MegaApiJava.java` contains JavaDoc documentation. It is recommended to use the `MegaApiSwing` subclass instead because it makes some needed initialization and sends callbacks to the UI thread, but the documentation in `MegaApiJava` applies also for `MegaApiSwing`.

