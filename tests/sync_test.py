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

class App ():
    def __init__(self, work_folder, remote_folder):
        """
        work_folder: a temporary folder to place generated files
        remote_folder: a remote folder to sync
        """
        self.work_folder = work_folder
        self.remote_folder = remote_folder
        self.megasync_pid_in = None
        self.megasync_pid_out = None
        self.local_folder_in = ""
        self.local_folder_out = ""
        self.local_mount_in = ""
        self.local_mount_out = ""
        self.nr_retries = 10
        self.l_files = []
        random.seed (time.time())
        self.seed = "sync_test.py"
        self.words = open("lorem.txt", "r").read().replace("\n", '').split()

        self.logger = logging.getLogger(__name__)
        ch = logging.StreamHandler(sys.stdout)
        ch.setLevel(logging.DEBUG)
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

    def start_megasync (self, local_folder):
        """
        fork and launch "megasync" application
        local_folder: local folder to sync
        """
        try:
            pid = os.fork ()
        except OSError, e:
            self.logger.error("Failed to start megasync")
            return 0

        # launch megasync
        if pid == 0:
            base_path = os.path.join(os.path.dirname(__file__), '..')
            bin_path = os.path.join(base_path, "examples")
            args = [os.path.join(bin_path, "megasync"), local_folder, self.remote_folder]

            try:
                os.execv(args[0], args)
            except OSError, e:
                self.logger.error( "Failed to start megasync")
                return 0
        else:
            return pid


    def test_create (self):
        """
        create files in "in" instance and check files presence in "out" instance
        """
        # generate and put files into "in" folder
        for i in range (10):
            fname = self.get_random_str () + "_" + str (i)
            ffname = self.local_folder_in + "/" + fname
            flen = random.randint (1, 1024 * 4)
            try:
                self.create_file (ffname, flen)
            except:
                self.logger.error("Failed to create file: %s", ffname)
                return False
            md5_str = self.md5_for_file (ffname)
            self.l_files.append ({"name":fname, "len":flen, "md5":md5_str})
            self.logger.debug ("File created: %s", ffname)

        # give some time to sync files to remote folder
        self.logger.debug ("Sleeping ..")
        time.sleep (10)

        # check files
        for f in self.l_files:
            ffname = self.local_folder_out + "/" + f["name"]
            ffname_in = self.local_folder_in + "/" + f["name"]
            success = False

            self.logger.debug ("Checking %s and %s", ffname_in, ffname)
            # try to access the file
            for r in range (0, self.nr_retries):
                try:
                    with open(ffname) as f: pass
                    success = True
                    break;
                except:
                    # wait for a file
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
                shutil.rmtree (ffname)
            except:
                self.logger.error("Failed to delete file: %s", ffname)
                None

        # give some time to sync files to remote folder
        time.sleep (10)

        success = False
        for f in self.l_files:
            ffname = self.local_folder_out + "/" + f["name"]
            for r in range (0, self.nr_retries):
                try:
                    # file must be deleted
                    with open(ffname) as f: pass
                    time.sleep (2)
                except:
                    success = True
                    break;
            if success == False:
                self.logger.error("Failed to delete file: %s", ffname)
                return False
        return True

    def run (self):
        """
        prepare and run tests
        """
        self.logger.info ("Starting ..")
        # create "in" folder
        self.local_mount_in = self.work_folder + "/in"
        self.local_folder_in = self.local_mount_in + "/" + self.get_random_str ()

        self.logger.info ("IN folder: %s", self.local_folder_in)
        try:
            os.makedirs (self.local_folder_in);
        except Exception, e:
            self.logger.error("Failed to create directory: %s", self.local_folder_in)
            return

        # create "out" folder
        self.local_mount_out = self.work_folder + "/out"
        self.local_folder_out = self.local_mount_out + "/" + self.get_random_str ()
        self.logger.info ("OUT folder: %s", self.local_folder_out)
        try:
            os.makedirs (self.local_folder_out);
        except Exception, e:
            self.logger.error("Failed to create directory: %s", self.local_folder_out)
            return

        # start "in" instance
        self.megasync_pid_in = self.start_megasync (self.local_mount_in)
        # start "out" instance
        self.megasync_pid_out = self.start_megasync (self.local_mount_out)

        # check both instances
        if self.megasync_pid_in == 0 or self.megasync_pid_out == 0:
            self.logger.error("Failed to start megasync instance.")
            return

        #
        # run tests
        #
        self.logger.info ("Testing files create ...")
        res = self.test_create ()
        if res == True:
            self.logger.info ("Testing files create: [SUCCESS]")
        else:
            self.logger.info ("Testing files create: [FAILED]")
            self.cleanup (False)
            return

        # wait for a bit
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
        time.sleep (2)

        self.cleanup (True)


    def cleanup(self, res):
        """
        kill megasync instances, remove temp folders
        """
        # kill instances
        try:
            os.kill (self.megasync_pid_in, signal.SIGINT)
        except Exception, e:
            self.logger.error ("Failed to kill megasync processes !")
        try:
            os.kill (self.megasync_pid_out, signal.SIGINT)
        except Exception, e:
            self.logger.error ("Failed to kill megasync processes !")

        # remove tmp folders if no errors
        if res == True:
            try:
                pass
                shutil.rmtree (self.local_folder_in)
            except:
                None
            try:
                pass
                shutil.rmtree (self.local_folder_out)
            except:
                None
            self.logger.info ("Done.")
        else:
            self.logger.info ("Aborted.")


if __name__ == "__main__":
    if len (sys.argv) < 2:
        print "Please run as:  python " + sys.argv[0] + " [working directory] [remote folder to sync]"
        print "set MEGA_EMAIL and MEGA_PWD environment variables"
        print "set MEGA_DEBUG environment variable to see JSON input / output data"
        sys.exit (1)

    app = App (sys.argv[1], sys.argv[2])
    app.run ()
