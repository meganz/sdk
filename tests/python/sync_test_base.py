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
import logging
import platform
import unicodedata
import time

def get_unicode_str(size=10, max_char=0xFFFF, onlyNormalized=False, includeUnexisting=False):
    '''
    generates valid (for current OS) Unicode file name
    Notice: if includeUnexisting==True, it is possible that files don't get synchronized
    '''
    if platform.system() == "Windows":
        # Unicode characters 1 through 31, as well as quote ("), less than (<), greater than (>), pipe (|), backspace (\b), null (\0) and tab (\t).
        exclude = string.punctuation + u"\t" +  u''.join([unichr(x) for x in range(0, 32)])
    else:
        # I guess it mainly depends on fs type
        #exclude = u"/" + u"." + u''.join([unichr(x) for x in range(0, 1)])
        exclude = u"/" + u"." + u''.join([unichr(x) for x in range(0, 32)])


    name = u""
    while len(name) < size:
        c = unichr(random.randint(0, max_char))
        if c not in exclude:
            try:
                if not includeUnexisting:
                    unicodedata.name(c) #this will cause invalid unicode character to throw exception
                if onlyNormalized:
                    name = name + unicodedata.normalize('NFC',c) #only normalized chars
                else:
                    name = name + c
 #           except UnicodeDecodeError:
 #               print "UnicodeDecodeError con",c,repr(c),c.encode('utf-8')
 #               c.decode('utf-8')
 #               try:
 #                   unicodedata.name(c)
 #               except:
 #                   pass
 #               pass
            except ValueError:
 #               try:
 #                   unicodedata.name(c)
#                    print "that one was valid!",c,repr(c)
 #                   pass
 #               except:
 #                   pass
                pass
    return name

def get_exotic_str(size=10):
    """
    generate string containing random combinations of % and ASCII symbols
    """
    name = u""
    while len(name) < size:
        num = random.randint(1, 3)
        name = name + u"%" * num + get_random_str(2)
    return name

def get_random_str(size=10, chars=string.ascii_lowercase + string.ascii_uppercase + string.digits):
    """
    return a random string
    size: size of an output string
    chars: characters to use
    """
    return ''.join(random.choice(chars) for x in range(size))

def generate_ascii_name(first_symbol, i):
    """
    generate random ASCII string
    """
    strlen = random.randint(0, 20)
    return first_symbol + get_random_str(size=strlen) + str(i)

def generate_unicode_name_old(first_symbol, i):
    """
    generate random UTF string
    """
    strlen = random.randint(10, 30)
    c = random.choice(['short-utf', 'utf', 'exotic'])
    if c == 'short-utf':
        s = get_unicode_str(strlen, 0xFF)
    elif c == 'utf':
        s = get_unicode_str(strlen)
    else:
        s = get_exotic_str(strlen)
    #logging.debug("Creating Unicode file:  %s" % (s.encode("unicode-escape")))
    return s

cogen=0
def generate_unicode_name(first_symbol, i):
    """
    generate random UTF string
    """
    strlen = random.randint(10, 30)
    c = random.choice(['short-utf', 'utf', 'exotic'])
    if c == 'short-utf':
        s = get_unicode_str(strlen, 0xFF)
    elif c == 'utf':
        s = get_unicode_str(strlen)
    else:
        s = get_exotic_str(strlen)
    #logging.debug("Creating Unicode file:  %s" % (s.encode("unicode-escape")))
    global cogen
    cogen=cogen+1
    return str(cogen)+"_"+s

def normalizeandescape(name):
    name=escapefsincompatible(name)
    name=unicodedata.normalize('NFC',unicode(name))
    #name=unicodedata.normalize('NFC',name)
    #name=escapefsincompatible(name)
    return name

