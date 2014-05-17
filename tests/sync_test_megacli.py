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
    def __init__(self, local_mount_in, local_mount_out, delete_tmp_files=True, use_large_files=True):
        """
        local_mount_in: local upsync folder
        local_mount_out: local downsync folder
        """
        self.work_dir = os.path.join(".", "work_dir")
        SyncTestApp.__init__(self, local_mount_in, local_mount_out, self.work_dir, delete_tmp_files, use_large_files)

    def sync(self):
        time.sleep(5)

    def start(self):
        # try to create work dir
        return True

    def finish(self):
        try:
            shutil.rmtree(self.work_dir)
        except OSError:
            logging.error("Failed to remove dir: %s" % self.work_dir)

    def is_alive(self):
        """
        return True if application instance is running
        """
        s = subprocess.Popen(["ps", "axw"], stdout=subprocess.PIPE)
        for x in s.stdout:
            if re.search("megacli", x):
                return True
        return False

    def pause(self):
        """
        pause application
        """
        #TODO: implement this !
        raise NotImplementedError("Not Implemented !")
    def unpause(self):
        """
        unpause application
        """
        #TODO: implement this !
        raise NotImplementedError("Not Implemented !")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--test1", help="test_create_delete_files", action="store_true")
    parser.add_argument("--test2", help="test_create_rename_delete_files", action="store_true")
    parser.add_argument("--test3", help="test_create_delete_dirs", action="store_true")
    parser.add_argument("--test4", help="test_create_rename_delete_dirs", action="store_true")
    parser.add_argument("--test5", help="test_sync_files_write", action="store_true")
    parser.add_argument("--test6", help="test_local_operations", action="store_true")
    parser.add_argument("-d", "--debug", help="use debug output", action="store_true")
    parser.add_argument("-l", "--large", help="use large files for testing", action="store_true")
    parser.add_argument("upsync_dir", help="local upsync directory")
    parser.add_argument("downsync_dir", help="local downsync directory")
    args = parser.parse_args()

    if args.debug:
        lvl = logging.DEBUG
    else:
        lvl = logging.INFO

    logging.StreamHandler(sys.stdout)
    logging.basicConfig(format='[%(asctime)s] %(message)s', datefmt='%Y-%m-%d %H:%M:%S', level=lvl)

    logging.info("")
    logging.info("1) Start the first [megacli] and run the following command:  sync " + sys.argv[1] + " [remote folder]")
    logging.info("2) Start the second [megacli] and run the following command:  sync " + sys.argv[2] + " [remote folder]")
    logging.info("3) Wait for both folders get fully synced")
    logging.info("4) Run sync_test.py")
    logging.info("")

    with SyncTestMegaCliApp(args.upsync_dir, args.downsync_dir, True, args.large) as app:
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

        testRunner = xmlrunner.XMLTestRunner(output='test-reports')
        testRunner.run(suite)
