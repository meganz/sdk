`tests` is the root level directory for SDK testing.

Unit and integration tests are contained within the `unit` and `integration` 
directories, respectively. Note that unit tests do NOT do I/O and are very fast
whereas integration tests typically require I/O, are slower, and harder to maintain. 

Unit and integration tests require the google test framework:
https://github.com/google/googletest

Building gtest and the tests itself depends very much on your OS and your build 
system of choice. We currently support CMake.

You can run all tests or just a subset of the tests by supplying a filter to the 
test executable, e.g., `./test_unit --gtest_filter=Crypto*`

Unit and integration tests are organized in such a way that the filename
should match the name of the contained test suite. A single test file should
only contain a single test suite. E.g., `Crypto_test.cpp` should only contain 
tests like `TEST(Crypto, blahblah)`. This makes test discovery more efficient.
Any testing framework code should live inside the `mt` namespace (= mega testing).

The `tool` directory contains standalone test applications that must be run manually.

The `python` directory contains work-in-progress system tests written in python.
