Contributions
=============

Eclipse
-------

Contains configurations, formatters and templates to ease the use of
the Eclipse IDE with the C/C++ Development Tools.


Static Code Check
-----------------

### N'SIQ CppStyle

N'SIQ CppCheck is a static code and code style checker. It is
available as open source from here:

https://code.google.com/p/nsiqcppstyle/

Checks can simply be run using the given list of checks as following:

    nsiqcppstyle -f contrib/nsiq_filefilter.txt src/
    nsiqcppstyle -f contrib/nsiq_filefilter.txt include/

A URL with further information on the different checks used is given
at the top of the file configuration file `nsiq_filefilter.txt`.
Further rules for checks can be implemented in simple Python files.


### Cppcheck

Cppcheck is a static code checker for C++.  It can very easily be
integrated into Eclipse through the `cppcheclipse` extension (from
Eclipse Marketplace).

For manual checks:

Checks for the `.cpp` files can be fun as follows:

    cppcheck --template='{file};{line};{severity};{id};{message}' \
      --enable=style,information,performance,portability,missingInclude,unusedFunction \
      --std=c++03 --force \
      $(find src/ -type f -name "*.cpp")

The checks for the `.h` headers can be run as follows:

    cppcheck --template='{file};{line};{severity};{id};{message}' \
      --enable=style,information,performance,portability,missingInclude,unusedFunction \
      --std=c++03 --force \
      $(find include/ -type f -name "*.h")
