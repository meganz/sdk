/**
 * @file posix/fs.cpp
 * @brief POSIX filesystem/directory access/notification
 *
 * (c) 2013-2014 by Mega Limited, Auckland, New Zealand
 *
 * This file is part of the MEGA SDK - Client Access Engine.
 *
 * Applications using the MEGA API must present a valid application key
 * and comply with the the rules set forth in the Terms of Service.
 *
 * The MEGA SDK is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * @copyright Simplified (2-clause) BSD License.
 *
 * MacOS X fsevents code based on osxbook.com/software/fslogger
 * (requires euid == root or passing an existing /dev/fsevents fd)
 * (c) Amit Singh
 *
 * You should have received a copy of the license along with this
 * program.
 */

#include "mega.h"
#include <sys/utsname.h>
#include <sys/ioctl.h>
#ifdef TARGET_OS_MAC
#include "mega/osx/osxutils.h"
#endif

#ifdef __ANDROID__
#include <jni.h>
extern JavaVM *MEGAjvm;
#endif

#if defined(__MACH__) && !(TARGET_OS_IPHONE)
#include <uuid/uuid.h>
#endif

#ifdef __linux__

#ifndef __ANDROID__
#include <linux/magic.h>
#endif /* ! __ANDROID__ */

#include <sys/vfs.h>

#ifndef FUSEBLK_SUPER_MAGIC
#define FUSEBLK_SUPER_MAGIC 0x65735546
#endif /* ! FUSEBLK_SUPER_MAGIC */

#ifndef FUSECTL_SUPER_MAGIC
#define FUSECTL_SUPER_MAGIC 0x65735543
#endif /* ! FUSECTL_SUPER_MAGIC */

#ifndef HFS_SUPER_MAGIC
#define HFS_SUPER_MAGIC 0x4244
#endif /* ! HFS_SUPER_MAGIC */

#ifndef HFSPLUS_SUPER_MAGIC
#define HFSPLUS_SUPER_MAGIC 0x482B
#endif /* ! HFSPLUS_SUPER_MAGIC */

#ifndef NTFS_SB_MAGIC
#define NTFS_SB_MAGIC 0x5346544E
#endif /* ! NTFS_SB_MAGIC */

#ifndef SDCARDFS_SUPER_MAGIC
#define SDCARDFS_SUPER_MAGIC 0x5DCA2DF5
#endif /* ! SDCARDFS_SUPER_MAGIC */

#ifndef F2FS_SUPER_MAGIC
#define F2FS_SUPER_MAGIC 0xF2F52010
#endif /* ! F2FS_SUPER_MAGIC */

#ifndef XFS_SUPER_MAGIC
#define XFS_SUPER_MAGIC 0x58465342
#endif /* ! XFS_SUPER_MAGIC */

#ifndef CIFS_MAGIC_NUMBER
#define CIFS_MAGIC_NUMBER 0xFF534D42
#endif // ! CIFS_MAGIC_NUMBER

#ifndef NFS_SUPER_MAGIC
#define NFS_SUPER_MAGIC 0x6969
#endif // ! NFS_SUPER_MAGIC

#ifndef SMB_SUPER_MAGIC
#define SMB_SUPER_MAGIC 0x517B
#endif // ! SMB_SUPER_MAGIC

#ifndef SMB2_MAGIC_NUMBER
#define SMB2_MAGIC_NUMBER 0xfe534d42
#endif // ! SMB2_MAGIC_NUMBER

#endif /* __linux__ */

#if defined(__APPLE__) || defined(USE_IOS)
#include <sys/mount.h>
#include <sys/param.h>
#endif /* __APPLE__ || USE_IOS */

