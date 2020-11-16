Build/Install Python Bindings
=============================

These things need to be done to build and install Python bindings usable in a
normal fashion:

* The Mega SDK needs to be built with the Python bindings.

* A Python distribution package (a "Wheel") needs to be built, which then can
  be installed.

* Install the Python package (also needed Python devel package).

The instructions given here are strictly only valid for Linux. The may need
adaptation for other platforms.

**Note:** we will refer to the root directory where SDK is downloaded as `<SDK>`

Prerequisites
-------------

To build the Python bindings, you will need to have some things installed on
your system:

* SWIG (to generate the bindings automatically from interface code)
* A suitable C++ compiler tool chain
* Autotools
* A Python runtime along with the development headers to compile against


Build Python Bindings
---------------------

* Configure the project for Python:
```
    ./autogen.sh
    ./configure --disable-silent-rules --enable-python --disable-examples
```
* Build the shared libraries and packages:
```
    make
```

Build Python Distribution Package
---------------------------------

To use the Mega API from Python, you need to build the Python package as a platform specific "Wheel" package,
as it contains all native libraries (shared libraries, DLLs) required.  
```
    cd <SDK>/bindings/python/  
    python setup.py bdist_wheel  
```
The package created will be located in folder `<SDK>/bindings/python/dist/`.

**Note:** You may need to install the `wheel` package for Python, if your Python
is not by default equipped for it, yet. This could be the (Linux) `python-wheel`
distribution package, or by using e. g. `pip install wheel`.


Install Python Distribution Package
-----------------------------------

Once you have generated the Wheel package located at `<SDK>/bindings/python/dist/`, you need to install it by using `pip` in the common
fashion, e. g.

    pip install megasdk-2.6.0-py2.py3-none-any.whl

**Note:**  Once the package has been generated, you will need to import from Python with command `import mega`


Test Installed Package
----------------------

    import mega #`(if you haven't done it yet)`
    api = mega.MegaApi('test')
    print(dir(api)))


Run megacli with python
-----------------------------------

    python <SDK>/examples/python/megacli.py
