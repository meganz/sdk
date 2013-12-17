"""
 Application for testing syncing algorithm

 (c) 2013 by Mega Limited, Wellsford, New Zealand

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
from threading import Thread
import random
import time
import string
import shutil
import logging
import collections
import signal
import hashlib
import logging
import subprocess

class SyncTestApp ():
    def __init__(self, local_mount_in, local_mount_out, remote_folder, work_folder):
        """
        local_mount_in: local upsync folder
        local_mount_out: local downsync folder
        remote_folder: a remote sync folder name
        work_folder: a temporary out of sync folder
        """
        random.seed (time.time())

        self.remote_folder = remote_folder

        self.local_mount_in = local_mount_in
        self.local_mount_out = local_mount_out

        self.rnd_folder = self.get_random_str ()
        self.local_folder_in = os.path.join (self.local_mount_in, self.rnd_folder)
        self.local_folder_out = os.path.join (self.local_mount_out, self.rnd_folder)
        self.work_folder = os.path.join (work_folder, self.rnd_folder)

        self.nr_retries = 1
        self.nr_files = 20
        self.sleep_sec = 10
        self.l_files = []
        self.seed = "0987654321"
        self.words = open("lorem.txt", "r").read().replace("\n", '').split()

        self.logger = logging.getLogger(__name__)
        ch = logging.StreamHandler(sys.stdout)
#        ch.setLevel(logging.DEBUG)
        formatter = logging.Formatter('[%(asctime)s] %(message)s')
        ch.setFormatter (formatter)
        self.logger.addHandler(ch)
        self.logger.setLevel (logging.DEBUG)

    def get_random_str (self, size=10, chars = string.ascii_uppercase + string.digits):
        """
        return a random string
        size: size of an output stirng
        chars: characters to use
        """
        return ''.join (random.choice (chars) for x in range (size))

    def fdata (self):
        """
        return random data
        """
        a = collections.deque (self.words)
        b = collections.deque (self.seed)
        while True:
            yield ' '.join (list (a)[0:1024])
            a.rotate (int (b[0]))
            b.rotate (1)

    def test_empty (self, folder_name):
        """
        return True if folder is empty
        """
        return not os.listdir(folder_name)

    def create_file (self, fname, flen):
        """
        create a file of a length flen and fill with a random data
        """
        g = self.fdata ()
        fout = open (fname, 'w')
        while os.path.getsize (fname) < flen:
            fout.write (g.next())
        fout.close ()

    def md5_for_file (self, fname, block_size=2**20):
        """
        calculates md5 of a file
        """
        fout = open (fname, 'r')
        md5 = hashlib.md5()
        while True:
            data = fout.read(block_size)
            if not data:
                break
            md5.update(data)
        fout.close ()
        return md5.hexdigest()

    def test_create (self):
        """
        create files in "in" instance and check files presence in "out" instance
        """
        # generate and put files into "in" folder
        for i in range (self.nr_files):
            strlen = random.randint (0, 20)
            fname = self.get_random_str (size=strlen) + str (i)
            ffname = self.local_folder_in + "/" + fname
            # up to 10k
            flen = random.randint (1, 1024 * 10)
            try:
                self.create_file (ffname, flen)
            except Exception, e:
                self.logger.error("Failed to create file: %s", ffname)
                return False
            md5_str = self.md5_for_file (ffname)
            self.l_files.append ({"name":fname, "len":flen, "md5":md5_str})
            self.logger.debug ("File created: %s [%s, %db]", ffname, md5_str, flen)

        # give some time to sync files to remote folder
        self.logger.debug ("Sleeping ..")
        time.sleep (self.sleep_sec)

        # check files
        for f in self.l_files:
            ffname = self.local_folder_out + "/" + f["name"]
            ffname_in = self.local_folder_in + "/" + f["name"]
            success = False

            self.logger.debug ("Checking %s and %s", ffname_in, ffname)
            # try to access the file
            for r in range (0, self.nr_retries):
                try:
                    with open(ffname) as fil: pass
                    success = True
                    break;
                except:
                    # wait for a file
                    self.logger.debug ("Retrying [%d/%d] ..", r + 1, self.nr_retries)
                    time.sleep(5)
            if success == False:
                self.logger.error("Failed to CHECK file: %s", ffname)
                return False
            # get md5 of synced file
            md5_str = self.md5_for_file (ffname)
            if md5_str != f["md5"]:
                self.logger.error("MD5 sums don't match for file: %s", ffname)
                return False
        return True

    def test_remove (self):
        """
        remove files in "in" instance and check files absence in "out" instance
        """

        for f in self.l_files:
            ffname = self.local_folder_in + "/" + f["name"]
            try:
                pass
                os.remove (ffname)
            except:
                self.logger.error("Failed to delete file: %s", ffname)
                return False

        # give some time to sync files to remote folder
        self.logger.debug ("Sleeping ..")
        time.sleep (10)

        success = False
        for f in self.l_files:
            ffname = self.local_folder_out + "/" + f["name"]
            for r in range (0, self.nr_retries):
                try:
                    # file must be deleted
                    with open(ffname) as fil: pass
                    self.logger.debug ("Retrying [%d/%d] ..", r + 1, self.nr_retries)
                    time.sleep (2)
                except:
                    success = True
                    break;
            if success == False:
                self.logger.error("Failed to delete file: %s", ffname)
                return False
        return True

# virtual functions
    def start (self):
        raise NotImplementedError("Not Implemented !")
    def finsh (self, res):
        raise NotImplementedError("Not Implemented !")

    def prepare_folders (self):
        # create "in" folder
        self.logger.info ("IN folder: %s", self.local_folder_in)
        try:
            os.makedirs (self.local_folder_in);
        except Exception, e:
            self.logger.error("Failed to create directory: %s", self.local_folder_in)
            return False

        # create "out" folder
        self.logger.info ("OUT folder: %s", self.local_folder_out)
        try:
            os.makedirs (self.local_folder_out);
        except Exception, e:
            self.logger.error("Failed to create directory: %s", self.local_folder_out)
            return False

        # create work folder
        self.logger.info ("Work folder: %s", self.work_folder)
        try:
            os.makedirs (self.work_folder);
        except Exception, e:
            self.logger.error("Failed to create directory: %s", self.work_folder)
            return False
        return True

    def run (self):
        """
        prepare and run tests
        """
        self.logger.info ("Starting ..")

        # call subclass function
        res = self.start ()
        if not res:
            self.logger.info ("Tests: [FAILED]")
            self.cleanup (False)
            return

        res = self.prepare_folders ()
        if not res:
            self.logger.info ("Tests: [FAILED]")
            self.cleanup (False)
            return

        # wait for a bit
        self.logger.debug ("Sleeping ..")
        time.sleep (2)

        #
        # run tests
        #
        self.logger.info ("Checking if remote folders are empty ...")
        # make sure remote folders are empty
        if self.test_empty (self.local_folder_in) and self.test_empty (self.local_folder_out):
            self.logger.info ("Checking if remote folders are empty: [SUCCESS]")
        else:
            self.logger.info ("Checking if remote folders are empty: [FAILED]")
            self.cleanup (False)
            return

        self.logger.info ("Testing files create ...")
        res = self.test_create ()
        if res == True:
            self.logger.info ("Testing files create: [SUCCESS]")
        else:
            self.logger.info ("Testing files create: [FAILED]")
            self.cleanup (False)
            return

        # wait for a bit
        self.logger.debug ("Sleeping ..")
        time.sleep (2)

        self.logger.info ( "Testing files remove ...")
        res = self.test_remove ()
        if res == True:
            self.logger.info ("Testing files remove: [SUCCESS]")
        else:
            self.logger.info ("Testing files remove: [FAILED]")
            self.cleanup (False)
            return

        # wait for a bit
        self.logger.debug ("Sleeping ..")
        time.sleep (2)

        self.cleanup (True)

    def cleanup(self, res):
        """
        """
        self.finish (res)

        # remove tmp folders if no errors
        if res == True:
            try:
                shutil.rmtree (self.local_folder_in)
            except:
                None
            try:
                shutil.rmtree (self.local_folder_out)
            except:
                None
            try:
                shutil.rmtree (self.work_folder)
            except:
                None

            self.logger.info ("Done.")
        else:
            self.logger.info ("Aborted.")

