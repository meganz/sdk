# -*- coding: utf-8 -*-
"""
Configuration file for the `diffcheck.py` tool.

This file should be checked into the repository configuring the
project-specific behaviour. Local (user-specific) modifications can be
made by creating a `local_config.py` file in the same directory as this,
which can contain options overlaying those specified for the proejct
here.
"""

# Checkers to run. Available options for checkers are:
# "jshint", "jscs", "cppcheck", "nsiqcppstyle"
checkers = ['jshint', 'jscs']

# Extra options for designated checkers.
# This configuration needs an entry for every encountered checker if the
# `checkers` option above.
extra_options = {
    'jshint': {'norules': False},
    'jscs': {'norules': False},
    'cppcheck': {},
    'nsiqcppstyle': {}
}

# Paths for the executables to use.
JSHINT_BIN = 'node_modules/.bin/jshint'
JSCS_BIN = 'node_modules/.bin/jscs'
CPPCHECK_BIN = 'cppcheck'
NSIQCPPSTYLE_BIN = '/usr/local/nsiqcppstyle/nsiqcppstyle.py'
JSHINT_RULES = '--verbose'
JSCS_RULES = '--verbose'

# Command line configuration.
JSHINT_COMMAND = 'node {binary} {rules} .'
JSCS_COMMAND = 'node {binary} {rules} .'

CPPCHECK_COMMAND = ("{command}"
                    " --template={{file}};{{line}};{{severity}};{{id}};{{message}}"
                    " --enable=warning,portability,information,missingInclude"
                    " --std=c++03 --force"
                    " --quiet"
                    " -I include"
                    " -I include/mega/{platform}"
                    " src/ examples/")
NSIQCPPSTYLE_COMMAND = ('python {binary} --output=csv --ci -o {outfile}'
                        ' -f contrib/nsiq_filefilter.txt .')


# Overlay project-config with a potentially available local configuration.
try:
    from local_config import *
except ImportError:
    pass
