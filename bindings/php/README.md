Build/Install PHP Bindings
=============================

These things need to be done to build and install PHP bindings usable in a
normal fashion:

* The Mega SDK needs to be built with the PHP bindings.

The instructions given here are strictly only valid for Linux. The may need
adaptation for other platforms.

**Note:** we will refer to the root directory where SDK is downloaded as <SDK>

## Prerequisites  
```
    Install `PHP` in your system  
    Install `SWIG` in your system (it's required to generate PHP bindings)  
    Install the required PHP dependendy `Symfony Console`, You can use `Composer` for that  
```

## How to build and run the project:  
```
    ./autogen.sh
    ./configure --disable-silent-rules --enable-php --disable-examples
    make
    php <SDK>/examples/php/megacli.php
```  

**Note:** if your PHP version is equal or less than `5.3.0`, you will need to make some adjustements in configuration file `php.ini`

- To check your PHP version, you can run this command: `php -v`
- To locate file `php.ini`, you can run this command in linux: `php -i | grep "Loaded Configuration File"`

Uncomment and set this line like this:  
```
    enable_dl = On
```

Add this line at the end of the file  
```
    extension="<SDK>/bindings/php/.libs/libmegaphp.so"
```