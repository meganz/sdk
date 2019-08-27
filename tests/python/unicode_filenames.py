"""
 Application for generating random Unicode filenames

 (c) 2013-2014 by Mega Limited, Wellsford, New Zealand

 This file is part of the MEGA SDK - Client Access Engine.

 Applications using the MEGA API must present a valid application key
 and comply with the the rules set forth in the Terms of Service.

 The MEGA SDK is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

 @copyright Simplified (2-clause) BSD License.

 You should have received a copy of the license along with this
 program.
"""

# python unicode_filenames.py -o out -p -c 100 -m 0xFF -l 1

import random
import os
import platform
import string
import sys
import argparse

def touch(path):
    """
    create an empty file
    update utime
    """
    with open(path, 'a'):
        os.utime(path, None)

def gen_name(l, exclude, max_val):
    """
    Generate random string of size l
    """
    name = u""
    while len(name) < l:
        c = unichr(random.randint(0, max_val))
        if c not in exclude:
            name = name + c
    return name

class hexact(argparse.Action):
    'An argparse.Action that handles hex string input'
    def __call__(self, parser, namespace, values, option_string=None):
        base = 10
        if '0x' in values:
            base = 16
        setattr(namespace, self.dest, int(values, base))
        return

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Create files with random Unicode filenames.")
    parser.add_argument("-c", "--file-count", help="How many files to create, default is 10000 (not checking for unique names)", default=1000, type=int)
    parser.add_argument("-l", "--filename-length", help="Generated filenames length, default is 20", default=20, type=int)
    parser.add_argument("-m", "--max-unicode-char", help="Maximal Unicode character, default is 0xFFFF", action=hexact, default=0xFFFF)
    parser.add_argument("-o", "--output-dir", help="Output dir (required)")
    parser.add_argument("-p", "--toprint", help="print generated filename to stdout", action="store_true")
    args = parser.parse_args()

    if args.output_dir is None:
        parser.print_help()
        sys.exit(1)

    try:
        os.makedirs(args.output_dir)
    except OSError, e:
        pass

    if platform.system() == "Windows":
        # Unicode characters 1 through 31, as well as quote ("), less than (<), greater than (>), pipe (|), backspace (\b), null (\0) and tab (\t).
        exclude = string.punctuation + u"\t" +  u''.join([unichr(x) for x in range(0, 32)])
    else:
        # find . -exec rm {} \;
        exclude = u"/" + u"." + u''.join([unichr(x) for x in range(0, 1)])

    for _ in range(0, args.file_count):
        s = gen_name(args.filename_length, exclude, args.max_unicode_char)
        fname = os.path.join(args.output_dir, s)
        if args.toprint:
            print fname.encode('utf-8')
        touch(fname)
