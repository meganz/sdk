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
import subprocess
from sync_test_base import get_random_str
from sync_test import SyncTest
from sync_test_app import SyncTestApp
import unittest
import xmlrunner
import logging
import argparse
import platform


class SyncTestMegaSyncApp(SyncTestApp):
    """
    operates with megasync application
    """
    def __init__(self, work_dir, remote_folder, delete_tmp_files=True, use_large_files=True):
        """
        work_dir: a temporary folder to place generated files
        remote_folder: a remote folder to sync
        """

        self.megasync_ch_in = None
        self.megasync_ch_out = None

        self.local_mount_in = os.path.join(work_dir, "sync_in")
        self.local_mount_out = os.path.join(work_dir, "sync_out")

        self.work_dir = os.path.join(work_dir, "tmp")
        self.remote_folder = remote_folder

        # init base class
        super(SyncTestMegaSyncApp, self).__init__(self.local_mount_in, self.local_mount_out, self.work_dir, delete_tmp_files, use_large_files)

        try:
            os.makedirs(self.local_mount_in)
        except OSError:
            pass

        try:
            os.makedirs(self.local_mount_out)
        except OSError:
            pass

        try:
            os.makedirs(self.work_dir)
        except OSError:
            pass

        if not os.access(self.local_mount_in, os.W_OK | os.X_OK):
            raise Exception("Not enough permissions to create / write to directory")

        if not os.access(self.local_mount_out, os.W_OK | os.X_OK):
            raise Exception("Not enough permissions to create / write to directory")

        if not os.access(self.work_dir, os.W_OK | os.X_OK):
            raise Exception("Not enough permissions to create / write to directory")

    def start_megasync(self, local_folder, type_str):
        """
        fork and launch "megasync" application
        local_folder: local folder to sync
        """
        # launch megasync
        base_path = os.path.join(os.path.dirname(__file__), '..')

        # the app is either in examples/ or in the project's root
        if platform.system() == "Windows":
            app_name = "megasync.exe"
        else:
            app_name = "megasync"
        bin_path = os.path.join(base_path, "examples")
        tmp = os.path.join(bin_path, app_name)
        if not os.path.isfile(tmp) or not os.access(tmp, os.X_OK):
            bin_path = os.path.join(base_path, "")
            tmp = os.path.join(bin_path, app_name)
            if not os.path.isfile(tmp) or not os.access(tmp, os.X_OK):
                raise Exception("megasync application is not found!")

        pargs = [os.path.join(bin_path, app_name), local_folder, self.remote_folder]
        output_fname = os.path.join(self.work_dir, "megasync" + "_" + type_str + "_" + get_random_str() + ".log")
        output_log = open(output_fname, "w")

        logging.info("Launching cmd: \"%s\", log: \"%s\"" % (" ".join(pargs), output_fname))

        try:
            ch = subprocess.Popen(pargs, universal_newlines=True, stdout=output_log, stderr=subprocess.STDOUT, bufsize=1, shell=False, env=os.environ)
        except OSError, e:
            logging.error("Failed to launch megasync process: %s" % e)
            return None
        return ch

    def sync(self):
        """
        TODO: wait for full synchronization
        """
        time.sleep(5)

    def start(self):
        """
        prepare and run tests
        """

        if os.environ.get('MEGA_EMAIL') is None or os.environ.get('MEGA_PWD') is None:
            logging.error("Environment variables MEGA_EMAIL and MEGA_PWD are not set !")
            return False

        # start "in" instance
        self.megasync_ch_in = self.start_megasync(self.local_mount_in, "in")
        # pause
        time.sleep(5)
        # start "out" instance
        self.megasync_ch_out = self.start_megasync(self.local_mount_out, "out")
        # check both instances
        if self.megasync_ch_in is None or self.megasync_ch_out is None:
            return False

        return True

    def finish(self):
        """
        kill megasync instances, remove temp folders
        """

        self.sync()

        # kill instances
        for _ in range(0, 5):
            if self.megasync_ch_in:
                self.megasync_ch_in.terminate()
                time.sleep(5)
            else:
                break

        for _ in range(0, 5):
            if self.megasync_ch_out:
                self.megasync_ch_out.terminate()
                time.sleep(5)
            else:
                break

    def is_alive(self):
        """
        return True if application instance is running
        """
        if not self.megasync_ch_in or not self.megasync_ch_out:
            return False
        return self.megasync_ch_in.poll() is None and self.megasync_ch_out.poll() is None

    def pause(self):
        """
        pause application
        """

    def unpause(self):
        """
        unpause application
        """

if __name__ == "__main__":
    parser = argparse.ArgumentParser(epilog="Please set MEGA_EMAIL and MEGA_PWD environment variables.")
    parser.add_argument("--test1", help="test_create_delete_files", action="store_true")
    parser.add_argument("--test2", help="test_create_rename_delete_files", action="store_true")
    parser.add_argument("--test3", help="test_create_delete_dirs", action="store_true")
    parser.add_argument("--test4", help="test_create_rename_delete_dirs", action="store_true")
    parser.add_argument("--test5", help="test_sync_files_write", action="store_true")
    parser.add_argument("--test6", help="test_local_operations", action="store_true")
    parser.add_argument("--test7", help="test_update_mtime", action="store_true")
    parser.add_argument("--test8", help="test_create_rename_delete_unicode_files_dirs", action="store_true")
    parser.add_argument("-a", "--all", help="run all tests", action="store_true")
    parser.add_argument("-b", "--basic", help="run basic, stable tests", action="store_true")
    parser.add_argument("-d", "--debug", help="use debug output", action="store_true")
    parser.add_argument("-l", "--large", help="use large files for testing", action="store_true")
    parser.add_argument("-n", "--nodelete", help="Do not delete work files", action="store_false")
    parser.add_argument("work_dir", help="local work directory")
    parser.add_argument("sync_dir", help="remote directory for synchronization")
    args = parser.parse_args()

    if args.debug:
        lvl = logging.DEBUG
        # megasync will use verbose output
        os.environ["MEGA_DEBUG"] = "2"
    else:
        lvl = logging.INFO

    if args.all:
        args.test1 = args.test2 = args.test3 = args.test4 = args.test5 = args.test6 = args.test7 = args.test8 = True
    if args.basic:
        args.test1 = args.test2 = args.test3 = args.test4 = True

    # logging stuff, output to stdout
    logging.StreamHandler(sys.stdout)
    logging.basicConfig(format='[%(asctime)s] %(message)s', datefmt='%Y-%m-%d %H:%M:%S', level=lvl)

    with SyncTestMegaSyncApp(args.work_dir, args.sync_dir, args.nodelete, args.large) as app:
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

        testRunner = xmlrunner.XMLTestRunner(output='test-reports')
        testRunner.run(suite)
