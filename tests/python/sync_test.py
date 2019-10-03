"""
 Base class for testing syncing algorithm

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

# TODO tests:
# * "pause" sync
# * lock directory
# * large (> 4Gb) files
# * > 10000 folders to synchronize

from sync_test_base import SyncTestBase
from sync_test_base import get_random_str
from sync_test_base import generate_unicode_name
import random
import os
import logging
import time
import math

class SyncTest(SyncTestBase):
    """
    Class with MEGA SDK test methods
    """

# tests
    def test_create_delete_files(self):
        """
        create files with different size,
        compare files on both folders,
        remove files, check that files removed from the second folder
        """
        logging.info("Launching test_create_delete_files test")
        self.assertTrue(self.app.is_alive(), "Test application is not running")
        self.app.change_folders();

        # make sure remote folders are empty
        self.assertTrue(self.dirs_check_empty(), "Checking if remote folders are empty")
        self.assertTrue(self.app.is_alive(), "Test application is not running")

        # create files
        l_files = self.files_create()
        self.assertIsNotNone(l_files, "Creating files")
        self.assertTrue(self.app.is_alive(), "Test application is not running")

        self.app.sync()

        # comparing
        self.assertTrue(self.files_check(l_files), "Comparing files")
        self.assertTrue(self.app.is_alive(), "Test application is not running")

        # remove files
        self.assertTrue(self.files_remove(l_files), "Removing files")
        self.assertTrue(self.app.is_alive(), "Test application is not running")

        # make sure remote folders are empty
        self.assertTrue(self.dirs_check_empty(), "Checking if remote folders are empty")
        self.assertTrue(self.app.is_alive(), "Test application is not running")

        return True

    def test_create_rename_delete_files(self):
        """
        create files with different size,
        compare files on both folders,
        rename files
        """
        logging.info("Launching test_create_rename_delete_files test")
        self.assertTrue(self.app.is_alive(), "Test application is not running")
        self.app.change_folders();        

        # make sure remote folders are empty
        self.assertTrue(self.dirs_check_empty(), "Checking if remote folders are empty")
        self.assertTrue(self.app.is_alive(), "Test application is not running")

        # create files
        l_files = self.files_create()
        self.assertIsNotNone(l_files, "Creating files")
        self.assertTrue(self.app.is_alive(), "Test application is not running")

        self.app.sync()

        # comparing
        self.assertTrue(self.files_check(l_files), "Comparing files")
        self.assertTrue(self.app.is_alive(), "Test application is not running")

        # renaming
        self.assertTrue(self.files_rename(l_files), "Renaming files")
        self.assertTrue(self.app.is_alive(), "Test application is not running")

        self.app.sync()

        # comparing
        self.assertTrue(self.files_check(l_files), "Comparing files")
        self.assertTrue(self.app.is_alive(), "Test application is not running")

        # remove files
        self.assertTrue(self.files_remove(l_files), "Removing files")
        self.assertTrue(self.app.is_alive(), "Test application is not running")

        # make sure remote folders are empty
        self.assertTrue(self.dirs_check_empty(), "Checking if remote folders are empty")
        self.assertTrue(self.app.is_alive(), "Test application is not running")

        return True
        
    def test_create_move_delete_files(self):
        """
        create files with different size,
        move and delete files
        """
        logging.info("Launching test_create_move_delete_files test")
        self.assertTrue(self.app.is_alive(), "Test application is not running")
        self.app.change_folders();        

        # make sure remote folders are empty
        self.assertTrue(self.dirs_check_empty(), "Checking if remote folders are empty")
        self.assertTrue(self.app.is_alive(), "Test application is not running")

        # create files
        l_files = self.files_create()
        self.assertIsNotNone(l_files, "Creating files")
        self.assertTrue(self.app.is_alive(), "Test application is not running")

        self.app.sync()

        # comparing
        self.assertTrue(self.files_check(l_files), "Comparing files")
        self.assertTrue(self.app.is_alive(), "Test application is not running")

        # move & delete
        self.assertTrue(self.files_moveanddelete(l_files,"subfolder"), "Move&Delete files")
        #~ self.assertTrue(self.files_moveanddelete(l_files,"."), "Move&Delete files") #this is expected to fail as of 20171019        
        self.assertTrue(self.app.is_alive(), "Test application is not running")

        self.app.sync()

        # make sure remote folders are empty
        self.assertTrue(self.dirs_check_empty(), "Checking if remote folders are empty")
        self.assertTrue(self.app.is_alive(), "Test application is not running")

        return True
        
    def test_mimic_update_with_backup_files(self):
        """
        create files with different size,
        mimic_update_with_backup
        """
        logging.info("Launching test_mimic_update_with_backup_files test")
        self.assertTrue(self.app.is_alive(), "Test application is not running")
        self.app.change_folders();        

        # make sure remote folders are empty
        self.assertTrue(self.dirs_check_empty(), "Checking if remote folders are empty")
        self.assertTrue(self.app.is_alive(), "Test application is not running")

        # create files
        l_files = self.files_create()
        self.assertIsNotNone(l_files, "Creating files")
        self.assertTrue(self.app.is_alive(), "Test application is not running")

        self.app.sync()

        # comparing
        self.assertTrue(self.files_check(l_files), "Comparing files")
        self.assertTrue(self.app.is_alive(), "Test application is not running")

        # mimic update with backup
        self.assertTrue(self.files_mimic_update_with_backup(l_files), "Mimic update with backup files")
        self.assertTrue(self.app.is_alive(), "Test application is not running")

        self.app.sync()

        # remove files
        self.assertTrue(self.files_remove(l_files), "Removing files")
        self.assertTrue(self.app.is_alive(), "Test application is not running")

        # make sure remote folders are empty
        self.assertTrue(self.dirs_check_empty(), "Checking if remote folders are empty")
        self.assertTrue(self.app.is_alive(), "Test application is not running")

        return True

    def test_create_delete_dirs(self):
        """
        create directories with different amount of files,
        compare directories on both sync folders,
        remove directories, check that directories removed from the second folder
        """
        logging.info("Launching test_create_delete_dirs test")
        self.assertTrue(self.app.is_alive(), "Test application is not running")
        self.app.change_folders();
        

        # make sure remote folders are empty
        self.assertTrue(self.dirs_check_empty(), "Checking if remote folders are empty")
        self.assertTrue(self.app.is_alive(), "Test application is not running")

        # create dirs
        l_dirs = self.dirs_create()
        self.assertIsNotNone(l_dirs, "Creating directories")
        self.assertTrue(self.app.is_alive(), "Test application is not running")

        self.app.sync()

        # comparing
        self.assertTrue(self.dirs_check(l_dirs), "Comparing directories")
        self.assertTrue(self.app.is_alive(), "Test application is not running")

        # remove files
        self.assertTrue(self.dirs_remove(l_dirs), "Removing directories")
        self.assertTrue(self.app.is_alive(), "Test application is not running")

        # make sure remote folders are empty
        self.assertTrue(self.dirs_check_empty(), "Checking if remote folders are empty")
        self.assertTrue(self.app.is_alive(), "Test application is not running")

        return True

    def test_create_rename_delete_dirs(self):
        """
        create directories with different amount of files,
        compare directories on both sync folders,
        rename directories
        compare directories on both sync folders,
        remove directories, check that directories removed from the second folder
        """
        logging.info("Launching test_create_rename_delete_dirs test")
        self.assertTrue(self.app.is_alive(), "Test application is not running")

        self.app.change_folders();

        # make sure remote folders are empty
        self.assertTrue(self.dirs_check_empty(), "Checking if remote folders are empty")
        self.assertTrue(self.app.is_alive(), "Test application is not running")

        # create dirs
        l_dirs = self.dirs_create()
        self.assertIsNotNone(l_dirs, "Creating directories")
        self.assertTrue(self.app.is_alive(), "Test application is not running")

        self.app.sync()

        # comparing
        self.assertTrue(self.dirs_check(l_dirs), "Comparing directories")
        self.assertTrue(self.app.is_alive(), "Test application is not running")

        # rename dirs
        self.assertTrue(self.dirs_rename(l_dirs), "Rename directories")
        self.assertTrue(self.app.is_alive(), "Test application is not running")

        self.app.sync()

        # comparing
        self.assertTrue(self.dirs_check(l_dirs), "Comparing directories")
        self.assertTrue(self.app.is_alive(), "Test application is not running")

        # remove files
        self.assertTrue(self.dirs_remove(l_dirs), "Removing directories")
        self.assertTrue(self.app.is_alive(), "Test application is not running")

        # make sure remote folders are empty
        self.assertTrue(self.dirs_check_empty(), "Checking if remote folders are empty")
        self.assertTrue(self.app.is_alive(), "Test application is not running")

        return True

    def test_sync_files_write(self):
        """
        write data to a file located in both sync folders
        check for the result, expected result: both files contains the same content
        """

        logging.info("Launching test_sync_files_write test")
        self.assertTrue(self.app.is_alive(), "Test application is not running")
        self.app.change_folders();

        self.assertTrue(self.dirs_check_empty(), "Checking if remote folders are empty")
        self.assertTrue(self.app.is_alive(), "Test application is not running")

        for _ in range(0, self.nr_files):
            self.assertTrue(self.app.is_alive(), "Test application is not running")
            strlen = random.randint(10, 20)
            fname = get_random_str(size=strlen)

            fname_in = os.path.join(self.app.local_folder_in, fname)
            fname_out = os.path.join(self.app.local_folder_out, fname)

            logging.debug("Writing to both files: %s and %s" % (fname_in, fname_out))

            with open(fname_in, 'a'):
                os.utime(fname_in, None)
            with open(fname_out, 'a'):
                os.utime(fname_out, None)

            #self.app.sync()

            for _ in range(self.nr_changes):
                with open(fname_in, 'a') as f_in:
                    f_in.write(get_random_str(100))

                with open(fname_out, 'a') as f_out:
                    f_out.write(get_random_str(100))

                for r in range(self.app.nr_retries):
                    self.app.attempt=r
                    md5_in = "INPUT FILE NOT READABLE";
                    md5_out = "OUTPUT FILE NOT READABLE";

                    try:
                        md5_in = self.md5_for_file(fname_in)
                        md5_out = self.md5_for_file(fname_out)
                    except IOError:
                        pass;

                    if md5_in == md5_out:
                        break
                    self.app.sync()

                logging.debug("File %s md5: %s" % (fname_in, md5_in))
                logging.debug("File %s md5: %s" % (fname_out, md5_out))

                self.assertEqual(md5_in, md5_out, "Files do not match")


    def test_local_operations(self):
        """
        write data to a file located in both sync folders
        check for the result, expected result: both files contains the same content
        """
        logging.info("Launching test_local_operations test")
        self.assertTrue(self.app.is_alive(), "Test application is not running")
        self.app.change_folders();
        
        l_tree = self.local_tree_create("", self.nr_dirs)
        self.assertIsNotNone(l_tree, "Failed to create directory tree!")
        self.assertTrue(self.app.is_alive(), "Test application is not running")
        self.app.sync()
        self.assertTrue(self.local_tree_compare(l_tree), "Failed to compare directory trees!")
        self.assertTrue(self.app.is_alive(), "Test application is not running")
        self.assertTrue(self.local_tree_create_and_move(l_tree), "Failed to create a new sub folder and move an existing directory into it!")
        self.assertTrue(self.app.is_alive(), "Test application is not running")
        self.assertTrue(self.local_tree_multiple_renames(l_tree), "Failed to rename folder multiple times and then rename back to the original name!")
        self.assertTrue(self.app.is_alive(), "Test application is not running")

    def test_update_mtime(self):
        """
        update mtime of a file in both local folders
        """
        logging.info("Launching test_update_mtime test")
        self.assertTrue(self.app.is_alive(), "Test application is not running")
        
        self.app.change_folders();

        in_file = os.path.join(self.app.local_folder_in, "mtime_test")
        out_file = os.path.join(self.app.local_folder_out, "mtime_test")

        for _ in range(self.nr_time_changes):
            logging.debug("Touching: %s" % in_file)
            now = math.floor(time.time()) #floor to get seconds
            with open(in_file, 'a'):
                os.utime(in_file, (now, now))

      #      with open(out_file, 'a'):
      #          os.utime(in_file, (now, now))
            
            atime=0
            mtime=0
            for r in range(self.app.nr_retries):
                self.app.attempt=r
                try:
                    mtime = os.path.getmtime(out_file)
                except OSError:
                    pass

                try:
                    atime = os.path.getatime(out_file)
                except OSError:
                    pass
            
                logging.debug("Comparing time: %s. atime: %d = %d, mtime: %d = %d" % (out_file, now, atime, now, mtime))
                
                if (mtime==now): #all good
                    break;
                self.app.sync()
                logging.debug("Comparing time for %s failed! Retrying [%d/%d] .." % (out_file, r + 1, self.nr_retries))

            
            #self.assertEqual(atime, now, "atime values are different")
            self.assertEqual(mtime, now, "mtime values are different")
            
            self.assertTrue(self.app.is_alive(), "Test application is not running")

    def test_create_rename_delete_unicode_files_dirs(self):
        """
        create directories with different amount of files,
        using Unicode encoding for files / directories names,
        compare directories on both sync folders,
        rename directories
        compare directories on both sync folders,
        remove directories, check that directories removed from the second folder
        """
        logging.info("Launching test_create_rename_delete_unicode_files_dirs test")
        self.assertTrue(self.app.is_alive(), "Test application is not running")
        
        self.app.change_folders();
        
        # make sure remote folders are empty
        self.assertTrue(self.dirs_check_empty(), "Checking if remote folders are empty")
        self.assertTrue(self.app.is_alive(), "Test application is not running")

        # create files
        l_files = self.files_create(generate_unicode_name)
        self.assertIsNotNone(l_files, "Creating files")
        self.assertTrue(self.app.is_alive(), "Test application is not running")

        self.app.sync()

        # comparing
        self.assertTrue(self.files_check(l_files), "Comparing files")
        self.assertTrue(self.app.is_alive(), "Test application is not running")

        # renaming
        self.assertTrue(self.files_rename(l_files, generate_unicode_name), "Renaming files")
        self.assertTrue(self.app.is_alive(), "Test application is not running")

        self.app.sync()

        # comparing
        self.assertTrue(self.files_check(l_files), "Comparing files")
        self.assertTrue(self.app.is_alive(), "Test application is not running")

        # remove files
        self.assertTrue(self.files_remove(l_files), "Removing files")
        self.assertTrue(self.app.is_alive(), "Test application is not running")

        # make sure remote folders are empty
        self.assertTrue(self.dirs_check_empty(), "Checking if remote folders are empty")
        self.assertTrue(self.app.is_alive(), "Test application is not running")

        # make sure remote folders are empty #TODO: why is this twice?
        self.assertTrue(self.dirs_check_empty(), "Checking if remote folders are empty")
        self.assertTrue(self.app.is_alive(), "Test application is not running")

        # create dirs
        l_dirs = self.dirs_create(generate_unicode_name)
        self.assertIsNotNone(l_dirs, "Creating directories")
        self.assertTrue(self.app.is_alive(), "Test application is not running")

        self.app.sync()

        # comparing
        self.assertTrue(self.dirs_check(l_dirs), "Comparing directories")
        self.assertTrue(self.app.is_alive(), "Test application is not running")

        # rename dirs
        self.assertTrue(self.dirs_rename(l_dirs, generate_unicode_name), "Rename directories")
        self.assertTrue(self.app.is_alive(), "Test application is not running")

        self.app.sync()

        # comparing
        self.assertTrue(self.dirs_check(l_dirs), "Comparing directories")
        self.assertTrue(self.app.is_alive(), "Test application is not running")

        # remove files
        self.assertTrue(self.dirs_remove(l_dirs), "Removing directories")
        self.assertTrue(self.app.is_alive(), "Test application is not running")

        # make sure remote folders are empty
        self.assertTrue(self.dirs_check_empty(), "Checking if remote folders are empty")
        self.assertTrue(self.app.is_alive(), "Test application is not running")

        return True
