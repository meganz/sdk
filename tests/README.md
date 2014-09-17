Running SDK unittests:

* Get the latest Google Test sources: https://code.google.com/p/googletest/
* Extract the archive
* Configure and compile Google Test library:

```
autoreconf -fiv
./configure
make
```
* Provide ```--enable-tests --with-gtest=[path to GTest directory]``` arguments when compiling MEGA SDK
* Run unittests:
```
cd tests
./api_test [flags]
```
