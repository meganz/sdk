================================
Installing the Mega SDK Java API
================================

-------------------------
First Create Clone of SDK
-------------------------

git clone https://github.com/meganz/sdk cd sdk

-------------------------
Next Install Dependancies
-------------------------
* sudo apt-get install autoconf   
* sudo apt-get install libtool   
* sh autogen.sh   
* sudo apt-get install libcrypto++-dev     
* sudo apt-get install zlib1g-dev    
* sudo apt-get install libsqlite3-dev    
* sudo apt-get install libssl-dev    
* sudo apt-get install libc-ares-dev    
* sudo apt-get install libcurl4-openssl-dev    
* sudo apt-get install libfreeimage-dev    
* sudo apt-get install libtinfo-dev   
* sudo apt-get install swig2.0    
* sudo apt-get install default-jdk   
   
----------------------
Next Configure the SDK 
----------------------
   
* Configure while pointing to Java headers ./configure --enable-java --with-java-include-dir=/usr/lib/jvm/"Your Java JDK Folder"/include/

---------------------------
Compile and Install the SDK
---------------------------
* make

* sudo make install

* Congratulations you are now ready to use the Mega Java API in your own applications!
