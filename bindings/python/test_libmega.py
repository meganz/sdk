import sys
import os

libsdir = os.getcwd() + '/.libs'
if os.path.isdir(libsdir) and os.path.isfile(libsdir + '/_mega.so'):
    sys.path.insert(0, libsdir)

from mega import *

# TODO: extend test example
def main():
    a = MegaApi("test")
    print a

if __name__ == '__main__':
    main()