def escapefsincompatible(name):
    """
    Escape file system incompatible characters
    """
    import urllib
    for i in "\\/:?\"<>|*":
        name=name.replace(i,urllib.quote(i).lower())
    return name

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

        self.nr_retries = 200
        self.nr_files = 10
        self.nr_dirs = 10
        self.nr_time_changes = 10
        self.nr_changes = 10
        self.local_obj_nr = 5
        self.force_syncing = False

    def check_empty(self, folder_name):
        """
        return True if folder is empty
        """
        logging.debug("Checking if folder %s is empty" % folder_name)

        # leave old files
        if not self.app.delete_tmp_files:
            return True

        for r in range(0, self.nr_retries):
            self.app.attempt=r
            try:
                res = not os.listdir(folder_name)
            except OSError, e:
                logging.error("Failed to list dir: %s (%s)" % (folder_name, e))
                return False

            if res:
                return True

            logging.debug("Directory %s is not empty! Retrying [%d/%d] .." % (folder_name, r + 1, self.nr_retries))
            self.app.sync()
            #~ try:
                #~ shutil.rmtree(folder_name)
            #~ except OSError, e:
                #~ logging.error("Failed to delete folder: %s (%s)" % (folder_name, e))
                #~ return False

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

    @staticmethod
    def touch(path):
        """
        create an empty file
        update utime
        """
        with open(path, 'a'):
            os.utime(path, None)

    @staticmethod
    def file_create(fname, fsize):
        """
        create a file of a size fsize and fill with a random data
        """
        fout = open(fname, 'wb')
        fout.write(get_random_str(fsize))
        fout.close()

    def files_create_size(self, first_symbol, maxsize, nr_files, dname, file_generate_name_func, l_files):
        """
        create a list of files of a specific size
        """
        for i in range(nr_files):
            fname = file_generate_name_func(first_symbol, i)
            ffname = os.path.join(dname, fname)
            if maxsize == 0:
                fsize = 0
            else:
                fsize = random.randint(1, maxsize)

            try:
                self.file_create(ffname, fsize)
            except IOError, e:
                logging.error("Failed to create file: %s (%s)" % (ffname, e))
                return False
            except UnicodeEncodeError, e:
                logging.debug("Discarded filename due to UnicodeEncodeError: %s" % (ffname))
                i=i-1
                continue
                
            md5_str = self.md5_for_file(ffname)
            l_files.append({"name":fname, "size":fsize, "md5":md5_str, "name_orig":fname})
            logging.debug("File created: %s [%s, %db]" % (ffname, md5_str, fsize))
        return True

    def files_create(self, file_generate_name_func=generate_ascii_name):
        """
        create files in "in" instance and check files presence in "out" instance
        Return list of files
        """
        logging.debug("Creating files.. (nrfiles="+str(self.nr_files)+")")

        l_files = []

        # empty files
        res = self.files_create_size("e", 0, self.nr_files, self.app.local_folder_in, file_generate_name_func, l_files)
        if not res:
            return None

        # small files < 1k
        if not hasattr(self.app, 'only_empty_files') or not self.app.only_empty_files:
            res = self.files_create_size("s", 1024, self.nr_files, self.app.local_folder_in, file_generate_name_func, l_files)
            if not res:
                return None
    
        if self.app.use_large_files:
            # medium files < 1mb
            res = self.files_create_size("m", 1024*1024, self.nr_files, self.app.local_folder_in, file_generate_name_func, l_files)
            if not res:
                return None

        # large files < 10mb
            res = self.files_create_size("l", 10*1024*1024, self.nr_files, self.app.local_folder_in, file_generate_name_func, l_files)
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
        logging.debug("Checking files..")

        # check files
        for f in l_files:
            dd_out = os.path.join(self.app.local_folder_out, dir_name)
            ffname = os.path.join(dd_out, f["name"])
            #when saving mega alters some characters (we will look for 
            #destiny file having that in mind)
            ffname=normalizeandescape(ffname)
            
            
            dd_in = os.path.join(self.app.local_folder_in, dir_name)
            ffname_in = os.path.join(dd_in, f["name"])
            
            success = False

            logging.debug("Comparing %s and %s" % (ffname_in, ffname))
            # try to access the file
            for r in range(0, self.nr_retries):
                self.app.attempt=r
                try:
                    with open(ffname):
                        pass
                    success = True
                    break
                except IOError as ex:
                    # wait for a file
                    logging.debug("File %s not found! Retrying [%d/%d] .." % (ffname, r + 1, self.nr_retries))
                    logging.debug("%s" % (ffname.encode("unicode-escape")))
                    self.app.sync()
            if success is False:
                logging.error("Failed to compare files: %s and %s" % (ffname_in, ffname))
                return False
            # get md5 of synced file
            md5_str = self.md5_for_file(ffname)
            if md5_str != f["md5"]:
                logging.error("MD5 sums don't match for file: %s" % ffname)
                return False
        return True

    def dir_create(self, dname, files_num, files_maxsize, file_generate_name_func=generate_ascii_name):
        """
        create and fill directory with files
        return files list
        """
        try:
            os.makedirs(dname)
        except OSError, e:
            logging.error("Failed to create directory: %s (%s)" % (dname, e))
            return None

        l_files = []
        res = self.files_create_size("s", files_maxsize, files_num, dname, file_generate_name_func, l_files)
        if not res:
            return None

        return l_files

    def dir_create_size(self, symbol, dirs_num, files_num, files_maxsize, parent_dir, dir_generate_name_func, l_dirs):
        """
        create dirs_num directories with  directories
        """
        for i in range(dirs_num):
            dname = dir_generate_name_func(symbol, i)
            ddname = os.path.join(parent_dir, dname)
            l_files = self.dir_create(ddname, files_num, files_maxsize, dir_generate_name_func)
            if l_files is None:
                logging.error("Failed to create directory: %s" % ddname)
                return False
            l_dirs.append({"name":dname, "files_nr":files_num, "name_orig":dname, "l_files":l_files})
            logging.debug("Directory created: %s [%d files]" % (ddname, files_num))
        return True

    def dirs_create(self, dir_generate_name_func=generate_ascii_name):
        """
        create dirs
        """
        logging.debug("Creating "+str(self.nr_dirs)+" directories..")

        l_dirs = []

        # create empty dirs
        res = self.dir_create_size("z", self.nr_dirs, 0, 0, self.app.local_folder_in, dir_generate_name_func, l_dirs)
        if not res:
            return None

        # create dirs with #nr_files files
        if not hasattr(self.app, 'only_empty_folders') or not self.app.only_empty_folders:
            res = self.dir_create_size("d", self.nr_dirs, self.nr_files, 1024, self.app.local_folder_in, dir_generate_name_func, l_dirs)
            if not res:
                return None

        # randomize list
        random.shuffle(l_dirs)

        return l_dirs

    def dirs_check(self, l_dirs):
        """
        check directories for both folders
        """
        logging.debug("Checking directories..")

        for d in l_dirs:
            dname = os.path.join(self.app.local_folder_out, d["name"])
            ##when saving mega alters some characters (we will look for 
            #destiny file having that in mind)
            dname=normalizeandescape(dname)
                
            dname_in = os.path.join(self.app.local_folder_in, d["name"])
            success = False

            logging.debug("Comparing dirs: %s and %s" % (dname_in, dname))

            # try to access the dir
            for r in range(0, self.nr_retries):
                self.app.attempt=r
                try:
                    if os.path.isdir(dname):
                        success = True
                        break
                    else:
                        # wait for a dir
                        logging.debug("Directory %s not found! Retrying [%d/%d].." % (dname, r + 1, self.nr_retries))
                        self.app.sync()
                except OSError:
                    # wait for a dir
                    logging.debug("Directory %s not found! Retrying [%d/%d].." % (dname, r + 1, self.nr_retries))
                    self.app.sync()
            if success is False:
                logging.error("Failed to access directories: %s and " % dname)
                return False

            # check files
            res = self.files_check(d["l_files"], d["name"])
            if not res:
                logging.error("Directories do not match !")
                return False

        return True

    def file_rename(self, ffname_src, ffname_dst):
        """
        renaming file
        return True if renamed
        """
        for r in range(0, self.nr_retries):
            self.app.attempt=r
            if os.path.exists(ffname_src):
                try:
                    shutil.move(ffname_src, ffname_dst)
                except OSError, e:
                    logging.error("Failed to rename file: %s (%s)" % (ffname_src, e))
                    return False

            if self.force_syncing:
                self.app.sync()

            # try to access both files (old and new)
            if not os.path.exists(ffname_dst):
                logging.debug("Failed to access a newly renamed file: %s, retrying [%d/%d].." % (ffname_dst, r + 1, self.nr_retries))
                continue
            if os.path.exists(ffname_src):
                logging.debug("Still can access an old renamed file: %s, retrying [%d/%d]" % (ffname_src, r + 1, self.nr_retries))
                continue
            break

        # try to access both files (old and new)
        if not os.path.exists(ffname_dst):
            logging.error("Failed to access a newly renamed file: %s. Aborting.." % ffname_dst)
            return False
        if os.path.exists(ffname_src):
            logging.error("Still can access an old renamed file: %s. Aborting.." % ffname_src)
            return False
        return True

    def files_rename(self, l_files, file_generate_name_func=generate_ascii_name):
        """
        rename objects in "in" instance and check new files in "out" instance
        """
        logging.debug("Renaming files..")

        i = 0
        for f in l_files:
            ffname_src = os.path.join(self.app.local_folder_in, f["name"])
            f["name"] = file_generate_name_func("renamed_", i)
            i = i + 1
            ffname_dst = os.path.join(self.app.local_folder_in, f["name"])

            logging.debug("Renaming file: %s => %s" % (ffname_src, ffname_dst))

            if not self.file_rename(ffname_src, ffname_dst):
                return False

        return True
        
    def files_moveanddelete(self, l_files, where=".", timeout=0, file_generate_name_func=generate_ascii_name):
        """
        moves and deletes objects in "in" instance and check new files in "out" instance
        """
        logging.debug("Move&Rename files..")
        try:
            os.makedirs(os.path.join(self.app.local_folder_in,where))
        except Exception, e:
            logging.debug("Unable to create subfolder: %s (%s)" % (where, e))

        i = 0
        for f in l_files:
            ffname_src = os.path.join(self.app.local_folder_in, f["name"])
            f["name"] = file_generate_name_func("renamed_", i)
            i = i + 1
            ffname_dst = os.path.join(self.app.local_folder_in, where, f["name"])

            logging.debug("move&delete file: %s => %s" % (ffname_src, ffname_dst))

            if os.path.exists(ffname_src):
                try:
                    shutil.move(ffname_src, ffname_dst)
                except OSError, e:
                    logging.error("Failed to rename file: %s (%s)" % (ffname_src, e))
                    return False
            try:
                time.sleep(timeout)
                os.remove(ffname_dst)
            except OSError, e:
                logging.error("Failed to delete file: %s (%s)" % (ffname_dst, e))
                return False

        if (where != "."):
            shutil.rmtree(os.path.join(self.app.local_folder_in,where))
        return True
        
    def files_mimic_update_with_backup(self, l_files, timeout=0, file_generate_name_func=generate_ascii_name):
        """
        moves and deletes objects in "in" instance and check new files in "out" instance
        """
        logging.debug("Mimic update with backup files..")

        i = 0
        for f in l_files:
            ffname_src = os.path.join(self.app.local_folder_in, f["name"])
            #f["name"] = file_generate_name_func("renamed_", i)
            i = i + 1
            ffname_dst = os.path.join(self.app.local_folder_in, "renamed_"+f["name"])
            ffname_dst_out = os.path.join(self.app.local_folder_out, "renamed_"+f["name"])

            logging.debug("Mimic update with backup file: %s => %s" % (ffname_src, ffname_dst))

            if os.path.exists(ffname_src):
                try:
                    shutil.move(ffname_src, ffname_dst)
                except OSError, e:
                    logging.error("Failed to rename file: %s (%s)" % (ffname_src, e))
                    return False
            try:
                time.sleep(timeout)
                with open(ffname_dst, 'r') as f:
                    with open(ffname_src, 'w') as f2:
                        for r in range(100):
                            f2.write("whatever")
                            time.sleep(0.03)
                            if os.path.exists(ffname_dst_out): #existing temporary file
                                logging.error("ERROR in sync: Temporary file being created in syncout: : %s!" % (ffname_dst))
                                os.remove(ffname_dst)
                                return False;
                os.remove(ffname_dst)
            except OSError, e:
                logging.error("Failed to delete file: %s (%s)" % (ffname_dst, e))
                return False

        return True

    def files_remove(self, l_files):
        """
        remove files in "in" instance and check files absence in "out" instance
        """
        logging.debug("Removing files..")

        for f in l_files:
            ffname = os.path.join(self.app.local_folder_in, f["name"])

            logging.debug("Deleting: %s" % ffname)

            for r in range(0, self.nr_retries):
                self.app.attempt=r
                try:
                    os.remove(ffname)
                except OSError, e:
                    logging.error("Failed to delete file: %s (%s)" % (ffname, e))
                    return False

                if self.force_syncing:
                    self.app.sync()

                # check if local file does not exist
                if not os.path.exists(ffname):
                    break
                logging.debug("Deleted file %s still exists, retrying [%d/%d].." % (ffname, r + 1, self.nr_retries))

            if os.path.exists(ffname):
                logging.debug("Deleted file %s still exists, aborting.." % ffname)

        success = False
        for f in l_files:
            ffname = os.path.join(self.app.local_folder_out, f["name"])
            
            for r in range(0, self.nr_retries):
                self.app.attempt=r
                try:
                    # file must be deleted
                    with open(ffname):
                        pass
                    logging.debug("File %s is not deleted. Retrying [%d/%d] .." % (ffname, r + 1, self.nr_retries))
                    self.app.sync()
                except IOError:
                    success = True
                    break
            if success is False:
                logging.error("Failed to delete file: %s" % ffname)
                return False
        return True

    def dirs_rename(self, l_dirs, dir_generate_name_func=generate_ascii_name):
        """
        rename directories in "in" instance and check directories new names in "out" instance
        """
        logging.debug("Renaming directories..")

        i = 0
        for d in l_dirs:
            dname_src = os.path.join(self.app.local_folder_in, d["name"])
            d["name"] = dir_generate_name_func("renamed_", i)
            i = i + 1
            dname_dst = os.path.join(self.app.local_folder_in, d["name"])
            try:
                shutil.move(dname_src, dname_dst)
            except OSError, e:
                logging.error("Failed to rename directory: %s (%s)" % (dname_src, e))
                return False

            if self.force_syncing:
                self.app.sync()

            # try to both dirs
            if not os.path.exists(dname_dst):
                logging.error("Failed to access a newly renamed directory: %s" % dname_dst)
                return False
            if os.path.exists(dname_src):
                logging.error("Still can access an old directory: %s" % dname_src)
                return False

            logging.debug("Directory renamed: %s => %s" % (dname_src, dname_dst))

        return True

    def dirs_remove(self, l_dirs):
        """
        remove directories in "in" instance and check directories absence in "out" instance
        """
        logging.debug("Removing directories..")

        for d in l_dirs:
            dname = os.path.join(self.app.local_folder_in, d["name"])
            try:
                shutil.rmtree(dname)
            except OSError, e:
                logging.error("Failed to delete dir: %s (%s)" % (dname, e))
                return False
            logging.debug("Directory removed: %s" % dname)

            if self.force_syncing:
                self.app.sync()

            if os.path.exists(dname):
                logging.error("Still can access a renamed directory: %s" % dname)
                return False

        success = False
        for d in l_dirs:
            dname = os.path.join(self.app.local_folder_out, d["name"])
            for r in range(0, self.nr_retries):
                self.app.attempt=r
                try:
                    # dir must be deleted
                    if not os.path.isdir(dname):
                        success = True
                        break
                    logging.debug("Directory %s is not deleted! Retrying [%d/%d] .." % (dname, r + 1, self.nr_retries))
                    self.app.sync()
                except OSError:
                    success = True
                    break
            if success is False:
                logging.error("Failed to delete dir: %s" % dname)
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
        dname = get_random_str(size=strlen)
        ddname = os.path.join(parent_dir, dname)
        real_dname = os.path.join(self.app.local_folder_in, ddname)

        try:
            os.makedirs(real_dname)
        except OSError, e:
            logging.error("Failed to create directory: %s (%s)" % (real_dname, e))
            return None, None, None, None

        # populate with random amount of files
        obj_nr = random.randint(1, self.local_obj_nr)
        l_files = []
        for _ in range(0, obj_nr):
            strlen = random.randint(10, 20)
            fname = get_random_str(size=strlen) + ".txt"
            ffname = os.path.join(ddname, fname)
            fname_real = os.path.join(self.app.local_folder_in, ffname)
            try:
                self.file_create(fname_real, random.randint(10, 100))
            except IOError, e:
                logging.error("Failed to create file: %s (%s)" % (fname_real, e))
                return None, None, None, None
            l_files.append({"name":fname, "fname":ffname})

        # populate with random amount of dirs
        obj_nr = random.randint(1, self.local_obj_nr)
        l_dirs = []
        for _ in range(0, obj_nr):
            strlen = random.randint(10, 20)
            cname = get_random_str(size=strlen)
            ccname = os.path.join(ddname, cname)
            cname_real = os.path.join(self.app.local_folder_in, ccname)
            try:
                os.makedirs(cname_real)
            except OSError, e:
                logging.error("Failed to create directory: %s (%s)" % (cname_real, e))
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

            # logging.debug("Trying to access dir: %s" % dname)
            for r in range(0, self.nr_retries):
                self.app.attempt=r
                try:
                    if os.path.isdir(dname):
                        success = True
                        break
                    else:
                        # wait for a dir
                        logging.debug("Directory %s not found! Retrying [%d/%d].." % (dname, r + 1, self.nr_retries))
                        self.app.sync()
                except OSError:
                    # wait for a dir
                    logging.debug("Directory %s not found! Retrying [%d/%d].." % (dname, r + 1, self.nr_retries))
                    self.app.sync()
            if success is False:
                logging.error("Failed to access directories: %s and " % dname)
                return False

        # try to access files in "out" folder
        for f in self.local_tree_get_file(l_tree):
            fname = os.path.join(self.app.local_folder_out, f)
            success = False
            total_files = total_files + 1

            # logging.debug("Trying to access file: %s" % fname)
            for r in range(0, self.nr_retries):
                self.app.attempt=r
                try:
                    with open(fname):
                        pass
                    success = True
                    break
                except IOError:
                    # wait for a file
                    logging.debug("File %s not found! Retrying [%d/%d] .." % (fname, r + 1, self.nr_retries))
                    self.app.sync()
            if success is False:
                logging.error("Failed to access file: %s" % fname)
                return False

        logging.debug("Total dirs: %d, files: %d" % (total_dirs, total_files))
        return True

    def local_tree_create_and_move(self, l_tree):
        """
        create a folder and fill with content
        randomly select an existing folder and create a subfolder of it
        move the first folder to the newly created one
        compare results, repeat 10 times
        return True if success
        """

        for _ in range(0, self.nr_dirs):
            # Create a dir
            l_dir = []
            dname, ddname, l_files, l_dirs = self.local_tree_create_dir("")
            if dname is None:
                return False
            logging.debug("Directory created: %s" % ddname)
            l_dir.append({"name":dname, "fname":ddname, "files":l_files, "dirs":l_dirs, "child":None})

            # wait for a sync and compare
            if not self.local_tree_compare(l_dir):
                return False

            # select random existing folder
            dir_dicts_l = [d for d in self.local_tree_get_dirs(l_tree)]
            dd = random.choice(dir_dicts_l)

            if dd["dirs"] is None:
                dd["dirs"] = []

            # create a new subfolder
            strlen = random.randint(10, 20)
            dname = get_random_str(size=strlen)
            ddname = os.path.join(dd["fname"], dname)
            dname_real = os.path.join(self.app.local_folder_in, ddname)
            logging.debug("Creating new dir: %s, parent: %s" % (ddname, dd["fname"]))
            try:
                os.makedirs(dname_real)
            except OSError, e:
                logging.error("Failed to create directory: %s (%s)" % (dname_real, e))
                return False

            dd["dirs"].append({"name":dname, "fname":ddname, "files":None, "dirs":None, "child":None})

            # move existing folder into newly created folder
            old_name = os.path.join(self.app.local_folder_in, l_dir[0]["fname"])
            new_name = os.path.join(dname_real, l_dir[0]["name"])

            logging.debug("Moving %s to %s" % (old_name, new_name))

            try:
                shutil.move(old_name, new_name)
            except OSError, e:
                logging.error("Failed to move dir: %s to new: %s (%s)" % (old_name, new_name, e))
                return False

            # fix existing dir dict
            new_ffname = os.path.join(ddname, l_dir[0]["name"])
            l_dir[0]["fname"] = new_ffname
            for f in l_dir[0]["files"]:
                f["ffname"] = os.path.join(new_ffname, f["name"])
            for d in l_dir[0]["dirs"]:
                d["ffname"] = os.path.join(new_ffname, d["name"])

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
        for _ in range(0, self.nr_changes):
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
            for _ in range(0, self.nr_changes):
                strlen = random.randint(10, 20)
                dname = get_random_str(size=strlen)
                ddname = os.path.join(dd["fname"], dname)
                dname_real = os.path.join(self.app.local_folder_in, ddname)

                logging.debug("Renaming %s to %s" % (prev_name, dname_real))

                if not self.file_rename(prev_name, dname_real):
                    return False

                prev_name = dname_real

            # rename back to origin
            logging.debug("Moving %s to %s" % (prev_name, orig_name))

            if not self.file_rename(prev_name, orig_name):
                return False

            self.app.sync()

            # wait for a sync and compare
            if not self.local_tree_compare(l_tree):
                return False

        # rename files
        for _ in range(0, self.nr_changes):
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
            for _ in range(0, self.nr_changes):
                strlen = random.randint(10, 20)
                dname = get_random_str(size=strlen)
                ddname = os.path.join(dd["fname"], dname)
                dname_real = os.path.join(self.app.local_folder_in, ddname)

                logging.debug("Renaming %s to %s" % (prev_name, dname_real))

                if not self.file_rename(prev_name, dname_real):
                    return False

                prev_name = dname_real

            # rename back to origin
            logging.debug("Moving %s to %s" % (prev_name, orig_name))

            if not self.file_rename(prev_name, orig_name):
                return False

            self.app.sync()

            # wait for a sync and compare
            if not self.local_tree_compare(l_tree):
                return False

        return True
