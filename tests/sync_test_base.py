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

import os
import random
import string
import shutil
import hashlib
import unittest

class SyncTestBase(unittest.TestCase):
    """
    Base class with MEGA SDK test helper methods
    """
    def __init__(self, methodName, app):
        """
        local_mount_in: local upsync folder
        local_mount_out: local downsync folder
        work_folder: a temporary out of sync folder
        """
        super(SyncTestBase, self).__init__(methodName)

        self.app = app

        self.nr_retries = 150
        self.nr_files = 10
        self.nr_dirs = 5
        self.local_obj_nr = 5
        self.force_syncing = False

    @staticmethod
    def get_random_str(size=10, chars=string.ascii_uppercase + string.digits):
        """
        return a random string
        size: size of an output stirng
        chars: characters to use
        """
        return ''.join(random.choice(chars) for x in range(size))

    def check_empty(self, folder_name):
        """
        return True if folder is empty
        """
        print "Checking if folder %s is empty" % folder_name

        # leave old files
        if not self.app.delete_tmp_files:
            return True

        for r in range(0, self.nr_retries):
            try:
                res = not os.listdir(folder_name)
            except OSError:
                print "Failed to list dir: %s" % folder_name
                return False

            if res:
                return True

            print "Directory %s is not empty! Retrying [%d/%d] .." % (folder_name, r + 1, self.nr_retries)

            try:
                shutil.rmtree(folder_name)
            except OSError:
                print "Failed to delete folder: %s" % folder_name
                return False

    @staticmethod
    def md5_for_file(fname, block_size=2**20):
        """
        calculates md5 of a file
        """
        fout = open(fname, 'r')
        md5 = hashlib.md5()
        while True:
            data = fout.read(block_size)
            if not data:
                break
            md5.update(data)
        fout.close()
        return md5.hexdigest()

    def file_create(self, fname, fsize):
        """
        create a file of a size fsize and fill with a random data
        """
        fout = open(fname, 'w')
        fout.write(self.get_random_str(fsize))
        fout.close()

    def files_create_size(self, first_symbol, maxsize, nr_files, dname, l_files):
        """
        create a list of files of a specific size
        """
        for i in range(nr_files):
            strlen = random.randint(0, 20)
            fname = first_symbol + self.get_random_str(size=strlen) + str(i)
            ffname = os.path.join(dname, fname)
            if maxsize == 0:
                fsize = 0
            else:
                fsize = random.randint(1, maxsize)

            try:
                self.file_create(ffname, fsize)
            except IOError:
                print "Failed to create file: %s" % ffname
                return False
            md5_str = self.md5_for_file(ffname)
            l_files.append({"name":fname, "size":fsize, "md5":md5_str, "name_orig":fname})
            print "File created: %s [%s, %db]" % (ffname, md5_str, fsize)
        return True

    def files_create(self):
        """
        create files in "in" instance and check files presence in "out" instance
        Return list of files
        """
        print "Creating files.."

        l_files = []

        # empty files
        res = self.files_create_size("e", 0, self.nr_files, self.app.local_folder_in, l_files)
        if not res:
            return None

        # small files < 1k
        res = self.files_create_size("s", 1024, self.nr_files, self.app.local_folder_in, l_files)
        if not res:
            return None

        if self.app.use_large_files:
            # medium files < 1mb
            res = self.files_create_size("m", 1024*1024, self.nr_files, self.app.local_folder_in, l_files)
            if not res:
                return None

        # large files < 10mb
            res = self.files_create_size("l", 10*1024*1024, self.nr_files, self.app.local_folder_in, l_files)
            if not res:
                return None

        # randomize list
        random.shuffle(l_files)

        return l_files

    def files_check(self, l_files, dir_name=""):
        """
        check files on both folders
        compare names, size, md5 sums
        """
        print "Checking files.."

        # check files
        for f in l_files:
            dd_out = os.path.join(self.app.local_folder_out, dir_name)
            ffname = os.path.join(dd_out, f["name"])

            dd_in = os.path.join(self.app.local_folder_in, dir_name)
            ffname_in = os.path.join(dd_in, f["name"])

            success = False

            print "Comparing %s and %s" % (ffname_in, ffname)
            # try to access the file
            for r in range(0, self.nr_retries):
                try:
                    with open(ffname):
                        pass
                    success = True
                    break
                except IOError:
                    # wait for a file
                    print "File %s not found! Retrying [%d/%d] .." % (ffname, r + 1, self.nr_retries)
                    self.app.sync()
            if success == False:
                print "Failed to compare files: %s and %s" % (ffname_in, ffname)
                return False
            # get md5 of synced file
            md5_str = self.md5_for_file(ffname)
            if md5_str != f["md5"]:
                print "MD5 sums don't match for file: %s" % ffname
                return False
        return True

    def dir_create(self, dname, files_num, files_maxsize):
        """
        create and fill directory with files
        return files list
        """
        try:
            os.makedirs(dname)
        except OSError:
            print "Failed to create directory: %s" % dname
            return None

        l_files = []
        res = self.files_create_size("s", files_maxsize, files_num, dname, l_files)
        if not res:
            return None

        return l_files

    def dir_create_size(self, symbol, dirs_num, files_num, files_maxsize, parent_dir, l_dirs):
        """
        create dirs_num directories with  directories
        """
        for i in range(dirs_num):
            strlen = random.randint(0, 20)
            dname = symbol + self.get_random_str(size=strlen) + str(i)
            ddname = os.path.join(parent_dir, dname)
            l_files = self.dir_create(ddname, files_num, files_maxsize)
            if l_files is None:
                print "Failed to create directory: %s" % ddname
                return False
            l_dirs.append({"name":dname, "files_nr":files_num, "name_orig":dname, "l_files":l_files})
            print "Directory created: %s [%d files]" % (ddname, files_num)
        return True

    def dirs_create(self):
        """
        create dirs
        """
        print "Creating directories.."

        l_dirs = []

        # create empty dirs
        res = self.dir_create_size("z", 10, 0, 0, self.app.local_folder_in, l_dirs)
        if not res:
            return None

        # create dirs with < 20 files
        res = self.dir_create_size("d", 10, 10, 1024, self.app.local_folder_in, l_dirs)
        if not res:
            return None

        # randomize list
        random.shuffle(l_dirs)

        return l_dirs

    def dirs_check(self, l_dirs):
        """
        check directories for both folders
        """
        print "Checking directories.."

        for d in l_dirs:
            dname = os.path.join(self.app.local_folder_out, d["name"])
            dname_in = os.path.join(self.app.local_folder_in, d["name"])
            success = False

            print "Comparing dirs: %s and %s" % (dname_in, dname)

            # try to access the dir
            for r in range(0, self.nr_retries):
                try:
                    if os.path.isdir(dname):
                        success = True
                        break
                    else:
                        # wait for a dir
                        print "Directory %s not found! Retrying [%d/%d].." % (dname, r + 1, self.nr_retries)
                        self.app.sync()
                except OSError:
                    # wait for a dir
                    print "Directory %s not found! Retrying [%d/%d].." % (dname, r + 1, self.nr_retries)
                    self.app.sync()
            if success == False:
                print "Failed to access directories: %s and " % dname
                return False

            # check files
            res = self.files_check(d["l_files"], d["name"])
            if not res:
                print "Directories do not match !"
                return False

        return True

    def file_rename(self, ffname_src, ffname_dst):
        """
        renaming file
        return True if renamed
        """

        for r in range(0, self.nr_retries):
            if os.path.exists(ffname_src):
                try:
                    shutil.move(ffname_src, ffname_dst)
                except OSError:
                    print "Failed to rename file: %s" % ffname_src
                    return False

            if self.force_syncing:
                self.app.sync()

            # try to access both files (old and new)
            if not os.path.exists(ffname_dst):
                print "Failed to access a newly renamed file: %s, retrying [%d/%d].." % (ffname_dst, r + 1, self.nr_retries)
                continue
            if os.path.exists(ffname_src):
                print "Still can access an old renamed file: %s, retrying [%d/%d]" % (ffname_src, r + 1, self.nr_retries)
                continue
            break

        # try to access both files (old and new)
        if not os.path.exists(ffname_dst):
            print "Failed to access a newly renamed file: %s. Aborting.." % ffname_dst
            return False
        if os.path.exists(ffname_src):
            print "Still can access an old renamed file: %s. Aborting.." % ffname_src
            return False
        return True

    def files_rename(self, l_files):
        """
        rename objects in "in" instance and check new files in "out" instance
        """
        print "Renaming files.."

        for f in l_files:
            ffname_src = os.path.join(self.app.local_folder_in, f["name"])
            f["name"] = "renamed_" + self.get_random_str(30)
            ffname_dst = os.path.join(self.app.local_folder_in, f["name"])

            print "Renaming file: %s => %s" % (ffname_src, ffname_dst)

            if not self.file_rename(ffname_src, ffname_dst):
                return False

        return True

    def files_remove(self, l_files):
        """
        remove files in "in" instance and check files absence in "out" instance
        """
        print "Removing files.."

        for f in l_files:
            ffname = os.path.join(self.app.local_folder_in, f["name"])

            print "Deleting: %s" % ffname

            for r in range(0, self.nr_retries):
                try:
                    os.remove(ffname)
                except OSError:
                    print "Failed to delete file: %s" % ffname
                    return False

                if self.force_syncing:
                    self.app.sync()

                # check if local file does not exist
                if not os.path.exists(ffname):
                    break
                print "Deleted file %s still exists, retrying [%d/%d].." % (ffname, r + 1, self.nr_retries)

            if os.path.exists(ffname):
                print "Deleted file %s still exists, aborting.." % ffname

        success = False
        for f in l_files:
            ffname = os.path.join(self.app.local_folder_out, f["name"])
            for r in range(0, self.nr_retries):
                try:
                    # file must be deleted
                    with open(ffname):
                        pass
                    print "File %s is not deleted. Retrying [%d/%d] .." % (ffname, r + 1, self.nr_retries)
                    self.app.sync()
                except IOError:
                    success = True
                    break
            if success == False:
                print "Failed to delete file: %s" % ffname
                return False
        return True

    def dirs_rename(self, l_dirs):
        """
        rename directories in "in" instance and check directories new names in "out" instance
        """
        print "Renaming directories.."

        for d in l_dirs:
            dname_src = os.path.join(self.app.local_folder_in, d["name"])
            d["name"] = "renamed_" + self.get_random_str(30)
            dname_dst = os.path.join(self.app.local_folder_in, d["name"])
            try:
                shutil.move(dname_src, dname_dst)
            except OSError:
                print "Failed to rename directory: %s" % dname_src
                return False

            if self.force_syncing:
                self.app.sync()

            # try to both dirs
            if not os.path.exists(dname_dst):
                print "Failed to access a newly renamed directory: %s" % dname_dst
                return False
            if os.path.exists(dname_src):
                print "Still can access an old directory: %s" % dname_src
                return False

            print "Directory renamed: %s => %s" % (dname_src, dname_dst)

        return True

    def dirs_remove(self, l_dirs):
        """
        remove directories in "in" instance and check directories absence in "out" instance
        """
        print "Removing directories.."

        for d in l_dirs:
            dname = os.path.join(self.app.local_folder_in, d["name"])
            try:
                shutil.rmtree(dname)
            except OSError:
                print "Failed to delete dir: %s" % dname
                return False
            print "Directory removed: %s" % dname

            if self.force_syncing:
                self.app.sync()

            if os.path.exists(dname):
                print "Still can access a renamed directory: %s" % dname
                return False

        success = False
        for d in l_dirs:
            dname = os.path.join(self.app.local_folder_out, d["name"])
            for r in range(0, self.nr_retries):
                try:
                    # dir must be deleted
                    if not os.path.isdir(dname):
                        success = True
                        break
                    print "Directory %s is not deleted! Retrying [%d/%d] .." % (dname, r + 1, self.nr_retries)
                    self.app.sync()
                except OSError:
                    success = True
                    break
            if success == False:
                print "Failed to delete dir: %s" % dname
                return False
        return True

    def dirs_check_empty(self):
        """
        return True if both folders are empty
        """
        return self.check_empty(self.app.local_folder_in) and self.check_empty(self.app.local_folder_out)

    def local_tree_create_dir(self, parent_dir):
        """
        generate directory
        """
        strlen = random.randint(10, 20)
        dname = self.get_random_str(size=strlen)
        ddname = os.path.join(parent_dir, dname)
        real_dname = os.path.join(self.app.local_folder_in, ddname)

        try:
            os.makedirs(real_dname)
        except OSError:
            print "Failed to create directory: %s" % real_dname
            return None, None, None, None

        # populate with random amount of files
        obj_nr = random.randint(1, self.local_obj_nr)
        l_files = []
        for _ in range(0, obj_nr):
            strlen = random.randint(10, 20)
            fname = self.get_random_str(size=strlen) + ".txt"
            ffname = os.path.join(ddname, fname)
            fname_real = os.path.join(self.app.local_folder_in, ffname)
            try:
                self.file_create(fname_real, random.randint(10, 100))
            except IOError:
                print "Failed to create file: %s" % fname_real
                return None, None, None, None
            l_files.append({"name":fname, "fname":ffname})

        # populate with random amount of dirs
        obj_nr = random.randint(1, self.local_obj_nr)
        l_dirs = []
        for _ in range(0, obj_nr):
            strlen = random.randint(10, 20)
            cname = self.get_random_str(size=strlen)
            ccname = os.path.join(ddname, cname)
            cname_real = os.path.join(self.app.local_folder_in, ccname)
            try:
                os.makedirs(cname_real)
            except OSError:
                print "Failed to create directory: %s" % cname_real
                return None, None, None, None
            l_dirs.append({"name":cname, "fname":ccname})

            # recursively create subtree
        return dname, ddname, l_files, l_dirs

    def local_tree_create(self, parent_dir, dirs_nr):
        """
        generate local directory tree, recursively populate with random number of directories / files
        return list of dictionaries
        """
        if dirs_nr == 0:
            return None

        l_tree = []

        for _ in range(0, dirs_nr):
            dname, ddname, l_files, l_dirs = self.local_tree_create_dir(parent_dir)
            if dname is None:
                return None
            l_child = self.local_tree_create(ddname, dirs_nr - 1)
            l_tree.append({"name":dname, "fname":ddname, "files":l_files, "dirs":l_dirs, "child":l_child})
        return l_tree

    def local_tree_get_dir(self, l_tree):
        """
        directory walk generator
        Returns relative directory path
        """
        for i in l_tree:
            yield i["fname"]
            for dd in i["dirs"]:
                yield dd["fname"]
            if i["child"] is not None:
                for x in self.local_tree_get_dir(i["child"]):
                    yield x

    def local_tree_get_dirs(self, l_tree):
        """
        directory walk generator
        Returns dir dict
        """
        for i in l_tree:
            yield i
            if i["child"] is not None:
                for x in self.local_tree_get_dirs(i["child"]):
                    yield x

    def local_tree_get_file(self, l_tree):
        """
        directory walk generator
        Returns relative file path
        """
        for i in l_tree:
            for ff in i["files"]:
                yield ff["fname"]
            if i["child"] is not None:
                for x in self.local_tree_get_file(i["child"]):
                    yield x

    def local_tree_compare(self, l_tree):
        """
        compare two local trees
        return True if they are the same
        """
        total_dirs = total_files = 0

        # try to access directories in "out" folder
        for d in self.local_tree_get_dir(l_tree):
            dname = os.path.join(self.app.local_folder_out, d)
            success = False
            total_dirs = total_dirs + 1

            # print "Trying to access dir: %s" % dname
            for r in range(0, self.nr_retries):
                try:
                    if os.path.isdir(dname):
                        success = True
                        break
                    else:
                        # wait for a dir
                        print "Directory %s not found! Retrying [%d/%d].." % (dname, r + 1, self.nr_retries)
                        self.app.sync()
                except OSError:
                    # wait for a dir
                    print "Directory %s not found! Retrying [%d/%d].." % (dname, r + 1, self.nr_retries)
                    self.app.sync()
            if success == False:
                print "Failed to access directories: %s and " % dname
                return False

        # try to access files in "out" folder
        for f in self.local_tree_get_file(l_tree):
            fname = os.path.join(self.app.local_folder_out, f)
            success = False
            total_files = total_files + 1

            # print "Trying to access file: %s" % fname
            for r in range(0, self.nr_retries):
                try:
                    with open(fname):
                        pass
                    success = True
                    break
                except IOError:
                    # wait for a file
                    print "File %s not found! Retrying [%d/%d] .." % (fname, r + 1, self.nr_retries)
                    self.app.sync()
            if success == False:
                print "Failed to access file: %s" % fname
                return False

        print "Total dirs: %d, files: %d" % (total_dirs, total_files)
        return True

    def local_tree_create_and_move(self, l_tree):
        """
        create a folder and fill with content
        randomly select an existing folder and create a subfolder of it
        move the first folder to the newly created one
        compare results, repeat 10 times
        return True if success
        """

        for _ in range(0, 10):
            # Create a dir
            l_dir = []
            dname, ddname, l_files, l_dirs = self.local_tree_create_dir("")
            if dname is None:
                return False
            print "Directory created: %s" % ddname
            l_dir.append({"name":dname, "fname":ddname, "files":l_files, "dirs":l_dirs, "child":None})

            # wait for a sync and compare
            if not self.local_tree_compare(l_dir):
                return False

            self.app.sync()

            # select random existing folder
            dir_dicts_l = [d for d in self.local_tree_get_dirs(l_tree)]
            dd = random.choice(dir_dicts_l)

            if dd["dirs"] is None:
                dd["dirs"] = []

            # create a new subfolder
            strlen = random.randint(10, 20)
            dname = self.get_random_str(size=strlen)
            ddname = os.path.join(dd["fname"], dname)
            dname_real = os.path.join(self.app.local_folder_in, ddname)
            print "Creating new dir: %s, parent: %s" % (ddname, dd["fname"])
            try:
                os.makedirs(dname_real)
            except OSError:
                print "Failed to create directory: %s" % dname_real
                return False

            dd["dirs"].append({"name":dname, "fname":ddname, "files":None, "dirs":None, "child":None})

            # move existing folder into newly created folder
            old_name = os.path.join(self.app.local_folder_in, l_dir[0]["fname"])
            new_name = os.path.join(dname_real, l_dir[0]["name"])

            print "Moving %s to %s" % (old_name, new_name)

            try:
                shutil.move(old_name, new_name)
            except OSError:
                print "Failed to move dir: %s to new: %s" % (old_name, new_name)
                return False

            # fix existing dir dict
            new_ffname = os.path.join(ddname, l_dir[0]["name"])
            l_dir[0]["fname"] = new_ffname
            for f in l_dir[0]["files"]:
                f["ffname"] = os.path.join(new_ffname, f["name"])
            for d in l_dir[0]["dirs"]:
                d["ffname"] = os.path.join(new_ffname, d["name"])

            self.app.sync()

            # wait for a sync and compare
            if not self.local_tree_compare(l_tree):
                return False

        # all good !
        return True

    def local_tree_multiple_renames(self, l_tree):
        """
        perform several object renames
        then rename back to the original name
        """

        # rename dirs
        for _ in range(0, 10):
            # select random existing folder
            dir_dicts_l = [d for d in self.local_tree_get_dirs(l_tree)]
            dd = random.choice(dir_dicts_l)
            if dd["dirs"] is None:
                continue

            oname = dd["dirs"][0]["name"]
            odname = os.path.join(dd["fname"], oname)
            orig_name = os.path.join(self.app.local_folder_in, odname)
            prev_name = orig_name

            # rename 10 times
            for _ in range(0, 10):
                strlen = random.randint(10, 20)
                dname = self.get_random_str(size=strlen)
                ddname = os.path.join(dd["fname"], dname)
                dname_real = os.path.join(self.app.local_folder_in, ddname)

                print "Renaming %s to %s" % (prev_name, dname_real)

                if not self.file_rename(prev_name, dname_real):
                    return False

                prev_name = dname_real

            # rename back to origin
            print "Moving %s to %s" % (prev_name, orig_name)

            if not self.file_rename(prev_name, orig_name):
                return False

            self.app.sync()

            # wait for a sync and compare
            if not self.local_tree_compare(l_tree):
                return False

        # rename files
        for _ in range(0, 10):
            # select random existing folder
            dir_dicts_l = [d for d in self.local_tree_get_dirs(l_tree)]
            dd = random.choice(dir_dicts_l)
            if dd["files"] is None:
                continue

            oname = dd["files"][0]["name"]
            odname = os.path.join(dd["fname"], oname)
            orig_name = os.path.join(self.app.local_folder_in, odname)
            prev_name = orig_name

            # rename 10 times
            for _ in range(0, 10):
                strlen = random.randint(10, 20)
                dname = self.get_random_str(size=strlen)
                ddname = os.path.join(dd["fname"], dname)
                dname_real = os.path.join(self.app.local_folder_in, ddname)

                print "Renaming %s to %s" % (prev_name, dname_real)

                if not self.file_rename(prev_name, dname_real):
                    return False

                prev_name = dname_real

            # rename back to origin
            print "Moving %s to %s" % (prev_name, orig_name)

            if not self.file_rename(prev_name, orig_name):
                return False

            self.app.sync()

            # wait for a sync and compare
            if not self.local_tree_compare(l_tree):
                return False

        return True
