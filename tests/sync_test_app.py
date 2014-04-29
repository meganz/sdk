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

import os
import time
import random
from sync_test_base import SyncTestBase
import shutil

class SyncTestApp(object):
    """
    test application base class
    """
    def __init__(self, local_mount_in, local_mount_out, work_folder, delete_tmp_files=True, use_large_files=True):
        """
        work_dir: a temporary folder to place generated files
        remote_folder: a remote folder to sync
        """

        random.seed(time.time())

        self.local_mount_in = local_mount_in
        self.local_mount_out = local_mount_out

        self.rnd_folder = SyncTestBase.get_random_str()
        self.local_folder_in = os.path.join(self.local_mount_in, self.rnd_folder)
        self.local_folder_out = os.path.join(self.local_mount_out, self.rnd_folder)
        self.work_folder = os.path.join(work_folder, self.rnd_folder)

        self.nr_retries = 100
        self.delete_tmp_files = delete_tmp_files
        self.use_large_files = use_large_files

    def __enter__(self):
        # call subclass function
        res = self.start()
        if not res:
            self.finish()
            raise Exception('Failed to start app!')

        res = self.prepare_folders()
        if not res:
            self.finish()
            raise Exception('Failed to start app!')

        return self

    def __exit__(self, exc_type, exc_value, traceback):
        # remove tmp folders
        if self.delete_tmp_files:
            try:
                shutil.rmtree(self.local_folder_in)
            except OSError:
                pass
            try:
                shutil.rmtree(self.local_folder_out)
            except OSError:
                pass
            try:
                shutil.rmtree(self.work_folder)
            except OSError:
                pass

        # terminate apps
        self.finish()

    def prepare_folders(self):
        """
        prepare upsync, downsync and work directories
        """
        # create "in" folder
        print "IN folder: %s" % self.local_folder_in
        try:
            os.makedirs(self.local_folder_in)
        except OSError:
            print "Failed to create directory: %s" % self.local_folder_in
            return False

        print "OUT folder: %s" % self.local_folder_out

        self.sync()

        success = False
        # try to access the dir
        for r in range(0, self.nr_retries):
            try:
                if os.path.isdir(self.local_folder_out):
                    success = True
                    break
                else:
                    # wait for a dir
                    print "Directory %s not found! Retrying [%d/%d] .." % (self.local_folder_out, r + 1, self.nr_retries)
                    self.sync()
            except OSError:
                # wait for a dir
                print "Directory %s not found! Retrying [%d/%d] .." % (self.local_folder_out, r + 1, self.nr_retries)
                self.sync()
        if success == False:
            print "Failed to access directory: %s" % self.local_folder_out
            return False

        # create work folder
        print "Work folder: %s" % self.work_folder
        try:
            os.makedirs(self.work_folder)
        except OSError:
            print "Failed to create directory: %s" % self.work_folder
            return False

        return True

# virtual methods
    def start(self):
        """
        start application
        """
        raise NotImplementedError("Not Implemented !")
    def finish(self):
        """
        stop application
        """
        raise NotImplementedError("Not Implemented !")
    def sync(self):
        """
        wait for full synchronization
        """
        raise NotImplementedError("Not Implemented !")
    def pause(self):
        """
        pause application
        """
        raise NotImplementedError("Not Implemented !")
    def unpause(self):
        """
        unpause application
        """
        raise NotImplementedError("Not Implemented !")