namespace mega {
using namespace std;

bool PosixFileAccess::mFoundASymlink = false;

#ifdef USE_IOS

const string adjustBasePath(const LocalPath& name)
{
    // return a temporary variable that the caller can optionally use c_str on (in that expression)
    if (PosixFileSystemAccess::appbasepath)
    {
        if (!name.beginsWithSeparator())
        {
            string absolutename = PosixFileSystemAccess::appbasepath;
            absolutename.append(name.localpath);
            return absolutename;
        }
    }
    return name.localpath;
}

char* PosixFileSystemAccess::appbasepath = nullptr;

#else /* USE_IOS */

const string& adjustBasePath(const LocalPath& name)
{
    return name.localpath;
}

#endif /* ! USE_IOS */

int platformCompareUtf(const string& p1, bool unescape1, const string& p2, bool unescape2)
{
    return compareUtf(p1, unescape1, p2, unescape2, false);
}

int platformCompareUtf(const string& p1, bool unescape1, const LocalPath& p2, bool unescape2)
{
    return compareUtf(p1, unescape1, p2, unescape2, false);
}

int platformCompareUtf(const LocalPath& p1, bool unescape1, const string& p2, bool unescape2)
{
    return compareUtf(p1, unescape1, p2, unescape2, false);
}

int platformCompareUtf(const LocalPath& p1, bool unescape1, const LocalPath& p2, bool unescape2)
{
    return compareUtf(p1, unescape1, p2, unescape2, false);
}

#ifdef HAVE_AIO_RT
PosixAsyncIOContext::PosixAsyncIOContext() : AsyncIOContext()
{
    aiocb = NULL;
}

PosixAsyncIOContext::~PosixAsyncIOContext()
{
    LOG_verbose << "Deleting PosixAsyncIOContext";
    finish();
}

void PosixAsyncIOContext::finish()
{
    if (aiocb)
    {
        if (!finished)
        {
            LOG_debug << "Synchronously waiting for async operation";
            AsyncIOContext::finish();
        }
        delete aiocb;
        aiocb = NULL;
    }
    assert(finished);
}
#endif

PosixFileAccess::PosixFileAccess(Waiter *w, int defaultfilepermissions, bool followSymLinks) : FileAccess(w)
{
    fd = -1;
    this->defaultfilepermissions = defaultfilepermissions;

#ifndef HAVE_FDOPENDIR
    dp = NULL;
#endif

    mFollowSymLinks = followSymLinks;
    fsidvalid = false;
}

PosixFileAccess::~PosixFileAccess()
{
#ifndef HAVE_FDOPENDIR
    if (dp)
    {
        closedir(dp);
    }
#endif

    if (fd >= 0)
    {
        close(fd);
    }
}

bool PosixFileAccess::sysstat(m_time_t* mtime, m_off_t* size)
{
#ifdef USE_IOS
    const string nameStr = adjustBasePath(nonblocking_localname);
#else
    // use the existing string if it's not iOS, no need for a copy
    const string& nameStr = adjustBasePath(nonblocking_localname);
#endif

    struct stat statbuf;
    retry = false;

    type = TYPE_UNKNOWN;
    mIsSymLink = lstat(nameStr.c_str(), &statbuf) == 0
                 && S_ISLNK(statbuf.st_mode);
    if (mIsSymLink && !PosixFileAccess::mFoundASymlink)
    {
        LOG_warn << "Enabling symlink check for syncup";
        PosixFileAccess::mFoundASymlink = true;
    }

    if (!(mFollowSymLinks ? stat(nameStr.c_str(), &statbuf)
                         : lstat(nameStr.c_str(), &statbuf)))
    {
        errorcode = 0;
        if (S_ISDIR(statbuf.st_mode))
        {
            type = FOLDERNODE;
            return false;
        }

        type = FILENODE;
        *size = statbuf.st_size;
        *mtime = statbuf.st_mtime;

        FileSystemAccess::captimestamp(mtime);

        return true;
    }

    errorcode = errno;
    return false;
}

bool PosixFileAccess::sysopen(bool)
{
    assert(fd < 0 && "There should be no opened file descriptor at this point");
    if (fd >= 0)
    {
        sysclose();
    }

    assert(mFollowSymLinks); //Notice: symlinks are not considered here for the moment,
    // this is ok: this is not called with mFollowSymLinks = false, but from transfers doio.
    // When fully supporting symlinks, this might need to be reassessed

    return (fd = open(adjustBasePath(nonblocking_localname).c_str(), O_RDONLY)) >= 0;
}

void PosixFileAccess::sysclose()
{
    assert(nonblocking_localname.empty() || fd >= 0);
    if (fd >= 0)
    {
        close(fd);
        fd = -1;
    }
}

bool PosixFileAccess::asyncavailable()
{
#ifdef HAVE_AIO_RT
    #ifdef __APPLE__
        return false;
    #endif

    return true;
#else
    return false;
#endif
}

#ifdef HAVE_AIO_RT
AsyncIOContext *PosixFileAccess::newasynccontext()
{
    return new PosixAsyncIOContext();
}

void PosixFileAccess::asyncopfinished(sigval sigev_value)
{
    PosixAsyncIOContext *context = (PosixAsyncIOContext *)(sigev_value.sival_ptr);
    struct aiocb *aiocbp = context->aiocb;
    int e = aio_error(aiocbp);
    assert (e != EINPROGRESS);
    context->retry = (e == EAGAIN);
    context->failed = (aio_return(aiocbp) < 0);
    if (!context->failed)
    {
        if (context->op == AsyncIOContext::READ && context->pad)
        {
            memset((void *)(((char *)(aiocbp->aio_buf)) + aiocbp->aio_nbytes), 0, context->pad);
            LOG_verbose << "Async read finished OK";
        }
        else
        {
            LOG_verbose << "Async write finished OK";
        }
    }
    else
    {
        LOG_warn << "Async operation finished with error: " << e;
    }

    asyncfscallback userCallback = context->userCallback;
    void *userData = context->userData;
    context->finished = true;
    if (userCallback)
    {
        userCallback(userData);
    }
}
#endif

void PosixFileAccess::asyncsysopen(AsyncIOContext *context)
{
#ifdef HAVE_AIO_RT
    context->failed = !fopen(context->openPath, context->access & AsyncIOContext::ACCESS_READ,
                             context->access & AsyncIOContext::ACCESS_WRITE);
    context->retry = retry;
    context->finished = true;
    if (context->userCallback)
    {
        context->userCallback(context->userData);
    }
#endif
}

void PosixFileAccess::asyncsysread(AsyncIOContext *context)
{
#ifdef HAVE_AIO_RT
    if (!context)
    {
        return;
    }

    PosixAsyncIOContext *posixContext = dynamic_cast<PosixAsyncIOContext*>(context);
    if (!posixContext)
    {
        context->failed = true;
        context->retry = false;
        context->finished = true;
        if (context->userCallback)
        {
            context->userCallback(context->userData);
        }
        return;
    }

    struct aiocb *aiocbp = new struct aiocb;
    memset(aiocbp, 0, sizeof (struct aiocb));

    aiocbp->aio_fildes = fd;
    aiocbp->aio_buf = (void *)posixContext->dataBuffer;
    aiocbp->aio_nbytes = posixContext->dataBufferLen;
    aiocbp->aio_offset = posixContext->posOfBuffer;
    aiocbp->aio_sigevent.sigev_notify = SIGEV_THREAD;
    aiocbp->aio_sigevent.sigev_notify_function = asyncopfinished;
    aiocbp->aio_sigevent.sigev_value.sival_ptr = (void *)posixContext;
    posixContext->aiocb = aiocbp;
    if (aio_read(aiocbp))
    {
        posixContext->retry = (errno == EAGAIN);
        posixContext->failed = true;
        posixContext->finished = true;
        posixContext->aiocb = NULL;
        delete aiocbp;

        LOG_warn << "Async read failed at startup:" << errno;
        if (posixContext->userCallback)
        {
            posixContext->userCallback(posixContext->userData);
        }
    }
#endif
}

void PosixFileAccess::asyncsyswrite(AsyncIOContext *context)
{
#ifdef HAVE_AIO_RT
    if (!context)
    {
        return;
    }

    PosixAsyncIOContext *posixContext = dynamic_cast<PosixAsyncIOContext*>(context);
    if (!posixContext)
    {
        context->failed = true;
        context->retry = false;
        context->finished = true;
        if (context->userCallback)
        {
            context->userCallback(context->userData);
        }
        return;
    }

    struct aiocb *aiocbp = new struct aiocb;
    memset(aiocbp, 0, sizeof (struct aiocb));

    aiocbp->aio_fildes = fd;
    aiocbp->aio_buf = (void *)posixContext->dataBuffer;
    aiocbp->aio_nbytes = posixContext->dataBufferLen;
    aiocbp->aio_offset = posixContext->posOfBuffer;
    aiocbp->aio_sigevent.sigev_notify = SIGEV_THREAD;
    aiocbp->aio_sigevent.sigev_notify_function = asyncopfinished;
    aiocbp->aio_sigevent.sigev_value.sival_ptr = (void *)posixContext;
    posixContext->aiocb = aiocbp;

    if (aio_write(aiocbp))
    {
        posixContext->retry = (errno == EAGAIN);
        posixContext->failed = true;
        posixContext->finished = true;
        posixContext->aiocb = NULL;
        delete aiocbp;

        LOG_warn << "Async write failed at startup: " << errno;
        if (posixContext->userCallback)
        {
            posixContext->userCallback(posixContext->userData);
        }
    }
#endif
}

// update local name
void PosixFileAccess::updatelocalname(const LocalPath& name, bool force)
{
    if (force || !nonblocking_localname.empty())
    {
        nonblocking_localname = name;
    }
}

bool PosixFileAccess::sysread(byte* dst, unsigned len, m_off_t pos)
{
    retry = false;
#ifndef __ANDROID__
    return pread(fd, (char*)dst, len, pos) == len;
#else
    lseek64(fd, pos, SEEK_SET);
    return read(fd, (char*)dst, len) == len;
#endif
}

bool PosixFileAccess::fwrite(const byte* data, unsigned len, m_off_t pos)
{
    retry = false;
#ifndef __ANDROID__
    return pwrite(fd, data, len, pos) == len;
#else
    lseek64(fd, pos, SEEK_SET);
    return write(fd, data, len) == len;
#endif
}

bool PosixFileAccess::ftruncate()
{
    retry = false;

    // Truncate the file.
    if (::ftruncate(fd, 0x0) == 0)
    {
        // Set the file pointer back to the start.
        return lseek(fd, 0x0, SEEK_SET) == 0x0;
    }

    // Couldn't truncate the file.
    return false;
}

int PosixFileAccess::stealFileDescriptor()
{
    int toret = fd;
    fd = -1;
    return toret;
}

bool PosixFileAccess::fopen(const LocalPath& f, bool read, bool write, DirAccess* iteratingDir, bool, bool skipcasecheck)
{
    struct stat statbuf;

    retry = false;
    bool statok = false;
    if (iteratingDir) //reuse statbuf from iterator
    {
        statbuf = static_cast<PosixDirAccess *>(iteratingDir)->currentItemStat;
        mIsSymLink = S_ISLNK(statbuf.st_mode) || static_cast<PosixDirAccess *>(iteratingDir)->currentItemFollowedSymlink;
        statok = true;
    }

#ifdef USE_IOS
    const string fstr = adjustBasePath(f);
#else
    // use the existing string if it's not iOS, no need for a copy
    const string& fstr = adjustBasePath(f);
#endif


#ifdef __MACH__
    if (!write)
    {
        char resolved_path[PATH_MAX];
        if (memcmp(fstr.c_str(), ".", 2) && memcmp(fstr.c_str(), "..", 3)
                && (statok || !lstat(fstr.c_str(), &statbuf) )
                && !S_ISLNK(statbuf.st_mode)
                && realpath(fstr.c_str(), resolved_path) == resolved_path)
        {
            const char *fname;
            size_t fnamesize;
            if ((fname = strrchr(fstr.c_str(), '/')))
            {
                fname++;
                fnamesize = fstr.size() - (fname - fstr.c_str());
            }
            else
            {
                fname =  fstr.c_str();
                fnamesize = fstr.size();
            }
            fnamesize++;

            const char *rname;
            size_t rnamesize;
            if ((rname = strrchr(resolved_path, '/')))
            {
                rname++;
            }
            else
            {
                rname = resolved_path;
            }
            rnamesize = strlen(rname) + 1;

            if (!skipcasecheck)
            {
                if (rnamesize == fnamesize && memcmp(fname, rname, fnamesize))
                {
                    LOG_warn << "fopen failed due to invalid case: " << fstr;
                    return false;
                }
            }
        }
    }
#endif

#ifndef HAVE_FDOPENDIR
    if (!write)
    {
        // workaround for the very unfortunate platforms that do not implement fdopendir() (MacOS...)
        // (FIXME: can this be done without intruducing a race condition?)
        if ((dp = opendir(fstr.c_str())))
        {
            // stat & check if the directory is still a directory...
            if (stat(fstr.c_str(), &statbuf)
                || !S_ISDIR(statbuf.st_mode))
            {
                return false;
            }

            size = 0;
            mtime = statbuf.st_mtime;
            type = FOLDERNODE;
            fsid = (handle)statbuf.st_ino;
            fsidvalid = true;

            FileSystemAccess::captimestamp(&mtime);

            return true;
        }

        if (errno != ENOTDIR) return false;
    }
#endif

    if (!statok)
    {
         mIsSymLink = lstat(fstr.c_str(), &statbuf) == 0
                      && S_ISLNK(statbuf.st_mode);
        if (mIsSymLink && !PosixFileAccess::mFoundASymlink)
        {
            LOG_warn << "Enabling symlink check for syncup.";
            PosixFileAccess::mFoundASymlink = true;
        }

        if (mIsSymLink && !mFollowSymLinks)
        {
            statok = true; //we will use statbuf filled by lstat instead of fstat
        }
    }

    mode_t mode = 0;
    if (write)
    {
        mode = umask(0);
    }

#ifndef O_PATH
#define O_PATH 0
// Notice in systems were O_PATH is not available, open will fail for links with O_NOFOLLOW
#endif

    assert(fd < 0 && "There should be no opened file descriptor at this point");
    sysclose();
    // if mFollowSymLinks is true (open normally: it will open the targeted file/folder),
    // otherwise, get the file descriptor for symlinks in case it is a sync link (notice O_PATH invalidates read/only flags)
    if ((fd = open(fstr.c_str(), (!mFollowSymLinks && mIsSymLink) ? (O_PATH | O_NOFOLLOW) : (write ? (read ? O_RDWR : O_WRONLY | O_CREAT) : O_RDONLY) , defaultfilepermissions)) >= 0 || statok)
    {
        if (write)
        {
            umask(mode);
        }

        if (!statok)
        {
            statok = !fstat(fd, &statbuf);
        }

        if (statok)
        {
            #ifdef __MACH__
                //If creation time equal to kMagicBusyCreationDate
                if(statbuf.st_birthtimespec.tv_sec == -2082844800)
                {
                    LOG_debug << "File is busy: " << fstr;
                    retry = true;
                    return false;
                }
            #endif

            type = S_ISDIR(statbuf.st_mode) ? FOLDERNODE : FILENODE;
            size = (type == FILENODE || mIsSymLink) ? statbuf.st_size : 0;
            mtime = statbuf.st_mtime;
            // in the future we might want to add LINKNODE to type and set it here using S_ISLNK
            fsid = (handle)statbuf.st_ino;
            fsidvalid = true;

            FileSystemAccess::captimestamp(&mtime);

            return true;
        }

        close(fd);
    }
    else if (write)
    {
        umask(mode);
    }

    return false;
}

PosixFileSystemAccess::PosixFileSystemAccess()
{
    assert(sizeof(off_t) == 8);

#ifdef ENABLE_SYNC
    notifyerr = false;
    notifyfailed = false;
#endif
    notifyfd = -1;

    defaultfilepermissions = 0600;
    defaultfolderpermissions = 0700;

#ifdef USE_IOS
    if (!appbasepath)
    {
        string basepath;
        ios_appbasepath(&basepath);
        if (basepath.size())
        {
            basepath.append("/");
            appbasepath = strdup(basepath.c_str());
        }
    }
#endif

#ifdef USE_INOTIFY
    lastcookie = 0;
    lastlocalnode = NULL;
#endif
}

PosixFileSystemAccess::~PosixFileSystemAccess()
{
    if (notifyfd >= 0)
    {
        close(notifyfd);
    }
}

bool PosixFileSystemAccess::cwd(LocalPath& path) const
{
    return cwd_static(path);
}

bool PosixFileSystemAccess::cwd_static(LocalPath& path)
{
    string buf(128, '\0');

    while (!getcwd(&buf[0], buf.size()))
    {
        if (errno != ERANGE)
        {
            return false;
        }

        buf.resize(buf.size() << 1);
    }

    buf.resize(strlen(buf.c_str()));

    path = LocalPath::fromPlatformEncodedAbsolute(std::move(buf));

    return true;
}

// wake up from filesystem updates
void PosixFileSystemAccess::addevents(Waiter* w, int /*flags*/)
{
    if (notifyfd >= 0)
    {
        PosixWaiter* pw = (PosixWaiter*)w;

        MEGA_FD_SET(notifyfd, &pw->rfds);
        MEGA_FD_SET(notifyfd, &pw->ignorefds);

        pw->bumpmaxfd(notifyfd);
    }
}

// read all pending inotify events and queue them for processing
int PosixFileSystemAccess::checkevents(Waiter* w)
{
    int r = 0;
    if (notifyfd < 0)
    {
        return r;
    }
#ifdef ENABLE_SYNC
#ifdef USE_INOTIFY
    PosixWaiter* pw = (PosixWaiter*)w;
    string *ignore;

    if (MEGA_FD_ISSET(notifyfd, &pw->rfds))
    {
        char buf[sizeof(struct inotify_event) + NAME_MAX + 1];
        ssize_t p, l;
        inotify_event* in;
        wdlocalnode_map::iterator it;
        string localpath;

        while ((l = read(notifyfd, buf, sizeof buf)) > 0)
        {
            for (p = 0; p < l; p += offsetof(inotify_event, name) + in->len)
            {
                in = (inotify_event*)(buf + p);

                if (in->mask & (IN_Q_OVERFLOW | IN_UNMOUNT))
                {
                    notifyerr = true;
                }

// this flag was introduced in glibc 2.13 and Linux 2.6.36 (released October 20, 2010)
#ifndef IN_EXCL_UNLINK
#define IN_EXCL_UNLINK 0x04000000
#endif
                if (in->mask & (IN_CREATE | IN_DELETE | IN_MOVED_FROM
                              | IN_MOVED_TO | IN_CLOSE_WRITE | IN_EXCL_UNLINK))
                {
                    //if ((in->mask & (IN_CREATE | IN_ISDIR)) != IN_CREATE) //certain operations (e.g: QFile::copy, Qt 5.11) might produce IN_CREATE with no further IN_CLOSE_WRITE
                    {
                        it = wdnodes.find(in->wd);

                        if (it != wdnodes.end())
                        {
                            if (lastcookie && lastcookie != in->cookie)
                            {
                                ignore = &lastlocalnode->sync->dirnotify->ignore.localpath;
                                if (lastname.size() < ignore->size()
                                 || memcmp(lastname.c_str(), ignore->data(), ignore->size())
                                 || (lastname.size() > ignore->size()
                                  && lastname[ignore->size()] != LocalPath::localPathSeparator))
                                {
                                    // previous IN_MOVED_FROM is not followed by the
                                    // corresponding IN_MOVED_TO, so was actually a deletion
                                    LOG_debug << "Filesystem notification (deletion). Root: " << lastlocalnode->name << "   Path: " << lastname;
                                    lastlocalnode->sync->dirnotify->notify(DirNotify::DIREVENTS,
                                                                           lastlocalnode,
                                                                           LocalPath::fromPlatformEncodedRelative(lastname),
                                                                           false,
                                                                           false);

                                    r |= Waiter::NEEDEXEC;
                                }
                            }

                            if (in->mask & IN_MOVED_FROM)
                            {
                                // could be followed by the corresponding IN_MOVE_TO or not..
                                // retain in case it's not (in which case it's a deletion)
                                lastcookie = in->cookie;
                                lastlocalnode = it->second;
                                lastname = in->name;
                            }
                            else
                            {
                                lastcookie = 0;

                                ignore = &it->second->sync->dirnotify->ignore.localpath;
                                size_t insize = strlen(in->name);

                                if (insize < ignore->size()
                                 || memcmp(in->name, ignore->data(), ignore->size())
                                 || (insize > ignore->size()
                                  && in->name[ignore->size()] != LocalPath::localPathSeparator))
                                {
                                    LOG_debug << "Filesystem notification. Root: " << it->second->name << "   Path: " << in->name;
                                    it->second->sync->dirnotify->notify(DirNotify::DIREVENTS,
                                                                        it->second,
                                                                        LocalPath::fromPlatformEncodedRelative(std::string(in->name, insize)),
                                                                        false,
                                                                        false);

                                    r |= Waiter::NEEDEXEC;
                                }
                            }
                        }
                    }
                }
            }
        }

        // this assumes that corresponding IN_MOVED_FROM / IN_MOVED_FROM pairs are never notified separately
        if (lastcookie)
        {
            ignore = &lastlocalnode->sync->dirnotify->ignore.localpath;

            if (lastname.size() < ignore->size()
             || memcmp(lastname.c_str(), ignore->data(), ignore->size())
             || (lastname.size() > ignore->size()
              && lastname[ignore->size()] != LocalPath::localPathSeparator))
            {
                LOG_debug << "Filesystem notification. Root: " << lastlocalnode->name << "   Path: " << lastname;
                lastlocalnode->sync->dirnotify->notify(DirNotify::DIREVENTS,
                                                       lastlocalnode,
                                                       LocalPath::fromPlatformEncodedRelative(lastname),
                                                       false,
                                                       false);

                r |= Waiter::NEEDEXEC;
            }

            lastcookie = 0;
        }
    }
#endif
#endif
    return r;
}

// no legacy DOS garbage here...
bool PosixFileSystemAccess::getsname(const LocalPath&, LocalPath&) const
{
    return false;
}

bool PosixFileSystemAccess::renamelocal(const LocalPath& oldname, const LocalPath& newname, bool override)
{
#ifdef USE_IOS
    const string oldnamestr = adjustBasePath(oldname);
    const string newnamestr = adjustBasePath(newname);
#else
    // use the existing string if it's not iOS, no need for a copy
    const string& oldnamestr = adjustBasePath(oldname);
    const string& newnamestr = adjustBasePath(newname);
#endif

    bool existingandcare = !override && (0 == access(newnamestr.c_str(), F_OK));
    if (!existingandcare && !rename(oldnamestr.c_str(), newnamestr.c_str()))
    {
        LOG_verbose << "Successfully moved file: " << oldnamestr << " to " << newnamestr;
        return true;
    }

    target_exists = existingandcare  || errno == EEXIST || errno == EISDIR || errno == ENOTEMPTY || errno == ENOTDIR;
    target_name_too_long = errno == ENAMETOOLONG;
    transient_error = !existingandcare && (errno == ETXTBSY || errno == EBUSY);

    int e = errno;
    if (e != EEXIST  || !skip_targetexists_errorreport)
    {
        LOG_warn << "Unable to move file: " << oldnamestr << " to " << newnamestr << ". Error code: " << e;
    }
    return false;
}

bool PosixFileSystemAccess::copylocal(const LocalPath& oldname, const LocalPath& newname, m_time_t mtime)
{
#ifdef USE_IOS
    const string oldnamestr = adjustBasePath(oldname);
    const string newnamestr = adjustBasePath(newname);
#else
    // use the existing string if it's not iOS, no need for a copy
    const string& oldnamestr = adjustBasePath(oldname);
    const string& newnamestr = adjustBasePath(newname);
#endif

    int sfd, tfd;
    ssize_t t = -1;

#ifdef HAVE_SENDFILE
    // Linux-specific - kernel 2.6.33+ required
    if ((sfd = open(oldnamestr.c_str(), O_RDONLY | O_DIRECT)) >= 0)
    {
        LOG_verbose << "Copying via sendfile";
        mode_t mode = umask(0);
        if ((tfd = open(newnamestr.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_DIRECT, defaultfilepermissions)) >= 0)
        {
            umask(mode);
            while ((t = sendfile(tfd, sfd, NULL, 1024 * 1024 * 1024)) > 0);
#else
    char buf[16384];

    if ((sfd = open(oldnamestr.c_str(), O_RDONLY)) >= 0)
    {
        LOG_verbose << "Copying via read/write";
        mode_t mode = umask(0);
        if ((tfd = open(newnamestr.c_str(), O_WRONLY | O_CREAT | O_TRUNC, defaultfilepermissions)) >= 0)
        {
            umask(mode);
            while (((t = read(sfd, buf, sizeof buf)) > 0) && write(tfd, buf, t) == t);
#endif
            close(tfd);
        }
        else
        {
            umask(mode);
            target_exists = errno == EEXIST;
            target_name_too_long = errno == ENAMETOOLONG;
            transient_error = errno == ETXTBSY || errno == EBUSY;

            int e = errno;
            LOG_warn << "Unable to copy file. Error code: " << e;
        }

        close(sfd);
    }

    if (!t)
    {
#ifdef ENABLE_SYNC
        t = !setmtimelocal(newname, mtime);
#else
        // fails in setmtimelocal are allowed in non sync clients.
        setmtimelocal(newname, mtime);
#endif
    }
    else
    {
        int e = errno;
        LOG_debug << "Unable to copy file: " << oldnamestr << " to " << newnamestr << ". Error code: " << e;
    }

    return !t;
}

bool PosixFileSystemAccess::unlinklocal(const LocalPath& name)
{
    if (!unlink(adjustBasePath(name).c_str()))
    {
        return true;
    }

    transient_error = errno == ETXTBSY || errno == EBUSY;

    return false;
}

// delete all files, folders and symlinks contained in the specified folder
// (does not recurse into mounted devices)
void PosixFileSystemAccess::emptydirlocal(const LocalPath& nameParam, dev_t basedev)
{
    LocalPath name = nameParam;

    DIR* dp;
    dirent* d;
    int removed;
    struct stat statbuf;
#ifdef USE_IOS
    const string namestr = adjustBasePath(name);
#else
    const string& namestr = adjustBasePath(name);
#endif

    if (!basedev)
    {
        if (lstat(namestr.c_str(), &statbuf)
            || !S_ISDIR(statbuf.st_mode)
            || S_ISLNK(statbuf.st_mode))
        {
            return;
        }

        basedev = statbuf.st_dev;
    }

    if ((dp = opendir(namestr.c_str())))
    {
        for (;;)
        {
            removed = 0;

            while ((d = readdir(dp)))
            {
                if (d->d_type != DT_DIR
                 || *d->d_name != '.'
                 || (d->d_name[1] && (d->d_name[1] != '.' || d->d_name[2])))
                {
                    ScopedLengthRestore restore(name);

                    name.appendWithSeparator(LocalPath::fromPlatformEncodedRelative(d->d_name), true);

#ifdef USE_IOS
                    const string nameStr = adjustBasePath(name);
#else
                    // use the existing string if it's not iOS, no need for a copy
                    const string& nameStr = adjustBasePath(name);
#endif
                    if (!lstat(nameStr.c_str(), &statbuf))
                    {
                        if (!S_ISLNK(statbuf.st_mode) && S_ISDIR(statbuf.st_mode) && statbuf.st_dev == basedev)
                        {
                            emptydirlocal(name, basedev);
                            removed |= !rmdir(nameStr.c_str());
                        }
                        else
                        {
                            removed |= !unlink(nameStr.c_str());
                        }
                    }
                }
            }

            if (!removed)
            {
                break;
            }

            rewinddir(dp);
        }

        closedir(dp);
    }
}

int PosixFileSystemAccess::getdefaultfilepermissions()
{
    return defaultfilepermissions;
}

void PosixFileSystemAccess::setdefaultfilepermissions(int permissions)
{
    defaultfilepermissions = permissions | 0600;
}

int PosixFileSystemAccess::getdefaultfolderpermissions()
{
    return defaultfolderpermissions;
}

void PosixFileSystemAccess::setdefaultfolderpermissions(int permissions)
{
    defaultfolderpermissions = permissions | 0700;
}

bool PosixFileSystemAccess::rmdirlocal(const LocalPath& name)
{
    emptydirlocal(name);

    if (!rmdir(adjustBasePath(name).c_str()))
    {
        return true;
    }

    transient_error = errno == ETXTBSY || errno == EBUSY;

    return false;
}

bool PosixFileSystemAccess::mkdirlocal(const LocalPath& name, bool, bool logAlreadyExistsError)
{
#ifdef USE_IOS
    const string nameStr = adjustBasePath(name);
#else
    // use the existing string if it's not iOS, no need for a copy
    const string& nameStr = adjustBasePath(name);
#endif

    mode_t mode = umask(0);
    bool r = !mkdir(nameStr.c_str(), defaultfolderpermissions);
    umask(mode);

    if (!r)
    {
        target_exists = errno == EEXIST;
        target_name_too_long = errno == ENAMETOOLONG;

        if (target_exists)
        {
            if (logAlreadyExistsError)
            {
                LOG_debug << "Failed to create local directory: " << nameStr << " (already exists)";
            }
        }
        else
        {
            LOG_err << "Error creating local directory: " << nameStr << " errno: " << errno;
        }
        transient_error = errno == ETXTBSY || errno == EBUSY;
    }

    return r;
}

bool PosixFileSystemAccess::setmtimelocal(const LocalPath& name, m_time_t mtime)
{
#ifdef USE_IOS
    const string nameStr = adjustBasePath(name);
#else
    // use the existing string if it's not iOS, no need for a copy
    const string& nameStr = adjustBasePath(name);
#endif

    struct utimbuf times = { (time_t)mtime, (time_t)mtime };

    bool success = !utime(nameStr.c_str(), &times);
    if (!success)
    {
        LOG_err << "Error setting mtime: " << nameStr <<" mtime: "<< mtime << " errno: " << errno;
        transient_error = errno == ETXTBSY || errno == EBUSY;
    }

    return success;
}

bool PosixFileSystemAccess::chdirlocal(LocalPath& name) const
{
    return !chdir(adjustBasePath(name).c_str());
}

// return lowercased ASCII file extension, including the . separator
bool PosixFileSystemAccess::getextension(const LocalPath& filename, std::string &extension) const
{
    const std::string* str = &filename.localpath;
    const char* ptr = str->data() + str->size();
    char c;

    for (unsigned i = 0; i < str->size(); i++)
    {
        if (*--ptr == '.')
        {
            extension.reserve(i+1);

            unsigned j = 0;
            for (; j <= i; j++)
            {
                if (*ptr < '.' || *ptr > 'z') return false;

                c = *(ptr++);

                // tolower()
                if (c >= 'A' && c <= 'Z') c |= ' ';

                extension.push_back(c);
            }
            return true;
        }
    }

    return false;
}

bool PosixFileSystemAccess::expanselocalpath(LocalPath& source, LocalPath& destination)
{
    // Sanity.
    assert(!source.empty());

    // At worst, the destination mirrors the source.
    destination = source;

    // Are we dealing with a relative path?
    if (!source.isAbsolute())
    {
        // Sanity.
        assert(source.localpath[0] != '/');

        // Retrieve current working directory.
        if (!cwd(destination))
        {
            return false;
        }

        // Compute absolute path.
        destination.appendWithSeparator(source, false);
    }

    // Sanity.
    assert(destination.isAbsolute());
    assert(destination.localpath[0] == '/');

    // Canonicalize the path.
    char buffer[PATH_MAX];

    if (!realpath(destination.localpath.c_str(), buffer))
        return destination = source, false;

    destination.localpath.assign(buffer);

    return true;
}

#ifdef __linux__
string &ltrimEtcProperty(string &s, const char &c)
{
    size_t pos = s.find_first_not_of(c);
    s = s.substr(pos == string::npos ? s.length() : pos, s.length());
    return s;
}

string &rtrimEtcProperty(string &s, const char &c)
{
    size_t pos = s.find_last_not_of(c);
    if (pos != string::npos)
    {
        pos++;
    }
    s = s.substr(0, pos);
    return s;
}

string &trimEtcproperty(string &what)
{
    rtrimEtcProperty(what,' ');
    ltrimEtcProperty(what,' ');
    if (what.size() > 1)
    {
        if (what[0] == '\'' || what[0] == '"')
        {
            rtrimEtcProperty(what, what[0]);
            ltrimEtcProperty(what, what[0]);
        }
    }
    return what;
}

string getPropertyFromEtcFile(const char *configFile, const char *propertyName)
{
    ifstream infile(configFile);
    string line;

    while (getline(infile, line))
    {
        if (line.length() > 0 && line[0] != '#')
        {
            if (!strlen(propertyName)) //if empty return first line
            {
                return trimEtcproperty(line);
            }
            string key, value;
            size_t pos = line.find("=");
            if (pos != string::npos && ((pos + 1) < line.size()))
            {
                key = line.substr(0, pos);
                rtrimEtcProperty(key, ' ');

                if (!strcmp(key.c_str(), propertyName))
                {
                    value = line.substr(pos + 1);
                    return trimEtcproperty(value);
                }
            }
        }
    }

    return string();
}

string getDistro()
{
    string distro;
    distro = getPropertyFromEtcFile("/etc/lsb-release", "DISTRIB_ID");
    if (!distro.size())
    {
        distro = getPropertyFromEtcFile("/etc/os-release", "ID");
    }
    if (!distro.size())
    {
        distro = getPropertyFromEtcFile("/etc/redhat-release", "");
    }
    if (!distro.size())
    {
        distro = getPropertyFromEtcFile("/etc/debian-release", "");
    }
    if (distro.size() > 20)
    {
        distro = distro.substr(0, 20);
    }
    transform(distro.begin(), distro.end(), distro.begin(), ::tolower);
    return distro;
}

string getDistroVersion()
{
    string version;
    version = getPropertyFromEtcFile("/etc/lsb-release", "DISTRIB_RELEASE");
    if (!version.size())
    {
        version = getPropertyFromEtcFile("/etc/os-release", "VERSION_ID");
    }
    if (version.size() > 10)
    {
        version = version.substr(0, 10);
    }
    transform(version.begin(), version.end(), version.begin(), ::tolower);
    return version;
}
#endif

void PosixFileSystemAccess::osversion(string* u, bool /*includeArchExtraInfo*/) const
{
#ifdef __linux__
    string distro = getDistro();
    if (distro.size())
    {
        u->append(distro);
        string distroversion = getDistroVersion();
        if (distroversion.size())
        {
            u->append(" ");
            u->append(distroversion);
            u->append("/");
        }
        else
        {
            u->append("/");
        }
    }
#endif

    utsname uts;

    if (!uname(&uts))
    {
        u->append(uts.sysname);
        u->append(" ");
        u->append(uts.release);
        u->append(" ");
        u->append(uts.machine);
    }
}

void PosixFileSystemAccess::statsid(string *id) const
{
#ifdef __ANDROID__
    if (!MEGAjvm)
    {
        LOG_err << "No JVM found";
        return;
    }

    try
    {
        JNIEnv *env;
        MEGAjvm->AttachCurrentThread(&env, NULL);
        jclass appGlobalsClass = env->FindClass("android/app/AppGlobals");
        if (!appGlobalsClass)
        {
            env->ExceptionClear();
            LOG_err << "Failed to get android/app/AppGlobals";
            MEGAjvm->DetachCurrentThread();
            return;
        }

        jmethodID getInitialApplicationMID = env->GetStaticMethodID(appGlobalsClass,"getInitialApplication","()Landroid/app/Application;");
        if (!getInitialApplicationMID)
        {
            env->ExceptionClear();
            LOG_err << "Failed to get getInitialApplication()";
            MEGAjvm->DetachCurrentThread();
            return;
        }

        jobject context = env->CallStaticObjectMethod(appGlobalsClass, getInitialApplicationMID);
        if (!context)
        {
            LOG_err << "Failed to get context";
            MEGAjvm->DetachCurrentThread();
            return;
        }

        jclass contextClass = env->GetObjectClass(context);
        if (!contextClass)
        {
            LOG_err << "Failed to get context class";
            MEGAjvm->DetachCurrentThread();
            return;
        }

        jmethodID getContentResolverMID = env->GetMethodID(contextClass, "getContentResolver", "()Landroid/content/ContentResolver;");
        if (!getContentResolverMID)
        {
            env->ExceptionClear();
            LOG_err << "Failed to get getContentResolver()";
            MEGAjvm->DetachCurrentThread();
            return;
        }

        jobject contentResolver = env->CallObjectMethod(context, getContentResolverMID);
        if (!contentResolver)
        {
            LOG_err << "Failed to get ContentResolver";
            MEGAjvm->DetachCurrentThread();
            return;
        }

        jclass settingsSecureClass = env->FindClass("android/provider/Settings$Secure");
        if (!settingsSecureClass)
        {
            env->ExceptionClear();
            LOG_err << "Failed to get Settings.Secure class";
            MEGAjvm->DetachCurrentThread();
            return;
        }

        jmethodID getStringMID = env->GetStaticMethodID(settingsSecureClass, "getString", "(Landroid/content/ContentResolver;Ljava/lang/String;)Ljava/lang/String;");
        if (!getStringMID)
        {
            env->ExceptionClear();
            LOG_err << "Failed to get getString()";
            MEGAjvm->DetachCurrentThread();
            return;
        }

        jstring idStr = (jstring) env->NewStringUTF("android_id");
        if (!idStr)
        {
            LOG_err << "Failed to get idStr";
            MEGAjvm->DetachCurrentThread();
            return;
        }

        jstring androidId = (jstring) env->CallStaticObjectMethod(settingsSecureClass, getStringMID, contentResolver, idStr);
        if (!androidId)
        {
            LOG_err << "Failed to get android_id";
            env->DeleteLocalRef(idStr);
            MEGAjvm->DetachCurrentThread();
            return;
        }

        const char *androidIdString = env->GetStringUTFChars(androidId, NULL);
        if (!androidIdString)
        {
            LOG_err << "Failed to get android_id bytes";
            env->DeleteLocalRef(idStr);
            MEGAjvm->DetachCurrentThread();
            return;
        }

        id->append(androidIdString);
        env->DeleteLocalRef(idStr);
        env->ReleaseStringUTFChars(androidId, androidIdString);
        MEGAjvm->DetachCurrentThread();
    }
    catch (...)
    {
        try
        {
            MEGAjvm->DetachCurrentThread();
        }
        catch (...) { }
    }
#elif TARGET_OS_IPHONE
#ifdef USE_IOS
    ios_statsid(id);
#endif
#elif defined(__MACH__)
    uuid_t uuid;
    struct timespec wait = {1, 0};
    if (gethostuuid(uuid, &wait))
    {
        return;
    }

    char uuid_str[37];
    uuid_unparse(uuid, uuid_str);
    id->append(uuid_str);
#else
    int fd = open("/etc/machine-id", O_RDONLY);
    if (fd < 0)
    {
        fd = open("/var/lib/dbus/machine-id", O_RDONLY);
        if (fd < 0)
        {
            return;
        }
    }

    char buff[512];
    ssize_t len = read(fd, buff, 512);
    close(fd);

    if (len <= 0)
    {
        return;
    }

    if (buff[len - 1] == '\n')
    {
        len--;
    }

    if (len > 0)
    {
        id->append(buff, len);
    }
#endif
}

#ifdef ENABLE_SYNC

PosixDirNotify::PosixDirNotify(const LocalPath& localbasepath, const LocalPath& ignore, Sync* s)
  : DirNotify(localbasepath, ignore, s)
{
#ifdef USE_INOTIFY
    setFailed(0, "");
#endif

#ifdef __MACH__
    setFailed(0, "");
#endif

    fsaccess = NULL;
}

void PosixDirNotify::addnotify(LocalNode* l, const LocalPath& path)
{
#ifdef USE_INOTIFY
    int wd;

    wd = inotify_add_watch(fsaccess->notifyfd, path.localpath.c_str(),
                           IN_CREATE | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO
                           | IN_CLOSE_WRITE | IN_EXCL_UNLINK | IN_ONLYDIR);

    if (wd >= 0)
    {
        l->dirnotifytag = (handle)wd;
        fsaccess->wdnodes[wd] = l;
    }
    else
    {
        LOG_warn << "Unable to addnotify path: " <<  path.localpath.c_str() << ". Error code: " << errno;
    }
#endif
}

void PosixDirNotify::delnotify(LocalNode* l)
{
#ifdef USE_INOTIFY
    if (fsaccess->wdnodes.erase((int)(long)l->dirnotifytag))
    {
        inotify_rm_watch(fsaccess->notifyfd, (int)l->dirnotifytag);
    }
#endif
}

fsfp_t PosixFileSystemAccess::fsFingerprint(const LocalPath& path) const
{
    struct statfs statfsbuf;

    // FIXME: statfs() does not really do what we want.
    if (statfs(path.localpath.c_str(), &statfsbuf))
    {
        return 0;
    }
    fsfp_t tmp;
    memcpy(&tmp, &statfsbuf.f_fsid, sizeof(fsfp_t));
    return tmp+1;
}

bool PosixFileSystemAccess::fsStableIDs(const LocalPath& path) const
{
    FileSystemType type;

    if (!getlocalfstype(path, type))
    {
        LOG_err << "Failed to get filesystem type. Error code:"
                << errno;

        return true;
    }

    return type != FS_EXFAT
           && type != FS_FAT32
           && type != FS_FUSE;
}

bool PosixFileSystemAccess::initFilesystemNotificationSystem()
{
#ifdef USE_INOTIFY
    notifyfd = inotify_init1(IN_NONBLOCK);
    notifyfailed = notifyfd < 0;
#endif // USE_INOTIFY

    return notifyfd >= 0;
}

#endif // ENABLE_SYNC

bool PosixFileSystemAccess::hardLink(const LocalPath& source, const LocalPath& target)
{
    using StringType = decltype(adjustBasePath(source));

    StringType sourcePath = adjustBasePath(source);
    StringType targetPath = adjustBasePath(target);

    if (link(sourcePath.c_str(), targetPath.c_str()))
    {
        LOG_warn << "Unable to create hard link from "
                 << sourcePath
                 << " to "
                 << targetPath
                 << ". Error code was: "
                 << errno;

        return false;
    }

    return true;
}

std::unique_ptr<FileAccess> PosixFileSystemAccess::newfileaccess(bool followSymLinks)
{
    return std::unique_ptr<FileAccess>{new PosixFileAccess{waiter, defaultfilepermissions, followSymLinks}};
}

unique_ptr<DirAccess>  PosixFileSystemAccess::newdiraccess()
{
    return unique_ptr<DirAccess>(new PosixDirAccess());
}

#ifdef ENABLE_SYNC
DirNotify* PosixFileSystemAccess::newdirnotify(const LocalPath& localpath, const LocalPath& ignore, Waiter*, LocalNode* syncroot)
{
    PosixDirNotify* dirnotify = new PosixDirNotify(localpath, ignore, syncroot->sync);

    dirnotify->fsaccess = this;

    return dirnotify;
}
#endif

bool PosixFileSystemAccess::issyncsupported(const LocalPath& localpathArg, bool& isnetwork, SyncError& syncError, SyncWarning& syncWarning)
{
    // What filesystem is hosting our sync?
    auto type = getlocalfstype(localpathArg);

    // Is it a known network filesystem?
    isnetwork = isNetworkFilesystem(type);

    syncError = NO_SYNC_ERROR;
    syncWarning = NO_SYNC_WARNING;

    return true;
}

bool PosixFileSystemAccess::getlocalfstype(const LocalPath& path, FileSystemType& type) const
{
#if defined(__linux__) || defined(__ANDROID__)
    struct statfs statbuf;

    if (!statfs(path.localpath.c_str(), &statbuf))
    {
        switch (statbuf.f_type)
        {
        case EXT2_SUPER_MAGIC:
            type = FS_EXT;
            break;
        case MSDOS_SUPER_MAGIC:
            type = FS_FAT32;
            break;
        case HFS_SUPER_MAGIC:
        case HFSPLUS_SUPER_MAGIC:
            type = FS_HFS;
            break;
        case NTFS_SB_MAGIC:
            type = FS_NTFS;
            break;
#if defined(__ANDROID__)
        case F2FS_SUPER_MAGIC:
            type = FS_F2FS;
            break;
        case FUSEBLK_SUPER_MAGIC:
        case FUSECTL_SUPER_MAGIC:
            type = FS_FUSE;
            break;
        case SDCARDFS_SUPER_MAGIC:
            type = FS_SDCARDFS;
            break;
#endif /* __ANDROID__ */
        case XFS_SUPER_MAGIC:
            type = FS_XFS;
            break;
        case CIFS_MAGIC_NUMBER:
            type = FS_CIFS;
            break;
        case NFS_SUPER_MAGIC:
            type = FS_NFS;
            break;
        case SMB_SUPER_MAGIC:
            type = FS_SMB;
            break;
        case SMB2_MAGIC_NUMBER:
            type = FS_SMB2;
            break;
        default:
            type = FS_UNKNOWN;
            break;
        }

        return true;
    }
#endif /* __linux__ || __ANDROID__ */

#if defined(__APPLE__) || defined(USE_IOS)
    static const map<string, FileSystemType> filesystemTypes = {
        {"apfs",        FS_APFS},
        {"exfat",       FS_EXFAT},
        {"hfs",         FS_HFS},
        {"msdos",       FS_FAT32},
        {"nfs",         FS_NFS},
        {"ntfs",        FS_NTFS}, // Apple NTFS
        {"smbfs",       FS_SMB},
        {"tuxera_ntfs", FS_NTFS}, // Tuxera NTFS for Mac
        {"ufsd_NTFS",   FS_NTFS}  // Paragon NTFS for Mac
    }; /* filesystemTypes */

    struct statfs statbuf;

    if (!statfs(path.localpath.c_str(), &statbuf))
    {
        auto it = filesystemTypes.find(statbuf.f_fstypename);

        if (it != filesystemTypes.end())
        {
            type = it->second;
            return true;
        }

        type = FS_UNKNOWN;
        return true;
    }
#endif /* __APPLE__ || USE_IOS */

    type = FS_UNKNOWN;
    return false;
}

bool PosixDirAccess::dopen(LocalPath* path, FileAccess* f, bool doglob)
{
    if (doglob)
    {
        if (glob(adjustBasePath(*path).c_str(), GLOB_NOSORT, NULL, &globbuf))
        {
            return false;
        }

        globbing = true;
        globindex = 0;

        return true;
    }

    if (f)
    {
#ifdef HAVE_FDOPENDIR
        dp = fdopendir(((PosixFileAccess*)f)->stealFileDescriptor());
#else
        dp = ((PosixFileAccess*)f)->dp;
        ((PosixFileAccess*)f)->dp = NULL;
#endif
    }
    else
    {
        dp = opendir(adjustBasePath(*path).c_str());
    }

    return dp != NULL;
}

bool PosixDirAccess::dnext(LocalPath& path, LocalPath& name, bool followsymlinks, nodetype_t* type)
{
    if (globbing)
    {
        struct stat &statbuf = currentItemStat;

        while (globindex < globbuf.gl_pathc)
        {
            if (followsymlinks ? !stat(globbuf.gl_pathv[globindex], &statbuf) : !lstat(globbuf.gl_pathv[globindex], &currentItemStat))
            {
                if (S_ISREG(statbuf.st_mode) || S_ISDIR(statbuf.st_mode)) // this evaluates false for symlinks
                //if (statbuf.st_mode & (S_IFREG | S_IFDIR)) //TODO: use this when symlinks are supported
                {
                    name = LocalPath::fromPlatformEncodedAbsolute(globbuf.gl_pathv[globindex]);
                    *type = (statbuf.st_mode & S_IFREG) ? FILENODE : FOLDERNODE;

                    globindex++;
                    return true;
                }
            }
            globindex++;
        }

        return false;
    }

    dirent* d;
    struct stat &statbuf = currentItemStat;

    while ((d = readdir(dp)))
    {
        ScopedLengthRestore restore(path);

        if (*d->d_name != '.' || (d->d_name[1] && (d->d_name[1] != '.' || d->d_name[2])))
        {
            path.appendWithSeparator(LocalPath::fromPlatformEncodedRelative(d->d_name), true);

#ifdef USE_IOS
            const string pathStr = adjustBasePath(path);
#else
            // use the existing string if it's not iOS, no need for a copy
            const string& pathStr = adjustBasePath(path);
#endif

            bool statOk = !lstat(pathStr.c_str(), &statbuf);
            if (followsymlinks && statOk && S_ISLNK(statbuf.st_mode))
            {
                currentItemFollowedSymlink = true;
                statOk = !stat(pathStr.c_str(), &statbuf);
            }
            else
            {
                currentItemFollowedSymlink = false;
            }

            if (statOk)
            {
                if (S_ISREG(statbuf.st_mode) || S_ISDIR(statbuf.st_mode)) // this evalves false for symlinks
                //if (statbuf.st_mode & (S_IFREG | S_IFDIR)) //TODO: use this when symlinks are supported
                {
                    name = LocalPath::fromPlatformEncodedRelative(d->d_name);

                    if (type)
                    {
                        *type = S_ISREG(statbuf.st_mode) ? FILENODE : FOLDERNODE;
                    }

                    return true;
                }
            }
        }
    }

    return false;
}

PosixDirAccess::PosixDirAccess()
{
    dp = NULL;
    globbing = false;
    memset(&globbuf, 0, sizeof(glob_t));
    globindex = 0;
}

PosixDirAccess::~PosixDirAccess()
{
    if (dp)
    {
        closedir(dp);
    }

    if (globbing)
    {
        globfree(&globbuf);
    }
}

bool isReservedName(const string&, nodetype_t)
{
    return false;
}

} // namespace
