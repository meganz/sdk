#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""Simplistic test module for the Python bindings."""

## Created: 14 Jan 2015 Paul Ionkin <pi@mega.co.nz>
##
## (c) 2015 by Mega Limited, Auckland, New Zealand
##     http://mega.co.nz/
##     Simplified (2-clause) BSD License.
##
## You should have received a copy of the license along with this
## program.
##
## This file is part of the multi-party chat encryption suite.
##
## This code is distributed in the hope that it will be useful,
## but WITHOUT ANY WARRANTY; without even the implied warranty of
## MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

__author__ = 'Paul Ionkin <pi@mega.co.nz>'

import sys
import os
import pprint

_here = os.path.abspath(os.path.dirname(__file__))
_wrapper_dir = os.path.join(_here, '..', '..', 'bindings', 'python')
_libs_dir = os.path.join(_wrapper_dir, '.libs')
_shared_lib = os.path.join(_libs_dir, '_mega.so')
if os.path.isdir(_wrapper_dir) and os.path.isfile(_shared_lib):
    sys.path.insert(0, _wrapper_dir)  # mega.py
    sys.path.insert(0, _libs_dir)     # _mega.so

import mega

# TODO: extend test example
def main():
    api = mega.MegaApi("test")
    pprint.pprint(dir(api))

if __name__ == '__main__':
    main()
