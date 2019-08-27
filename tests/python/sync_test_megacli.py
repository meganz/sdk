"""
 Application for testing syncing algorithm

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

import sys
import os
import time
import shutil
import unittest
import xmlrunner
import subprocess
import re
from sync_test_app import SyncTestApp
from sync_test import SyncTest
import logging
import argparse


class SyncTestMegaCliApp(SyncTestApp):
    """
    operates with megacli application
    """
    def __init__(self, local_mount_in, local_mount_out, delete_tmp_files=True, use_large_files=True, check_if_alive=True):
        """
        local_mount_in: local upsync folder
        local_mount_out: local downsync folder
        """
        self.work_dir = os.path.join(".", "work_dir")
        SyncTestApp.__init__(self, local_mount_in, local_mount_out, self.work_dir, delete_tmp_files, use_large_files)
        self.check_if_alive = check_if_alive

    def sync(self):
        time.sleep(5)

    def start(self):
        # try to create work dir
        return True

    def finish(self):
        try:
            shutil.rmtree(self.work_dir)
        except OSError, e:
            logging.error("Failed to remove dir: %s (%s)" % (self.work_dir, e))

    def is_alive(self):
        """
        return True if application instance is running
        """
        if not self.check_if_alive:
            return True

        s = subprocess.Popen(["ps", "axw"], stdout=subprocess.PIPE)
        for x in s.stdout:
            if re.search("megacli", x):
                return True
        return False

    def pause(self):
        """
        pause application
        """
        # TODO: implement this !
        raise NotImplementedError("Not Implemented !")

    def unpause(self):
        """
        unpause application
        """
        # TODO: implement this !
        raise NotImplementedError("Not Implemented !")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--test1", help="test_create_delete_files", action="store_true")
    parser.add_argument("--test2", help="test_create_rename_delete_files", action="store_true")
    parser.add_argument("--test3", help="test_create_delete_dirs", action="store_true")
    parser.add_argument("--test4", help="test_create_rename_delete_dirs", action="store_true")
    parser.add_argument("--test5", help="test_sync_files_write", action="store_true")
    parser.add_argument("--test6", help="test_local_operations", action="store_true")
    parser.add_argument("--test7", help="test_update_mtime", action="store_true")
    parser.add_argument("--test8", help="test_create_rename_delete_unicode_files_dirs", action="store_true")
    parser.add_argument("--test9", help="test_create_move_delete_files", action="store_true")
    parser.add_argument("--test10", help="test_mimic_update_with_backup_files", action="store_true")
    parser.add_argument("-a", "--all", help="run all tests", action="store_true")
    parser.add_argument("-b", "--basic", help="run basic, stable tests", action="store_true")
    parser.add_argument("-d", "--debug", help="use debug output", action="store_true")
    parser.add_argument("-l", "--large", help="use large files for testing", action="store_true")
    parser.add_argument("-n", "--nodelete", help="Do not delete work files", action="store_false")
    parser.add_argument("-c", "--check", help="Do not check if megacli is running (useful, if other application is used for testing)", action="store_false")
    parser.add_argument("upsync_dir", help="local upsync directory")
    parser.add_argument("downsync_dir", help="local downsync directory")
    args = parser.parse_args()

    if args.debug:
        lvl = logging.DEBUG
    else:
        lvl = logging.INFO

    if args.all:
        args.test1 = args.test2 = args.test3 = args.test4 = args.test5 = args.test6 = args.test7 = args.test8 = args.test9 = args.test10 = True
    if args.basic:
        args.test1 = args.test2 = args.test3 = args.test4 = True

    logging.StreamHandler(sys.stdout)
    logging.basicConfig(format='[%(asctime)s] %(message)s', datefmt='%Y-%m-%d %H:%M:%S', level=lvl)

    logging.info("")
    logging.info("1) Start the first [megacli] and run the following command:  sync " + args.upsync_dir + " [remote folder]")
    logging.info("2) Start the second [megacli] and run the following command:  sync " + args.downsync_dir + " [remote folder]")
    logging.info("3) Wait for both folders get fully synced")
    logging.info("4) Run: python %s", sys.argv[0])
    logging.info("")
    logging.info("   Make sure you have unittest module installed:   pip install unittest-xml-reporting")
    logging.info("")
    time.sleep(5)

    with SyncTestMegaCliApp(args.upsync_dir, args.downsync_dir, args.nodelete, args.large, args.check) as app:
        suite = unittest.TestSuite()

        if args.test1:
            suite.addTest(SyncTest("test_create_delete_files", app))

        if args.test2:
            suite.addTest(SyncTest("test_create_rename_delete_files", app))

        if args.test3:
            suite.addTest(SyncTest("test_create_delete_dirs", app, ))

        if args.test4:
            suite.addTest(SyncTest("test_create_rename_delete_dirs", app))

        if args.test5:
            suite.addTest(SyncTest("test_sync_files_write", app))

        if args.test6:
            suite.addTest(SyncTest("test_local_operations", app))

        if args.test7:
            suite.addTest(SyncTest("test_update_mtime", app))

        if args.test8:
            suite.addTest(SyncTest("test_create_rename_delete_unicode_files_dirs", app))

        if args.test9:
            suite.addTest(SyncTest("test_create_move_delete_files", app))
            
        if args.test10:
            suite.addTest(SyncTest("test_mimic_update_with_backup_files", app))

        testRunner = xmlrunner.XMLTestRunner(output='test-reports')
        testRunner.run(suite)
