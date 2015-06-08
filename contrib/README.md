Contributions
=============

Eclipse
-------

Contains configurations, formatters and templates to ease the use of
the Eclipse IDE with the C/C++ Development Tools.

(See `contrib/Eclipse/README.md`)


Static Code Check
-----------------

### Clang Static Analyzer

The Clang Static Analyzer is a source code analysis tool that finds
bugs in C, C++, and Objective-C programs.

For manual checks run clang-analyzer.sh script from "contrib/" folder:

    ./contrib/clang-analyzer.sh /usr/bin/clang++ sdk_check

Output HTML files will be placed in "sdk_check" folder.


### N'SIQ CppStyle

N'SIQ CppCheck is a static code and code style checker. It is
available as open source from here:

* Original project location: https://code.google.com/p/nsiqcppstyle/

* For convenience, a version that is updated and reduced in unnecessary
  stuff is available in an archive on our wiki:
  https://wiki.developers.mega.co.nz/SoftwareEngineering/CppCodingStyle

Checks can simply be run using the given list of checks as following:

    nsiqcppstyle -f contrib/nsiq_filefilter.txt .

Or to integrate it into Jenkins CI (see also
https://code.google.com/p/nsiqcppstyle/wiki/HudsonIntegration):

    nsiqcppstyle --ci --output=xml -f contrib/nsiq_filefilter.txt .

A URL with further information on the different checks used is given
at the top of the file configuration file `nsiq_filefilter.txt`.
Further rules for checks can be implemented in simple Python files.


### Cppcheck

Cppcheck is a static code checker for C++ (Debian/Ubuntu package
`cppcheck`).  It can very easily be integrated into Eclipse through
the `cppcheclipse` extension (from Eclipse Marketplace).

For integration into `vim` use the file `vimcppcheck.vim` included.

For manual checks just run the make target `cppcheck`:

    make cppcheck


### Reduced Output Checker

The amount of output can be quite overwhelming. To ease the pain for
this, the `contrib/` directory contains a `diffcheck.py` tool, which
extracts the changed lines in code between two branches or two
commits, and reduces the amount of output produced to only relevant
entries from all checkers configured.

    contrib/diffcheck.py 97ab5f8e a2f40975  # Between two commits.
    contrib/diffcheck.py 97ab5f8e           # Against current branch tip.
    contrib/diffcheck.py master my-feature  # From master tip to feature tip.
    contrib/diffcheck.py master             # From master to current tip.

This is to ease the enforcement of agreed general style for code
reviews on merge requests as well as enable developers to check their
work against a target branch before issuing a merge request to make
sure things are done correctly. Therefore the number of "round trips"
for the review process can be significantly reduced.

`diffcheck.py` is configured through `contrib/config.py`, which is
part of the repository.  If you want to make local adaptations, please
use `contrib/local_config.py` with local options, which will override
those from `config.py`.  `local_config.py` will not be committed to
the repository.


### Code Formatter

Automatic code formatting can be performed according to our specified
rules using the uncrustify tool (Debian/Ubuntu package `uncrustify`).
For other platforms from here:

http://uncrustify.sourceforge.net/

To format a single file `<file>` into `<file>.uncrustify`:

    uncrustify -c contrib/uncrustify.cfg <file>

To format many files *in place*

    find src/ -type f -name "*.cpp" -exec \
        uncrustify --replace \
        -c ~/workspace/megasdk/contrib/uncrustify.cfg {} \;
    find include/ -type f -name "*.h" -exec \
        uncrustify --replace \
        -c ~/workspace/megasdk/contrib/uncrustify.cfg {} \;

*Note:* Sometimes the uncrustify tool terminates with a segmentation
fault. In those cases there will be an empty (0 bytes) file
`<file>.uncrustify`.  These files will need to be sorted out manually!
