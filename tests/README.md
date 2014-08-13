Running SDK unittests:

1) Get the latest Google Test sources: https://code.google.com/p/googletest/
2) Extract the archive
3) Configure and compile Google Test library:
```
autoreconf -fiv
./configure
make
```
4) Provide ```--enable-tests --with-gtest=[path to GTest directory]``` arguments when compiling MEGA SDK
5) Run unittests:
```
cd tests
./api_test [flags]
```
