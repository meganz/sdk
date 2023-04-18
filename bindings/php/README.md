Build/Install PHP Bindings
=============================

These things need to be done to build and install PHP bindings usable in a
normal fashion:

* The Mega SDK needs to be built with the PHP bindings.

The instructions given here are strictly only valid for Linux. The may need
adaptation for other platforms.

**Note:** we will refer to the root directory where SDK is downloaded as `<SDK>`

## Prerequisites  
```
    Install `PHP` in your system (`sudo apt install php php-dev` in Ubuntu).
    Install `SWIG` in your system (`sudo apt install swig` in Ubuntu. Required to generate PHP bindings).
    Install `composer` in your system (`sudo apt install composer` in Ubuntu. Required to install PHP dependencies of the console app).
```

The last versions of SWIG and PHP that we have checked are:
- PHP 7.4.3
- SWIG 4.0.1

## How to build and run the project:  

- Configure the project for PHP:

```
        ./autogen.sh
        ./configure --enable-php 
```  

- Build the shared libraries and packages:
``` 
        make
```  

**Note:** Only PHP 7 is supported at the moment. 

You will need to make some adjustements in configuration file `php.ini`

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

## Run megacli with PHP  
```
 cd <SDK>/examples/php
 composer install
 php megacli.php
```

