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
from sync_test_base import get_random_str
import shutil
import logging
import datetime


class SyncTestApp(object):
    """
    test application base class
    """
    def __init__(self, local_mount_in, local_mount_out, work_folder, delete_tmp_files=True, use_large_files=True):
        """
        work_dir: a temporary folder to place generated files
        remote_folder: a remote folder to sync
        """

        self.start_time = time.time()

        random.seed(time.time())

        self.local_mount_in = local_mount_in
        self.local_mount_out = local_mount_out

        self.rnd_folder = get_random_str()
        self.local_folder_in = os.path.join(self.local_mount_in, self.rnd_folder)
        self.local_folder_out = os.path.join(self.local_mount_out, self.rnd_folder)
        self.work_folder = os.path.join(work_folder, self.rnd_folder)

        self.nr_retries = 200
        self.delete_tmp_files = delete_tmp_files
        self.use_large_files = use_large_files
    
    def change_folders(self):
        """
        cleans directories and call finish
        """
        time.sleep(0.2) # to prevent from sync algorithm interpreting we are renaming
        
        if self.delete_tmp_files:
            try:
                shutil.rmtree(self.local_folder_in)
            except OSError:
                pass
        time.sleep(1.2) # to prevent from sync algorithm interpreting we are renaming
        self.rnd_folder = get_random_str()
        self.local_folder_in = os.path.join(self.local_mount_in, self.rnd_folder)
        self.local_folder_out = os.path.join(self.local_mount_out, self.rnd_folder)
        self.work_folder = os.path.join(self.work_folder, self.rnd_folder) 
        
        self.prepare_folders();          

    def __enter__(self):
        # call subclass function
        res = self.start()
        if not res:
            self.stop()
            raise Exception('Failed to start app!')

        res = self.prepare_folders()
        if not res:
            self.stop()
            raise Exception('Failed to start app!')

        return self

    def __exit__(self, exc_type, exc_value, traceback):
        # remove tmp folders
        if self.delete_tmp_files:
            try:
                logging.debug("Deleting %s" % self.local_folder_in)
                shutil.rmtree(self.local_folder_in)
            except OSError:
                pass
            try:
                logging.debug("Deleting %s" % self.local_folder_out)
                shutil.rmtree(self.local_folder_out)
            except OSError:
                pass
            try:
                logging.debug("Deleting %s" % self.work_folder)
                shutil.rmtree(self.work_folder)
            except OSError:
                pass

        # terminate apps
        self.stop()
        logging.info("Execution time: %s" % str(datetime.timedelta(seconds=time.time()-self.start_time)))

    @staticmethod
    def touch(path):
        """
        create an empty file
        update utime
        """
        with open(path, 'a'):
            os.utime(path, None)

    def prepare_folders(self):
        """
        prepare upsync, downsync and work directories
        """
        # create "in" folder
        logging.info("IN folder: %s" % self.local_folder_in)
        try:
            os.makedirs(self.local_folder_in)
        except OSError, e:
            logging.error("Failed to create directory: %s (%s)" % (self.local_folder_in, e))
            return False

        logging.info("OUT folder: %s" % self.local_folder_out)

        self.sync()

        # temporary workaround
        #tmp_fix_file = os.path.join(self.local_mount_out, "tmp_fix")

        success = False
        # try to access the dir
        for r in range(0, self.nr_retries):
            self.attempt=r
            try:
                if os.path.isdir(self.local_folder_out):
                    success = True
                    break
                else:
                    # wait for a dir
                    logging.debug("Directory %s not found! Retrying [%d/%d] .." % (self.local_folder_out, r + 1, self.nr_retries))
                    #self.touch(tmp_fix_file)
                    self.sync()
            except OSError:
                # wait for a dir
                logging.debug("Directory %s not found! Retrying [%d/%d] .." % (self.local_folder_out, r + 1, self.nr_retries))
                #self.touch(tmp_fix_file)
                self.sync()
        if success is False:
            logging.error("Failed to access directory: %s" % self.local_folder_out)
            return False

        # create work folder
        logging.debug("Work folder: %s" % self.work_folder)
        try:
            os.makedirs(self.work_folder)
        except OSError, e:
            logging.error("Failed to create directory: %s (%s)" % (self.work_folder, e))
            return False

        return True

    def stop(self):
        """
        cleans directories and call finish
        """
        if self.delete_tmp_files:
            try:
                shutil.rmtree(self.local_folder_in)
            except OSError:
                pass
        self.sync()
        self.finish()

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

    def is_alive(self):
        """
        return True if application instance is running
        """
        raise NotImplementedError("Not Implemented !")
