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
# "jshint", "jscs", "cppcheck"
checkers = ['cppcheck']

# Extra options for designated checkers.
# This configuration needs an entry for every encountered checker if the
# `checkers` option above.
extra_options = {
    'jshint': {'norules': False},
    'jscs': {'norules': False},
    'cppcheck': {}
}


# Overlay project-config with a potentially available local configuration.
try:
    from local_config import *
except ImportError:
    pass
