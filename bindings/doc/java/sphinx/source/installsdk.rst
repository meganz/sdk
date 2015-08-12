================================
Installing the Mega SDK Java API
================================

////// Work in progress /////

git clone https://github.com/meganz/sdk cd sdk

sudo apt-get install autoconf

sudo apt-get install libtool

sh autogen.sh

// Install dependencies sudo apt-get install libcrypto++-dev sudo apt-get install zlib1g-dev sudo apt-get install libsqlite3-dev sudo apt-get install libssl-dev sudo apt-get install libc-ares-dev sudo apt-get install libcurl4-openssl-dev sudo apt-get install libfreeimage-dev sudo apt-get install libtinfo-dev // sudo apt-get install swig sudo apt-get install swig2.0 sudo apt-get install default-jdk

// Configure while pointing to Java headers http://tecadmin.net/install-oracle-java-8-jdk-8-ubuntu-via-ppa/ ./configure --enable-java --with-java-include-dir=/usr/lib/jvm/java-8-oracle/include/

(./configure --enable-java)

make

sudo make install

// Copy the library libmegajava.so into the libs folder in your project.

create /libs in examples mkdir sdk/examples/java/libs

cp bindings/java/.libs/libmegajava.so examples/java/libs

// Copy the Java classes into the src folder in your project. mkdir examples/java/src/nz/mega/sdk cp bindings/java/nz/mega/sdk/*.java examples/java/src/nz/mega/sdk

// Create a credentials.txt in the root of the project with the user's mega // login details (email and password) each // on their own line.

// Compile with java compiler mkdir sdk/examples/java/bin

javac -d bin -sourcepath src/ src/nz/mega/bindingsample/MainWindow.java // Get 2 errors src/nz/mega/sdk/MegaApiJava.java:3340: error: cannot find symbol return megaApi.nameToLocal(name); ^ symbol: method nameToLocal(String) location: variable megaApi of type MegaApi src/nz/mega/sdk/MegaApiJava.java:3351: error: cannot find symbol return megaApi.localToName(localName); ^ symbol: method localToName(String) location: variable megaApi of type MegaApi // Comment out offending methods to continue

java -cp bin nz.mega.bindingsample.MainWindow
