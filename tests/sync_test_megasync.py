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
import platform
from sync_test_base import SyncTestBase
from sync_test import SyncTest
from sync_test_app import SyncTestApp
import unittest
import xmlrunner

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

        #XXX: currently on Windows megasync.exe is located in the sources root dir.
        if platform.system() == 'Windows':
            bin_path = os.path.join(base_path, "")
        else:
            bin_path = os.path.join(base_path, "examples")

        args = [os.path.join(bin_path, "megasync"), local_folder, self.remote_folder]
        output_fname = os.path.join(self.work_dir, "megasync" + "_" + type_str + "_" + SyncTestBase.get_random_str() + ".log")
        output_log = open(output_fname, "w")

        print "Launching megasync: %s" % (" ".join(args))

        try:
            ch = subprocess.Popen(args, universal_newlines=True, stdout=output_log, stderr=subprocess.STDOUT, shell=False)
        except OSError:
            print "Failed to launch megasync process"
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
            print "Environment variables MEGA_EMAIL and MEGA_PWD are not set !"
            return False

        # start "in" instance
        self.megasync_ch_in = self.start_megasync(self.local_mount_in, "in")
        # pause
        time.sleep(5)
        # start "out" instance
        self.megasync_ch_out = self.start_megasync(self.local_mount_out, "out")
        # check both instances
        if self.megasync_ch_in == None or self.megasync_ch_out == None:
            return False

        return True

    def finish(self):
        """
        kill megasync instances, remove temp folders
        """
        # kill instances
        if self.megasync_ch_in:
            self.megasync_ch_in.terminate()

        if self.megasync_ch_out:
            self.megasync_ch_out.terminate()

    def pause(self):
        """
        pause application
        """

    def unpause(self):
        """
        unpause application
        """

if __name__ == "__main__":
    if len(sys.argv) < 3:
        print "Please run as:  python " + sys.argv[0] + " [work dir] [remote folder name]"
        sys.exit(1)

    with SyncTestMegaSyncApp(sys.argv[1], sys.argv[2], True, True) as app:
        suite = unittest.TestSuite()

        suite.addTest(SyncTest("test_create_delete_files", app))
        suite.addTest(SyncTest("test_create_rename_delete_files", app))
        suite.addTest(SyncTest("test_create_delete_dirs", app, ))
        suite.addTest(SyncTest("test_create_rename_delete_dirs", app))
        suite.addTest(SyncTest("test_sync_files_write", app))
        suite.addTest(SyncTest("test_local_operations", app))

        testRunner = xmlrunner.XMLTestRunner(output='test-reports')
        testRunner.run(suite)
