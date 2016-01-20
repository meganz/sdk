.. _installsdk:

================================
Installing the Mega SDK Java API
================================

--------------------------
1. Prepare System
--------------------------

.. code:: bash

    sudo apt-get install build-essential autoconf libtool git-core

-------------------------
2. Clone MEGA SDK
-------------------------

.. code:: bash
    
    git clone https://github.com/meganz/sdk

-------------------------
3. Install Dependencies
-------------------------

.. code:: bash

    sudo apt-get install libcrypto++-dev zlib1g-dev libsqlite3-dev libssl-dev libc-ares-dev libcurl4-openssl-dev libfreeimage-dev libreadline6-dev swig2.0 default-jdk
   
----------------------
4. Configure SDK 
----------------------

.. code:: bash

    cd sdk/
   
.. code:: bash

    sh autogen.sh

.. code:: bash
    
    ./configure --enable-java --with-java-include-dir=/usr/lib/jvm/java-7-openjdk-i386/include/

-------------------------------
5. Compile & Install SDK
-------------------------------

.. code:: bash
    
    make

.. code:: bash

    sudo make install
    
-------------------------------------------------
6. Copy SDK Library & Bindings into your Project
-------------------------------------------------
    
To use the MEGA SDK bindings, copy the MEGA SDK Java library ``libmegajava.so`` into a  ``/libs/`` folder in the root of your project folder and copy the MEGA SDK Java binding classes into the source folder of your project:

.. code:: bash

    mkdir -p projectRootFolder/libs/

.. code:: bash
    
    cp bindings/java/.libs/libmegajava.so projectRootFolder/libs/

.. code:: bash
    
    mkdir -p projectRootFolder/src/nz/mega/sdk/
    
.. code:: bash

    cp bindings/java/nz/mega/sdk/*.java projectRootFolder/src/nz/mega/sdk/

-------------------------------------------------
7. Done
-------------------------------------------------

Congratulations you are now ready to use the MEGA SDK Java API bindings in your own applications!

.. NOTE::
    This guide was tested on Ubuntu 15.04 and is adapted from:      https://github.com/meganz/sdk/blob/master/README.md and     https://help.ubuntu.com/community/CompilingEasyHowTo 
