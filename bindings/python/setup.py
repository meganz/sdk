#!/bin/env python
# -*- coding: utf-8 -*-
"""
This setup is loosely following the instructions adapted from
https://hynek.me/articles/sharing-your-labor-of-love-pypi-quick-and-dirty/
"""
import os
import re

from setuptools import setup


def read(fname):
    """
    Utility function to read the README file.
    Used for the long_description.  It's nice, because now
    1) we have a top level README file and
    2) it's easier to type in the README file than to put a raw string in below ...
    """
    return open(os.path.join(os.path.dirname(__file__), fname)).read()

def get_version():
    """
    Grabs and returns the version and release numbers from autotools.
    """
    version_h = open(os.path.join('..', '..', 'include', 'mega', 'version.h')).read()
    major = re.search('define MEGA_MAJOR_VERSION ([0-9]+)',
                      version_h)
    minor = re.search('define MEGA_MINOR_VERSION ([0-9]+)',
                      version_h)
    micro = re.search('define MEGA_MICRO_VERSION (.+?)',
                      version_h)
    if major:
        major, minor, micro = major.group(1), minor.group(1), micro.group(1)
        version = '.'.join([major, minor])
    else:
        version = 'raw_development'
    if micro:
        release = '.'.join([major, minor, micro])
    else:
        release = 'raw_development'
    return version, release

def make_symlink(src, dst):
    """Makes a symlink, ignores errors if it's there already."""
    try:
        os.symlink(src, dst)
    except OSError as e:
        if e.strerror != 'File exists':
            raise e

def remove_file(fname):
    """Removes a file/link, ignores errors if it's not there any more."""
    try:
        os.remove(fname)
    except OSError as e:
        if e.strerror != 'No such file or directory':
            raise e

# Put native library modules into a "good place" for the package.
make_symlink('../../src/.libs/libmega.so', 'libmega.so')
make_symlink('.libs/_mega.so', '_mega.so')

# Create a dummy __init__.py if not present.
_init_file = '__init__.py'
_init_file_created = False
if not os.path.exists(_init_file):
    with open(_init_file, 'wb') as fd:
        _init_file_created = True

setup(
    name='megasdk',
    version=get_version()[1],
    description='Python bindings to the Mega file storage SDK.',
    long_description=read('DESCRIPTION.rst'),
    url='http://github.com/meganz/sdk/',
    license='Simplified BSD',
    author='Guy Kloss',
    author_email='gk@mega.co.nz',
    packages=['mega'],
    package_dir={'mega': '.'},
    package_data = {
        'mega': ['libmega.so', '_mega.so'],
    },
    exclude_package_data = {'': ['test_libmega.py']},
    include_package_data=True,
    keywords=['MEGA', 'privacy', 'cloud', 'storage', 'API'],
    classifiers=[
        'Development Status :: 4 - Beta',
        'Intended Audience :: Developers',
        'Natural Language :: English',
        'License :: OSI Approved :: BSD License',
        'Operating System :: OS Independent',
        'Programming Language :: Python',
        'Programming Language :: Python :: 2',
        'Programming Language :: Python :: 2.6',
        'Programming Language :: Python :: 2.7',
        'Programming Language :: Python :: Implementation :: CPython',
        'Programming Language :: Python :: 3',
        'Programming Language :: Python :: 3.3',
        'Programming Language :: Python :: 3.4',
        'Topic :: Software Development :: Libraries :: Python Modules',
    ],
)

# Clean up some temporary stuff.
remove_file('libmega.so')
remove_file('_mega.so')
if _init_file_created:
    remove_file(_init_file)
