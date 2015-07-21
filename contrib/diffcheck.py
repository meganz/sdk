#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""
Test runner that will limit the output of tests to a result set relevant for a
particular diff in Git. That is a diff between revisions or a diff between two
branches' tips.

This test runner will work with both Python 2.7 as well as 3.x.
"""

## Created: 23 May 2015 Guy Kloss <gk@mega.co.nz>
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

__author__ = 'Guy Kloss <gk@mega.co.nz>'

import argparse
import os
import sys
import re
import logging
import tempfile
import subprocess
import collections
import config

PLATFORMS = {'posix': 'posix',
             'nt': 'win32'}
PROJECT_PATH = os.path.abspath(os.path.join(os.path.dirname(__file__),
                                            os.path.pardir))
PATH_SPLITTER = re.compile(r'\\|/')


def get_git_line_sets(base, target):
    """
    Obtains the Git diff between the base and target to identify the lines that
    contain alterations in the target code. If branch names are given for the
    base or target, the tips of those branches are used.

    :param base: Base branch/commit for the diff.
    :param target: Target branch/commit for the diff.
    :return: A dictionary of changed line numbers. The key in the dictionary is
        the file path, the value is a set of line numbers.
    """
    # Get the Git output for the desired diff.
    logging.info('Extracting relevant lines from Git diff ...')
    command = 'git diff -U0 {} {}'.format(base, target)
    output = subprocess.check_output(command.split())
    diff = output.decode('latin1').split('\n')

    # Hunt down lines of changes for different files.
    file_line_mapping = collections.defaultdict(set)
    current_file = None
    for line in diff:
        if line.startswith('+++'):
            # Line giving target file.
            for_file = line.split()[1]
            current_file = tuple(re.split(PATH_SPLITTER, for_file[2:]))
        elif line.startswith('@@'):
            # Line giving alteration line range of diff fragment.
            target_lines = line.split()[2].split(',')
            start_line = int(target_lines[0])
            line_range = int(target_lines[1]) if len(target_lines) == 2 else 1
            # Update our lines if we have target lines.
            if line_range > 0:
                file_line_mapping[current_file].update(range(start_line,
                                                             start_line + line_range - 1))

    return file_line_mapping


def reduce_jshint(file_line_mapping, **extra):
    """
    Runs JSHint on the project with the default configured rules. The output
    is reduced to only contain entries from the Git change set.

    :param file_line_mapping: Mapping of files with changed lines (obtained
        `get_git_line_sets()`).
    :param extra: Optional keyword arguments:
        `norules`: If true, omit verbose output of violated rule identifier
                   (default: `False` to include rules).
    :return: A tuple containing the formatted string suitable for output and
        an integer containing the number of failed rules.
    """
    norules = extra['norules'] if 'norules' in extra else False
    # Get the JSHint output.
    logging.info('Obtaining JSHint output ...')
    os.chdir(PROJECT_PATH)
    rules = config.JSHINT_RULES if not norules else ''
    command = config.JSHINT_COMMAND.format(binary=config.JSHINT_BIN,
                                           rules=rules)
    output = None
    try:
        output = subprocess.check_output(command.split())
    except subprocess.CalledProcessError as ex:
        # JSHint found something, so it has returned an error code.
        # But we still want the output in the same fashion.
        output = ex.output
    output = output.decode('utf8').split('\n')

    # Go through output and collect only relevant lines to the result.
    result = ['\nJSHint output:\n==============\n']
    jshint_expression = re.compile(r'(.+): line (\d+), col \d+, .+')
    for line in output:
        parse_result = jshint_expression.findall(line)
        # Check if we've got a relevant line.
        if parse_result:
            file_name, line_no = parse_result[0][0], int(parse_result[0][1])
            file_name = tuple(re.split(PATH_SPLITTER, file_name))
            # Check if the line is part of our selection list.
            if line_no in file_line_mapping[file_name]:
                result.append(line)

    # Add the number of errors and return in a nicely formatted way.
    error_count = len(result) - 1
    result.append('\n{} errors'.format(error_count))
    return '\n'.join(result), error_count


def reduce_jscs(file_line_mapping, **extra):
    """
    Runs JSHCS on the project with the default configured rules. The output
    is reduced to only contain entries from the Git change set.

    :param file_line_mapping: Mapping of files with changed lines (obtained
        `get_git_line_sets()`).
    :param extra: Optional keyword arguments:
        `norules`: If true, omit verbose output of violated rule identifier
                   (default: `False` to include rules).
    :return: A tuple containing the formatted string suitable for output and
        an integer containing the number of failed rules.
    """
    norules = extra['norules'] if 'norules' in extra else False
    # Get the JSCS output.
    logging.info('Obtaining JSCS output ...')
    os.chdir(PROJECT_PATH)
    rules = config.JSHINT_RULES if not norules else ''
    command = config.JSCS_COMMAND.format(binary=config.JSCS_BIN,
                                         rules=rules)
    output = None
    try:
        output = subprocess.check_output(command.split())
    except subprocess.CalledProcessError as ex:
        # JSCS found something, so it has returned an error code.
        # But we still want the output in the same fashion.
        output = ex.output
    output = output.decode('utf8').split('\n\n')

    # Go through output and collect only relevant lines to the result.
    result = ['\nJSCS output:\n============']
    lines_expression = re.compile(r'^ +(\d+) |.*(?:\n|\r\n?)-', re.MULTILINE)
    file_expression = re.compile(r'^[^\b].* (?:\./)?(.+) :$', re.MULTILINE)
    for item in output:
        # Do the processing for every block here.
        line_no_candidates = lines_expression.findall(item, re.MULTILINE)
        # Check if we've got a relevant block.
        if line_no_candidates and '' in line_no_candidates:
            line_no = int(line_no_candidates[line_no_candidates.index('') - 1])
            file_name = file_expression.findall(item)[0]
            file_name = tuple(re.split(PATH_SPLITTER, file_name))
            # Check if the line is part of our selection list.
            if line_no in file_line_mapping[file_name]:
                result.append(item)

    # Add the number of errors and return in a nicely formatted way.
    error_count = len(result) - 1
    result.append('\n{} code style errors found.'.format(error_count))
    return '\n\n'.join(result), error_count


def reduce_cppcheck(file_line_mapping, **extra):
    """
    Runs CppCheck on the project with the default configured rules. The output
    is reduced to only contain entries from the Git change set.

    :param file_line_mapping: Mapping of files with changed lines (obtained
        `get_git_line_sets()`).
    :param extra: Optional keyword arguments:
        `platform`: A specific platform to test for, as used in
                    `include/mega/` (default: detect local system's platform).
    :return: A tuple containing the formatted string suitable for output and
        an integer containing the number of failed rules.
    """
    logging.info('Obtaining CppCheck output ...')
    platform = platform = PLATFORMS[os.name]
    if 'platform' in extra:
        # Override if given.
        platform = extra['platform']
        
    # Get the CppCheck output.
    os.chdir(PROJECT_PATH)
    command = config.CPPCHECK_COMMAND.format(command=config.CPPCHECK_BIN,
                                             platform=platform)
    output = None
    try:
        output = subprocess.check_output(command.split(),
                                         stderr=subprocess.STDOUT)
    except subprocess.CalledProcessError as ex:
        # CppCheck found something, so it has returned an error code.
        # But we still want the output in the same fashion.
        output = ex.output
    output = output.decode('utf8').split('\n')
    
    # Go through output and collect only relevant lines to the result.
    logging.debug('Reducing CppCheck output ...')
    result = ['\nCppCheck output:\n================\n']
    cppcheck_expression = re.compile(r'^(.+);(\d+);')
    for line in output:
        parse_result = cppcheck_expression.findall(line)
        # Check if we've got a relevant line.
        if parse_result:
            file_name, line_no = parse_result[0][0], int(parse_result[0][1])
            file_name = tuple(re.split(PATH_SPLITTER, file_name))
            # Check if the line is part of our selection list.
            if line_no in file_line_mapping[file_name]:
                formatted = '; '.join(line.split(';'))
                result.append(formatted)

    # Add the number of errors and return in a nicely formatted way.
    error_count = len(result) - 1
    result.append('\n{} errors'.format(error_count))
    return '\n'.join(result), error_count


def reduce_nsiqcppstyle(file_line_mapping, **extra):
    """
    Runs N'SIQ CppStyle on the project with the project configured rules.
    Thet output is reduced to only contain entries from the Git change set.

    :param file_line_mapping: Mapping of files with changed lines (obtained
        `get_git_line_sets()`).
    :param extra: Optional keyword arguments.
    :return: A tuple containing the formatted string suitable for output and
        an integer containing the number of failed rules.
    """
    logging.info("Obtaining N'SIQ CppStyle output ...")
    
    # Get the output.
    outfile = tempfile.mktemp()
    os.chdir(PROJECT_PATH)
    command = config.NSIQCPPSTYLE_COMMAND.format(binary=config.NSIQCPPSTYLE_BIN,
                                                 outfile=outfile)

    # Just capturing the console output to keep things quiet.
    consoleoutput = None
    try:
        consoleoutput = subprocess.check_output(command.split(),
                                                stderr=subprocess.STDOUT)
    except subprocess.CalledProcessError as ex:
        # CppCheck found something, so it has returned an error code.
        # But we still want the output in the same fashion.
        consoleoutput = ex.output

    # Get the output and delete the outfile.
    output = open(outfile, 'rt').read().split('\n')
    os.remove(outfile)
    
    # Go through output and collect only relevant lines to the result.
    logging.debug("Reducing N'SIQ CppStyle output ...")
    result = ["\nN'SIQ CppStyle output:\n======================\n"]
    # This stunt is required to fix Windows path problems with backslashes.
    base_path = os.path.join(PROJECT_PATH, '')
    if os.name == 'nt':
        base_path = base_path.replace('\\', '\\\\')
    nsiqcppstyle_expression = re.compile(r'^{}(.+?),(\d+),(.+),'
                                         .format(base_path))
    for line in output:
        parse_result = nsiqcppstyle_expression.findall(line)
        # Check if we've got a relevant line.
        if parse_result:
            file_name = tuple(re.split(PATH_SPLITTER, parse_result[0][0]))
            line_no = int(parse_result[0][1])
            rest = parse_result[0][2]
            # Check if the line is part of our selection list.
            if line_no in file_line_mapping[file_name]:
                formatted = ', '.join(['/'.join(file_name), str(line_no)]
                                      + rest.split(','))
                result.append(formatted)

    # Add the number of errors and return in a nicely formatted way.
    error_count = len(result) - 1
    result.append('\n{} errors'.format(error_count))
    return '\n'.join(result), error_count


def reduce_verapp(file_line_mapping, **extra):
    """
    Runs Vera++ on the project with the project configured rules. The output
    is reduced to only contain entries from the Git change set.

    :param file_line_mapping: Mapping of files with changed lines (obtained
        `get_git_line_sets()`).
    :param extra: Optional keyword arguments.
    :return: A tuple containing the formatted string suitable for output and
        an integer containing the number of failed rules.
    """
    logging.info('Obtaining Vera++ output ...')

    # Collect all C++ source files.
    cpp_source_expression = re.compile(r'\.(h|cpp)$')
    source_files = [os.path.join(dirpath, name)
                    for dirpath, _, items in os.walk(PROJECT_PATH)
                    for name in items
                    if cpp_source_expression.findall(name.lower())]

    # Get the Vera++ output.
    os.chdir(PROJECT_PATH)
    command = config.VERAPP_COMMAND.format(command=config.VERAPP_BIN,
                                           rules=' '.join(['-R {}'.format(item)
                                                           for item in config.VERAPP_RULES]))
    output = None
    try:
        process = subprocess.Popen(command.split(),
                                   stdout=subprocess.PIPE,
                                   stderr=subprocess.STDOUT,
                                   stdin=subprocess.PIPE)
        output, _ = process.communicate(bytes('\n'.join(source_files)
                                        .encode('ASCII')))
    except subprocess.CalledProcessError as ex:
        # Vera++ found something, so it has returned an error code.
        # But we still want the output in the same fashion.
        output = ex.output
    output = output.decode('utf8').split('\n')

    # Go through output and collect only relevant lines to the result.
    result = ['\nVera++ output:\n==============\n']
    # This stunt is required to fix Windows path problems with backslashes.
    base_path = os.path.join(PROJECT_PATH, '')
    if os.name == 'nt':
        base_path = base_path.replace('\\', '\\\\')
    verapp_expression = re.compile(r'^{}(.+?):(\d+): '.format(base_path))
    for line in output:
        parse_result = verapp_expression.findall(line)
        # Check if we've got a relevant line.
        if parse_result:
            file_name, line_no = parse_result[0][0], int(parse_result[0][1])
            file_name = tuple(re.split(PATH_SPLITTER, file_name))
            # Check if the line is part of our selection list.
            # TODO: find proper file name sub-set (remove base path)
            if line_no in file_line_mapping[file_name]:
                result.append(line)

    # Add the number of errors and return in a nicely formatted way.
    error_count = len(result) - 1
    result.append('\n{} errors'.format(error_count))
    return '\n'.join(result), error_count
    

def main(base, target, norules):
    """
    Run the JSHint and JSCS tests and present output ont eh console via print.
    """
    CHECKER_MAPPING = {'jshint': reduce_jshint,
                       'jscs': reduce_jscs,
                       'cppcheck': reduce_cppcheck,
                       'nsiqcppstyle': reduce_nsiqcppstyle,
                       'vera++': reduce_verapp}
    
    file_line_mapping = get_git_line_sets(base, target)
    results = []
    total_errors = 0
    for checker in config.checkers:
        worker = CHECKER_MAPPING[checker]
        extra_options = config.extra_options[checker]
        output, error_count = worker(file_line_mapping, **extra_options)
        results.append(output)
        total_errors += error_count
    logging.info('Output of reduced results ...')
    print('\n\n'.join(results))
    if total_errors > 0:
        sys.exit(1)


if __name__ == '__main__':
    # Set up logging.
    logging.basicConfig(level=logging.DEBUG,
                        format='%(levelname)s\t%(asctime)s %(message)s')

    # Setup the command line argument parser.
    DESCRIPTION = ('Filter output from static code analyser and style checker '
                   'to only contain content relevant to a diff '
                   '(e. g. between commits or tips of branches). '
                   'This tool will filter output from JSHint and JSCS.')
    EPILOG = 'Note: if no revision (commit ID) is given, the branch tip will be used.'
    parser = argparse.ArgumentParser(description=DESCRIPTION, epilog=EPILOG)
    parser.add_argument('--norules', default=False, action='store_true',
                        help="Don't show rule names with description (default: show rules names)")
    parser.add_argument('base',
                        help='base revision or name of base branch')
    parser.add_argument('target', nargs='?', default='',
                        help=('target revision or name of target branch'
                              ' (default: tip of current branch)'))

    args = parser.parse_args()

    main(args.base, args.target, args.norules)
