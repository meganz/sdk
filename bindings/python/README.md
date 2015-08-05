Build/Install Python Bindings
=============================

These things need to be done to build and install Python bindings usable in a
normal fashion:

* The Mega SDK needs to be built with the Python bindings.

* A Python distribution package (a "Wheel") needs to be built, which then can
  be installed.

* Install the Python package.

The instructions given here are strictly only valid for Linux. The may need
adaptation for other platforms.


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

    ./autogen.sh
    ./configure --disable-silent-rules --enable-python --disable-examples

* Build the shared libraries and packages:

    make


Build Python Distribution Package
---------------------------------

The Python package to be built will be a platform specific "Wheel" package,
as it contains all native libraries (shared libraries, DLLs) required to use
the Mega API from Python.

    python setup.py bdist_wheel

The package created will be located in folder `dist/`.

**Note:** You may need to install the `wheel` package for Python, if your Python
is not by default equipped for it, yet. This could be the (Linux) `python-wheel`
distribution package, or by using e. g. `pip install wheel`.


Install Python Distribution Package
-----------------------------------

The Wheel package should then be easy to install using `pip` in the common
fashion, e. g.

    pip install megasdk-2.6.0-py2.py3-none-any.whl


Test Installed Package
----------------------

    import mega
    api = mega.MegaApi('test')
    print(dir(api)))
