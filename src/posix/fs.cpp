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
#ifndef __APPLE__
#include <mntent.h>
#endif // ! __APPLE__

#include "mega.h"
#include "mega/scoped_helpers.h"

#include <sys/ioctl.h>
#include <sys/resource.h>
#include <sys/statvfs.h>
#include <sys/types.h>
#include <sys/utsname.h>
#ifdef TARGET_OS_MAC
#include "mega/osx/osxutils.h"
#endif

#ifdef __ANDROID__
#include <mega/android/androidFileSystem.h>

#include <jni.h>
extern JavaVM* MEGAjvm;
extern jclass fileWrapper;
#endif

#if defined(__MACH__) && !(TARGET_OS_IPHONE)
#include <uuid/uuid.h>
#endif

#ifdef __linux__

#ifndef __ANDROID__
#include <linux/magic.h>
#endif /* ! __ANDROID__ */

#include <sys/sysmacros.h>
#include <sys/vfs.h>

#ifndef FUSEBLK_SUPER_MAGIC
#define FUSEBLK_SUPER_MAGIC 0x65735546ul
#endif /* ! FUSEBLK_SUPER_MAGIC */

#ifndef FUSECTL_SUPER_MAGIC
#define FUSECTL_SUPER_MAGIC 0x65735543ul
#endif /* ! FUSECTL_SUPER_MAGIC */

#ifndef HFS_SUPER_MAGIC
#define HFS_SUPER_MAGIC 0x4244ul
#endif /* ! HFS_SUPER_MAGIC */

#ifndef HFSPLUS_SUPER_MAGIC
#define HFSPLUS_SUPER_MAGIC 0x482Bul
#endif /* ! HFSPLUS_SUPER_MAGIC */

#ifndef NTFS_SB_MAGIC
#define NTFS_SB_MAGIC 0x5346544Eul
#endif /* ! NTFS_SB_MAGIC */

#ifndef SDCARDFS_SUPER_MAGIC
#define SDCARDFS_SUPER_MAGIC 0x5DCA2DF5ul
#endif /* ! SDCARDFS_SUPER_MAGIC */

#ifndef F2FS_SUPER_MAGIC
#define F2FS_SUPER_MAGIC 0xF2F52010ul
#endif /* ! F2FS_SUPER_MAGIC */

#ifndef XFS_SUPER_MAGIC
#define XFS_SUPER_MAGIC 0x58465342ul
#endif /* ! XFS_SUPER_MAGIC */

#ifndef CIFS_MAGIC_NUMBER
#define CIFS_MAGIC_NUMBER 0xFF534D42ul
#endif // ! CIFS_MAGIC_NUMBER

#ifndef NFS_SUPER_MAGIC
#define NFS_SUPER_MAGIC 0x6969ul
#endif // ! NFS_SUPER_MAGIC

#ifndef SMB_SUPER_MAGIC
#define SMB_SUPER_MAGIC 0x517Bul
#endif // ! SMB_SUPER_MAGIC

#ifndef SMB2_MAGIC_NUMBER
#define SMB2_MAGIC_NUMBER 0xfe534d42ul
#endif // ! SMB2_MAGIC_NUMBER

#endif /* __linux__ */

#if defined(__APPLE__) || defined(USE_IOS)
#include <sys/mount.h>
#include <sys/param.h>
#endif /* __APPLE__ || USE_IOS */

namespace mega {

namespace detail {

#ifdef USE_IOS

static std::string GetBasePath()
{
    static std::string    basePath;
    static std::once_flag onceOnly;

    // Compute base path as necessary.
    std::call_once(onceOnly, []() {
        ios_appbasepath(&basePath);
        basePath.append("/");
    });

    // Return base path to caller.
    return basePath;
}

AdjustBasePathResult adjustBasePath(const LocalPath& path)
{
    // Get our hands on the app's base path.
    auto basePath = GetBasePath();

    // No base path.
    if (basePath.empty())
        return path.asPlatformEncoded(false);

    // Path is absolute.
    if (path.beginsWithSeparator())
        return path.asPlatformEncoded(false);

    // Compute absolute path.
    basePath.append(path.asPlatformEncoded(false));

    // Return absolute path to caller.
    return basePath;
}

#else // USE_IOS

AdjustBasePathResult adjustBasePath(const LocalPath& path)
{
    return path.asPlatformEncoded(false);
}

#endif // ! USE_IOS

} // detail

using namespace std;

// Make AdjustBasePath visible in current scope.
using detail::adjustBasePath;
using detail::AdjustBasePathResult;

bool PosixFileAccess::mFoundASymlink = false;

void FileSystemAccess::setMinimumDirectoryPermissions(int permissions)
{
    mMinimumDirectoryPermissions = permissions & 07777;
}

void FileSystemAccess::setMinimumFilePermissions(int permissions)
{
    mMinimumFilePermissions = permissions & 07777;
}

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
    fclose();
}

bool PosixFileAccess::sysstat(m_time_t* mtime, m_off_t* size, FSLogging)
{
    AdjustBasePathResult nameStr = adjustBasePath(nonblocking_localname);

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

bool PosixFileAccess::sysopen(bool, FSLogging fsl)
{
    assert(fd < 0 && "There should be no opened file descriptor at this point");
    errorcode = 0;
    if (fd >= 0)
    {
        sysclose();
    }

    assert(mFollowSymLinks); //Notice: symlinks are not considered here for the moment,
    // this is ok: this is not called with mFollowSymLinks = false, but from transfers doio.
    // When fully supporting symlinks, this might need to be reassessed

    fd = open(adjustBasePath(nonblocking_localname).c_str(), O_RDONLY);
    if (fd < 0)
    {
        errorcode = errno;
        if (fsl.doLog(errorcode))
        {
            LOG_err << "Failed to open('" << adjustBasePath(nonblocking_localname) << "'): error " << errorcode << ": " << PosixFileSystemAccess::getErrorMessage(errorcode);
        }
    }

    return fd >= 0;
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

void PosixFileAccess::asyncsysopen([[maybe_unused]] AsyncIOContext *context)
{
#ifdef HAVE_AIO_RT
    context->failed = !fopen(context->openPath, context->access & AsyncIOContext::ACCESS_READ,
                             context->access & AsyncIOContext::ACCESS_WRITE, FSLogging::logOnError);
    if (context->failed)
    {
        LOG_err << "Failed to fopen('" << context->openPath << "'): error " << errorcode << ": " << PosixFileSystemAccess::getErrorMessage(errorcode);
    }
    context->retry = retry;
    context->finished = true;
    if (context->userCallback)
    {
        context->userCallback(context->userData);
    }
#endif
}

void PosixFileAccess::asyncsysread([[maybe_unused]] AsyncIOContext *context)
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

void PosixFileAccess::asyncsyswrite([[maybe_unused]] AsyncIOContext *context)
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

void PosixFileAccess::fclose()
{
#ifndef HAVE_FDOPENDIR
    if (dp)
        closedir(dp);

    dp = nullptr;
#endif // HAVE_FDOPENDIR

    if (fd >= 0)
        close(fd);

    fd = -1;
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

bool PosixFileAccess::fstat(m_time_t& modified, m_off_t& size)
{
    struct stat attributes;

    retry = false;

    if (::fstat(fd, &attributes))
    {
        errorcode = errno;

        LOG_err << "Unable to stat descriptor: "
                << fd
                << ". Error was: "
                << errorcode;

        return false;
    }

    modified = attributes.st_mtime;
    size = static_cast<m_off_t>(attributes.st_size);

    return true;
}

bool PosixFileAccess::ftruncate(m_off_t size)
{
    retry = false;

    // Truncate the file.
    if (::ftruncate(fd, size) == 0)
    {
        // Set the file pointer to the end.
        return lseek(fd, size, SEEK_SET) == size;
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

bool PosixFileAccess::fopen(const LocalPath& f,
                            bool read,
                            bool write,
                            FSLogging fsl,
                            DirAccess* iteratingDir,
                            bool,
                            [[maybe_unused]] bool skipcasecheck,
                            LocalPath* /*actualLeafNameIfDifferent*/)
{
    struct stat statbuf;

    fopenSucceeded = false;
    retry = false;
    bool statok = false;
    if (iteratingDir) //reuse statbuf from iterator
    {
        statbuf = static_cast<PosixDirAccess *>(iteratingDir)->currentItemStat;
        mIsSymLink = S_ISLNK(statbuf.st_mode) || static_cast<PosixDirAccess *>(iteratingDir)->currentItemFollowedSymlink;
        statok = true;
    }

    AdjustBasePathResult fstr = adjustBasePath(f);

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
                fnamesize = fstr.size() - (static_cast<size_t>(fname - fstr.c_str()));
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

            fopenSucceeded = true;
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

    errorcode = 0;
    fd = open(fstr.c_str(), (!mFollowSymLinks && mIsSymLink) ? (O_PATH | O_NOFOLLOW) : (write ? (read ? O_RDWR : O_WRONLY | O_CREAT) : O_RDONLY), defaultfilepermissions);
    if (fd < 0)
    {
        errorcode = errno; // streaming may set errno
        if (fsl.doLog(errorcode))
        {
            LOG_err << "Failed to open('" << fstr << "'): error " << errorcode << ": " << PosixFileSystemAccess::getErrorMessage(errorcode) << (statok ? " (statok so may still open ok)" : "");
        }
    }
    if (fd >= 0 || statok)
    {
        if (write)
        {
            umask(mode);
        }

        if (!statok)
        {
            statok = !::fstat(fd, &statbuf);
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

            fopenSucceeded = true;
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

std::string FileSystemAccess::getErrorMessage(int error)
{
    return strerror(error);
}

int FileSystemAccess::isFileHidden(const LocalPath& path, FSLogging)
{
    // What file are we actually referencing?
    auto name = path.leafName().toPath(false);

    // Only consider dotfiles hidden.
    return name.size() > 1 && name.front() == '.';
}

bool FileSystemAccess::setFileHidden(const LocalPath& path, FSLogging logWhen)
{
    return isFileHidden(path, logWhen);
}

bool FSLogging::isFileNotFound(int error)
{
    return error == ENOENT;
}

PosixFileSystemAccess::PosixFileSystemAccess()
{
    assert(sizeof(off_t) == 8);

    defaultfilepermissions = 0600;
    defaultfolderpermissions = 0700;
}

#ifdef __linux__
#ifdef ENABLE_SYNC

bool LinuxFileSystemAccess::initFilesystemNotificationSystem()
{
    mNotifyFd = inotify_init1(IN_NONBLOCK);

    if (mNotifyFd < 0)
        return mNotifyFd = -errno, false;

    return true;
}
#endif // ENABLE_SYNC

LinuxFileSystemAccess::~LinuxFileSystemAccess()
{
#ifdef ENABLE_SYNC

    // Make sure there are no active notifiers.
    assert(mNotifiers.empty());

    // Release inotify descriptor, if any.
    if (mNotifyFd >= 0)
        close(mNotifyFd);

#endif // ENABLE_SYNC
}


#endif //  __linux__


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

#ifdef __linux__
void LinuxFileSystemAccess::addevents([[maybe_unused]] Waiter* waiter, int /*flags*/)
{
#ifdef ENABLE_SYNC

    if (mNotifyFd < 0)
        return;

    auto w = static_cast<PosixWaiter*>(waiter);

    MEGA_FD_SET(mNotifyFd, &w->rfds);
    MEGA_FD_SET(mNotifyFd, &w->ignorefds);

    w->bumpmaxfd(mNotifyFd);

#endif // ENABLE_SYNC
}

// read all pending inotify events and queue them for processing
int LinuxFileSystemAccess::checkevents([[maybe_unused]] Waiter* waiter)
{
    int result = 0;

#ifdef ENABLE_SYNC

    if (mNotifyFd < 0)
        return result;

    // Called so that related syncs perform a rescan.
    auto notifyTransientFailure = [&]() {
        for (auto* notifier : mNotifiers)
            ++notifier->mErrorCount;
    };

    auto* w = static_cast<PosixWaiter*>(waiter);

    if (!MEGA_FD_ISSET(mNotifyFd, &w->rfds))
        return result;

    char buf[sizeof(struct inotify_event) + NAME_MAX + 1];
    ssize_t p, l;
    inotify_event* in;
    WatchMapIterator it;
    string localpath;

    auto notifyAll = [&](int handle, const string& name)
    {
        // Loop over and notify all associated nodes.
        auto associated = mWatches.equal_range(handle);

        for (auto i = associated.first; i != associated.second;)
        {
            // Convenience.
            using std::move;
            auto& node = *i->second.first;
            auto& sync = *node.sync;
            auto& notifier = *sync.dirnotify;

            LOG_debug << "Filesystem notification:"
                << " Root: "
                << node.localname
                << " Path: "
                << name;

            if ((in->mask & IN_DELETE_SELF))
            {
                // The FS directory watched is gone
                node.mWatchHandle.invalidate();
                // Remove it from the container (C++11 and up)
                i = mWatches.erase(i);
            }
            else
            {
                ++i;
            }

            auto localName = LocalPath::fromPlatformEncodedRelative(name);
            notifier.notify(notifier.fsEventq,
                            &node,
                            Notification::NEEDS_PARENT_SCAN,
                            std::move(localName));

            // We need to rescan the directory if it's changed permissions.
            //
            // The reason for this is that we may not have been able to list
            // the directory's contents before. If we didn't rescan, we
            // wouldn't notice these files until some other event is
            // triggered in or below this directory.
            if (in->mask == (IN_ATTRIB | IN_ISDIR))
                notifier.notify(notifier.fsEventq,
                                &node,
                                Notification::FOLDER_NEEDS_SELF_SCAN,
                                LocalPath::fromPlatformEncodedRelative(name));

            result |= Waiter::NEEDEXEC;
        }
    };

    while ((l = read(mNotifyFd, buf, sizeof buf)) > 0)
    {
        for (p = 0; p < l; p += offsetof(inotify_event, name) + in->len)
        {
            in = (inotify_event*)(buf + p);

            if ((in->mask & (IN_Q_OVERFLOW | IN_UNMOUNT)))
            {
                LOG_err << "inotify "
                    << (in->mask & IN_Q_OVERFLOW ? "IN_Q_OVERFLOW" : "IN_UNMOUNT");

                notifyTransientFailure();
            }

            // this flag was introduced in glibc 2.13 and Linux 2.6.36 (released October 20, 2010)
#ifndef IN_EXCL_UNLINK
#define IN_EXCL_UNLINK 0x04000000
#endif
            if ((in->mask & (IN_ATTRIB | IN_CREATE | IN_DELETE_SELF | IN_DELETE | IN_MOVED_FROM
                | IN_MOVED_TO | IN_CLOSE_WRITE | IN_EXCL_UNLINK)))
            {
                LOG_verbose << "Filesystem notification:"
                    << " event " << in->name << ": " << std::hex << in->mask;
                it = mWatches.find(in->wd);

                if (it != mWatches.end())
                {
                    // What nodes are associated with this handle?
                    notifyAll(it->first, in->len? in->name : "");
                }
            }
        }
    }

#endif // ENABLE_SYNC

    return result;
}

#endif //  __linux__


// no legacy DOS garbage here...
bool PosixFileSystemAccess::getsname(const LocalPath&, LocalPath&) const
{
    return false;
}

bool PosixFileSystemAccess::renamelocal(const LocalPath& oldname, const LocalPath& newname, bool override)
{
    AdjustBasePathResult oldnamestr = adjustBasePath(oldname);
    AdjustBasePathResult newnamestr = adjustBasePath(newname);

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
    AdjustBasePathResult oldnamestr = adjustBasePath(oldname);
    AdjustBasePathResult newnamestr = adjustBasePath(newname);

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
            while (((t = read(sfd, buf, sizeof buf)) > 0) &&
                   write(tfd, buf, static_cast<size_t>(t)) == t)
                ;
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
    AdjustBasePathResult namestr = adjustBasePath(name);

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
                    LocalPath newpath{name};

                    newpath.appendWithSeparator(LocalPath::fromPlatformEncodedRelative(d->d_name),
                                                true);

                    AdjustBasePathResult nameStr = adjustBasePath(newpath);

                    if (!lstat(nameStr.c_str(), &statbuf))
                    {
                        if (!S_ISLNK(statbuf.st_mode) && S_ISDIR(statbuf.st_mode) && statbuf.st_dev == basedev)
                        {
                            emptydirlocal(newpath, basedev);
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
    // Sanitize permissions.
    permissions &= 07777;

    defaultfilepermissions = permissions | mMinimumFilePermissions;
}

int PosixFileSystemAccess::getdefaultfolderpermissions()
{
    return defaultfolderpermissions;
}

void PosixFileSystemAccess::setdefaultfolderpermissions(int permissions)
{
    // Sanitize permissions.
    permissions &= 07777;

    defaultfolderpermissions = permissions | mMinimumDirectoryPermissions;
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
    AdjustBasePathResult nameStr = adjustBasePath(name);

    mode_t mode = umask(0);
    bool r = !mkdir(nameStr.c_str(), static_cast<mode_t>(defaultfolderpermissions));
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
    AdjustBasePathResult nameStr = adjustBasePath(name);

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

bool PosixFileSystemAccess::expanselocalpath(const LocalPath& source, LocalPath& destination)
{
    // Sanity.
    assert(!source.empty());

    // At worst, the destination mirrors the source.
    destination = source;

    // Are we dealing with a relative path?
    if (!source.isAbsolute())
    {
        // Sanity.
        assert(source.toPath(false)[0] != '/');

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
    assert(destination.toPath(false)[0] == '/');

    // Canonicalize the path.
    char buffer[PATH_MAX];

    if (!realpath(destination.toPath(false).c_str(), buffer))
    {
        destination = source;
        return false;
    }

    destination = LocalPath::fromAbsolutePath(buffer);

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
        id->append(buff, static_cast<size_t>(len));
    }
#endif
}

#if defined(ENABLE_SYNC)
#if defined(__linux__)

LinuxDirNotify::LinuxDirNotify(LinuxFileSystemAccess& owner,
                               LocalNode& /*root*/,
                               const LocalPath& rootPath):
    DirNotify(rootPath),
    mOwner(owner),
    mNotifiersIt(owner.mNotifiers.insert(owner.mNotifiers.end(), this))
{
    // Assume our owner couldn't initialize.
    setFailed(-owner.mNotifyFd, "Unable to create filesystem monitor.");

    // Did our owner initialize correctly?
    if (owner.mNotifyFd >= 0)
        setFailed(0, "");
}

LinuxDirNotify::~LinuxDirNotify()
{
    // Remove ourselves from our owner's list of notiifers.
    mOwner.mNotifiers.erase(mNotifiersIt);
}

#if defined(USE_INOTIFY)

AddWatchResult LinuxDirNotify::addWatch(LocalNode& node,
    const LocalPath& path,
    handle fsid)
{
    using std::forward_as_tuple;
    using std::piecewise_construct;

    assert(node.type == FOLDERNODE);

    // Convenience.
    auto& watches = mOwner.mWatches;

    auto handle =
        inotify_add_watch(mOwner.mNotifyFd,
                          path.toPath(false).c_str(),
                          IN_ATTRIB | IN_CLOSE_WRITE | IN_CREATE | IN_DELETE | IN_DELETE_SELF |
                              IN_EXCL_UNLINK | IN_MOVED_FROM // event->cookie set as IN_MOVED_TO
                              | IN_MOVED_TO | IN_ONLYDIR);

    if (handle >= 0)
    {
        auto entry =
            watches.emplace(piecewise_construct,
                forward_as_tuple(handle),
                forward_as_tuple(&node, fsid));

        return make_pair(entry, WR_SUCCESS);
    }

    LOG_warn << "Unable to monitor path for filesystem notifications: "
             << path.toPath(false).c_str() << ": Descriptor: " << mOwner.mNotifyFd
             << ": Error: " << errno;

    if (errno == ENOMEM || errno == ENOSPC)
        return make_pair(watches.end(), WR_FATAL);

    return make_pair(watches.end(), WR_FAILURE);
}

void LinuxDirNotify::removeWatch(WatchMapIterator entry)
{
    LOG_verbose << "removeWatch for handle: " << entry->first;
    auto& watches = mOwner.mWatches;

    auto handle = entry->first;
    assert(handle >= 0);

    watches.erase(entry); // Removes first instance

    if (watches.find(handle) != watches.end())
    {
        LOG_warn << " There are more watches under handle: " << handle;

        auto it = watches.find(handle);

        while (it!=watches.end() && it->first == handle)
        {
            LOG_warn << "Handle: " << handle << " fsid:" << it->second.second;

            ++it;
        }

        return;
    }

    auto const removedResult = inotify_rm_watch(mOwner.mNotifyFd, handle);

    if (removedResult)
    {
        LOG_verbose << "inotify_rm_watch for handle: " << handle
            <<  " error no: " << errno;
    }
}

#endif // USE_INOTIFY
#endif // __linux__

#endif //ENABLE_SYNC
// Used by directoryScan(...) below to avoid extra stat(...) calls.
class UnixStreamAccess
    : public InputStreamAccess
{
public:
    UnixStreamAccess(const char* path, m_off_t size)
      : mDescriptor(open(path))
      , mOffset(0)
      , mSize(size)
    {
    }

    MEGA_DISABLE_COPY_MOVE(UnixStreamAccess);

    ~UnixStreamAccess()
    {
        if (mDescriptor >= 0)
            close(mDescriptor);
    }

    operator bool() const
    {
        return mDescriptor >= 0;
    }

    bool read(byte* buffer, unsigned size) override
    {
        if (mDescriptor < 0)
            return false;

        if (!buffer)
            return mOffset += (m_off_t)size, true;

        auto result = pread(mDescriptor, (void*)buffer, size, mOffset);

        if (result < 0 || (unsigned)result < size)
            return false;

        mOffset += result;

        return true;
    }

    m_off_t size() override
    {
        return mDescriptor >= 0 ? mSize : -1;
    }

private:

    // open with O_NOATIME if possible
    int open(const char *path)
    {
#ifdef TARGET_OS_IPHONE
        // building for iOS, there is no O_NOATIME flag
        int fd = ::open(path, O_RDONLY) ;
#else
        // for sync in particular, try to open without setting access-time
        // we don't want to update that every time we get a fingerprint to see if it's changed
        // and we don't want to be processing the filesystem notifications that would cause either
        int fd = ::open(path, O_NOATIME | O_RDONLY);

        if (fd < 0 && errno == EPERM)
        {
            // But then, on some systems (Android) sometimes (for external storage, but not for internal), the call fails if we try to set O_NOATIME
            fd = ::open(path, O_RDONLY);
        }
#endif
        return fd;
    }

    int mDescriptor;
    m_off_t mOffset;
    m_off_t mSize;
}; // UnixStreamAccess

ScanResult PosixFileSystemAccess::directoryScan(const LocalPath& targetPath,
                                                handle expectedFsid,
                                                map<LocalPath, FSNode>& known,
                                                std::vector<FSNode>& results,
                                                bool followSymLinks,
                                                unsigned& nFingerprinted)
{
    // Scan path should always be absolute.
    assert(targetPath.isAbsolute());

    // Whether we can reuse an existing fingerprint.
    // I.e. Can we avoid computing the CRC?
    auto reuse = [](const FSNode& lhs, const FSNode& rhs) {
        return lhs.type == rhs.type
               && lhs.fsid == rhs.fsid
               && lhs.fingerprint.mtime == rhs.fingerprint.mtime
               && lhs.fingerprint.size == rhs.fingerprint.size;
    };

    // So we don't duplicate link chasing logic.
    auto stat = [&](const char* path, struct stat& metadata, bool* followSymLinkHere = nullptr) {
        auto result = !lstat(path, &metadata);

        if (!result) return false;

        bool followSymLink = followSymLinkHere ? *followSymLinkHere : followSymLinks;
        if (!followSymLink || !S_ISLNK(metadata.st_mode))
            return result;

        return !::stat(path, &metadata);
    };

    // Where we store file information.
    struct stat metadata;

    // Try and get information about the scan target.
    bool scanTarget_followSymLink = true; // Follow symlink for the parent directory, so we retrieve the stats of the path that the symlinks points to
    if (!stat(targetPath.toPath(false).c_str(), metadata, &scanTarget_followSymLink))
    {
        LOG_warn << "Failed to directoryScan: "
                 << "Unable to stat(...) scan target: "
                 << targetPath
                 << ". Error code was: "
                 << errno;

        return SCAN_INACCESSIBLE;
    }

    // Is the scan target a directory?
    if (!S_ISDIR(metadata.st_mode))
    {
        LOG_warn << "Failed to directoryScan: "
                 << "Scan target is not a directory: "
                 << targetPath;

        return SCAN_INACCESSIBLE;
    }

    // Are we scanning the directory we think we are?
    if (expectedFsid != (handle)metadata.st_ino)
    {
        LOG_warn << "Failed to directoryScan: "
                 << "Scan target mismatch on expected FSID: "
                 << targetPath
                 << " was " << expectedFsid
                 << " now " << (handle)metadata.st_ino;

        return SCAN_FSID_MISMATCH;
    }

    // Try and open the directory for iteration.
    auto directory = opendir(targetPath.toPath(false).c_str());

    if (!directory)
    {
        LOG_warn << "Failed to directoryScan: "
                 << "Unable to open scan target for iteration: "
                 << targetPath
                 << ". Error code was: "
                 << errno;

        return SCAN_INACCESSIBLE;
    }

    // What device is this directory on?
    auto device = metadata.st_dev;

    // Iterate over the directory's children.
    auto entry = readdir(directory);
    auto path = targetPath;

    for ( ; entry; entry = readdir(directory))
    {
        // Skip special hardlinks.
        if (!strcmp(entry->d_name, "."))
            continue;

        if (!strcmp(entry->d_name, ".."))
            continue;

        // Push a new scan record.
        auto& result = (results.emplace_back(), results.back());

        result.fsid = (handle)entry->d_ino;
        result.localname = LocalPath::fromPlatformEncodedRelative(entry->d_name);

        // Compute this entry's absolute name.
        LocalPath newpath{path};

        newpath.appendWithSeparator(result.localname, false);

        // Try and get information about this entry.
        if (!stat(newpath.toPath(false).c_str(), metadata))
        {
            LOG_warn << "directoryScan: "
                     << "Unable to stat(...) file: " << newpath << ". Error code was: " << errno;

            // Entry's unknown if we can't determine otherwise.
            result.type = TYPE_UNKNOWN;
            continue;
        }

        result.fingerprint.mtime = metadata.st_mtime;
        captimestamp(&result.fingerprint.mtime);

        // Are we dealing with a directory?
        if (S_ISDIR(metadata.st_mode))
        {
            // Then no fingerprint is necessary.
            result.fingerprint.size = 0;

            // Assume this directory isn't a mount point.
            result.type = FOLDERNODE;

            // Directory's a mount point.
            if (device != metadata.st_dev)
            {
                // Mark directory as a mount so we can emit a stall.
                result.type = TYPE_NESTED_MOUNT;

                // Leave a trail for debuggers.
                LOG_warn << "directoryScan: "
                         << "Encountered a nested mount: " << newpath << ". Expected device "
                         << major(static_cast<unsigned>(device)) << ":"
                         << minor(static_cast<unsigned>(device)) << ", got device "
                         << major(static_cast<unsigned>(metadata.st_dev)) << ":"
                         << minor(static_cast<unsigned>(metadata.st_dev));
            }

            continue;
        }

        result.fingerprint.size = metadata.st_size;

        // Are we dealing with a special file?
        if (!S_ISREG(metadata.st_mode))
        {
            LOG_warn << "directoryScan: "
                     << "Encountered a special file: " << newpath
                     << ". Mode flags were: " << (metadata.st_mode & S_IFMT);

            result.isSymlink = S_ISLNK(metadata.st_mode);
            result.type = result.isSymlink ? TYPE_SYMLINK: TYPE_SPECIAL;
            continue;
        }

        // We're dealing with a regular file.
        result.type = FILENODE;

#ifdef __MACH__
        // 1904/01/01 00:00:00 +0000 GMT.
        //
        // Special marker set by Finder when it begins a long-lasting
        // operation such as copying a file from/to USB storage.
        //
        // In some cases, attributes such as mtime or size can be unstable
        // and effectively meaningless.
        constexpr auto busyDate = -2082844800;

        // The file's temporarily unaccessible while it's busy.
        //
        // Attributes such as size are pretty much meaningless.
        result.isBlocked = metadata.st_birthtimespec.tv_sec == busyDate;

        if (result.isBlocked)
        {
            LOG_warn << "directoryScan: "
                     << "Finder has marked this file as busy: " << newpath;
            continue;
        }
#endif // __MACH__

        // Have we processed this file before?
        auto it = known.find(result.localname);

        // Can we avoid recomputing this file's fingerprint?
        if (it != known.end() && reuse(result, it->second))
        {
            result.fingerprint = std::move(it->second.fingerprint);
            continue;
        }

        // Try and open the file for reading.
        UnixStreamAccess isAccess(newpath.toPath(false).c_str(), result.fingerprint.size);

        // Only fingerprint the file if we could actually open it.
        if (!isAccess)
        {
            LOG_warn << "directoryScan: "
                     << "Unable to open file for fingerprinting: " << newpath
                     << ". Error was: " << errno;
            continue;
        }

        // Fingerprint the file.
        result.fingerprint.genfingerprint(
          &isAccess, result.fingerprint.mtime);

        ++nFingerprinted;
    }

    // We're done iterating the directory.
    closedir(directory);

    return SCAN_SUCCESS;
}

#ifndef __APPLE__

// Determine which device contains the specified path.
static std::string deviceOf(const std::string& database,
                            const std::string& path)
{
    // Convenience.
    using FileDeleter = std::function<int(FILE*)>;
    using FilePtr     = std::unique_ptr<FILE, FileDeleter>;

    LOG_verbose << "Opening mount database: "
                << database;

    // Try and open mount database.
    FilePtr mounts(setmntent(database.c_str(), "r"), endmntent);

    // Couldn't open mount database.
    if (!mounts)
    {
        // Latch error.
        auto error = errno;

        LOG_warn << "Couldn't open mount database: "
                 << database
                 << ". Error was: "
                 << strerror(error);

        return std::string();
    }

    // What device contains path?
    std::string device;

    // Determines which device is the strongest match.
    //
    // As an example consider:
    // /dev/sda1 -> /mnt/usb
    // /dev/sda2 -> /mnt/usb/a/b/c
    //
    // /dev/sda2 is a better match for /mnt/usb/a/b/c/d.
    std::size_t score = 0;

    // Temporary storage space for mount entries.
    std::string storage(3 * PATH_MAX, '\0');

    // Try and determine which device contains path.
    for (errno = 0; ; )
    {
        struct mntent entry;

        // Couldn't retrieve mount entry.
        if (!getmntent_r(mounts.get(),
                         &entry,
                         storage.data(),
                         static_cast<int>(storage.size())))
            break;

        // Where is this device mounted?
        std::string target = entry.mnt_dir;

        // Path's too short to be contained by target.
        if (path.size() < target.size())
            continue;

        // Target doesn't contain path.
        if (path.compare(0, target.size(), target))
            continue;

        // Existing device is a better match.
        if (score >= target.size())
            continue;

        // This device is a better match.
        device = entry.mnt_fsname;
        score  = target.size();
    }

    // Couldn't retrieve mount entry.
    if (errno)
    {
        // Latch error.
        auto error = errno;

        LOG_warn << "Couldn't enumerate mount database: "
                 << database
                 << ". Error was: "
                 << std::strerror(error);

        return std::string();
    }

    // No device seems to contain path.
    if (device.empty())
    {
        LOG_warn << "No device seems to contain path: "
                 << path;

        return std::string();
    }

    // Device isn't actually a device.
    if (device.front() != '/')
    {
        LOG_warn << "A virtual device "
                 << device
                 << " seems to contain path: "
                 << path;

        return std::string();
    }

    // Couldn't resolve symlinks in device.
    //
    // This is necessary to correctly handle nodes managed by device-mapper.
    // Say, the user is using LUKS or LVM.
    if (!realpath(device.c_str(), storage.data()))
    {
        // Latch error.
        auto error = errno;

        LOG_warn << "Couldn't resolve device symlink: "
                 << device
                 << ". Error was: "
                 << std::strerror(error);
                 
        return std::string();
    }

    // Truncate storage down to size.
    storage.erase(storage.find('\0'));

    // Sanity.
    assert(!storage.empty());

    // For debugging purposes.
    LOG_verbose << "Path "
                << path
                << " is on device "
                << storage;

    // Return device to caller.
    return storage;
}

static std::string deviceOf(const std::string& path)
{
    // Which mount databases should we search?
    static const std::vector<std::string> databases = {
        "/proc/mounts",
        "/etc/mtab"
    }; // databases

    // Try and determine which device contains path.
    for (const auto& database : databases)
    {
        // Ask database which devices contains path.
        auto device = deviceOf(database, path);

        // Database has a mapping for path.
        if (!device.empty())
            return device;
    }

    LOG_warn << "Couldn't determine which device contains path: "
             << path;

    // No database has a mapping for this path.
    return std::string();
}

// Compute legacy filesystem fingerprint.
static std::uint64_t fingerprintOf(const std::string& path)
{
    struct statfs buffer;

    // What filesystem contains our path?
    if (statfs(path.c_str(), &buffer))
    {
        // Latch error.
        auto error = errno;

        LOG_warn << "Couldn't retrieve filesystem ID: "
                 << path
                 << ". Error was: "
                 << std::strerror(error);

        return 0;
    }

    std::uint64_t value;

    // Alias-friendly conversion to uint64_t.
    std::memcpy(&value, &buffer.f_fsid, sizeof(value));

    return ++value;
}

// Determine the UUID of the specified device.
static std::string uuidOf(const std::string& device)
{
    // Convenience.
    using IteratorDeleter = std::function<int(DIR*)>;
    using IteratorPtr     = std::unique_ptr<DIR, IteratorDeleter>;

    std::string path = "/dev/disk/by-uuid";

    // Try and open /dev/disk/by-uuid.
    IteratorPtr iterator(opendir(path.c_str()), closedir);

    // Couldn't open /dev/disk/by-uuid.
    if (!iterator)
    {
        // Latch error.
        auto error = errno;

        LOG_warn << "Couldn't determine device UUID: "
                 << strerror(error);

        return std::string();
    }

    // Convenience.
    auto size = path.size();

    // Try and determine which entry references device.
    for (errno = 0; ; )
    {
        // Try and retrieve next directory entry.
        const auto* entry = readdir(iterator.get());

        // Couldn't retrieve directory entry.
        if (!entry)
            break;

        // Restore path's size.
        path.resize(size);

        // Extract entry's name.
        auto name = std::string(entry->d_name);

        // Compute path of directory entry.
        path.append(1, '/');
        path.append(name);

        // Temporary storage.
        std::string storage(PATH_MAX, '\0');

        // Couldn't resolve link.
        if (!realpath(path.c_str(), storage.data()))
        {
            // Latch error.
            auto error = errno;

            LOG_warn << "[uuidOf] Couldn't resolve path link: '" << storage
                     << "'. Error was: " << std::strerror(error);

            continue;
        }

        // Truncate storage down to size.
        storage.erase(storage.find('\0'));

        // Sanity.
        assert(!storage.empty());

        // Resolved path matches our device.
        if (device == storage)
            return name;
    }

    // Couldn't determine device's UUID.
    return std::string();
}

fsfp_t FileSystemAccess::fsFingerprint(const LocalPath& path) const
{
    // Try and compute legacy filesystem fingerprint.
    auto fingerprint = fingerprintOf(path.toPath(false));

    // Couldn't compute legacy fingerprint.
    if (!fingerprint)
        return fsfp_t();

    // What device contains the specified path?
    auto device = deviceOf(path.toPath(false));

    // We know what device contains path.
    if (!device.empty())
    {
        // Try and determine the device's UUID.
        auto uuid = uuidOf(device);

        // We retrieved the device's UUID.
        if (!uuid.empty())
            return fsfp_t(fingerprint, std::move(uuid));
    }

    // Couldn't determine filesystem UUID.
    return fsfp_t(fingerprint, std::string());
}

#endif // ! __APPLE__

#ifdef ENABLE_SYNC

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
           && type != FS_FUSE
           && type != FS_LIFS;
}

#endif // ENABLE_SYNC

bool PosixFileSystemAccess::hardLink(const LocalPath& source, const LocalPath& target)
{
    AdjustBasePathResult sourcePath = adjustBasePath(source);
    AdjustBasePathResult targetPath = adjustBasePath(target);

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
#ifndef __ANDROID__
    return std::unique_ptr<FileAccess>{new PosixFileAccess{waiter, defaultfilepermissions, followSymLinks}};
#else
    if (fileWrapper != nullptr)
    {
        return std::unique_ptr<FileAccess>{
            new AndroidFileAccess{waiter, defaultfilepermissions, followSymLinks}};
    }
    else
    {
        return std::unique_ptr<FileAccess>{
            new PosixFileAccess{waiter, defaultfilepermissions, followSymLinks}};
    }
#endif
}

unique_ptr<DirAccess>  PosixFileSystemAccess::newdiraccess()
{
#ifndef __ANDROID__
    return unique_ptr<DirAccess>(new PosixDirAccess());
#else
    if (fileWrapper != nullptr)
    {
        return unique_ptr<DirAccess>(new AndroidDirAccess());
    }
    else
    {
        return unique_ptr<DirAccess>(new PosixDirAccess());
    }
#endif
}

#ifdef __linux__
#ifdef ENABLE_SYNC
DirNotify* LinuxFileSystemAccess::newdirnotify(LocalNode& root,
    const LocalPath& rootPath,
    Waiter*)
{
    return new LinuxDirNotify(*this, root, rootPath);
}
#endif
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

    if (!statfs(path.toPath(false).c_str(), &statbuf))
    {
        switch (static_cast<unsigned long>(statbuf.f_type))
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
        {"ufsd_NTFS",   FS_NTFS},  // Paragon NTFS for Mac
        {"lifs",        FS_LIFS},  // on macos (in Ventura at least), external USB with exFAT are reported as "lifs"
    }; /* filesystemTypes */

    struct statfs statbuf;

    if (!statfs(path.toPath(false).c_str(), &statbuf))
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
        LocalPath newpath{path};

        if (*d->d_name != '.' || (d->d_name[1] && (d->d_name[1] != '.' || d->d_name[2])))
        {
            newpath.appendWithSeparator(LocalPath::fromPlatformEncodedRelative(d->d_name), true);

            AdjustBasePathResult pathStr = adjustBasePath(newpath);

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

// A more robust implementation would check whether the device has storage
// quotas enabled and if so, return the amount of space available before
// saturating that quota.
m_off_t PosixFileSystemAccess::availableDiskSpace(const LocalPath& drivePath)
{
    struct statfs buffer;
    m_off_t constexpr maximumBytes = std::numeric_limits<m_off_t>::max();

    if (statfs(adjustBasePath(drivePath).c_str(), &buffer) < 0)
    {
        auto result = errno;

        LOG_warn << "Unable to determine available disk space on volume: "
                 << drivePath
                 << ". Error code was: "
                 << result;

        return maximumBytes;
    }

    uint64_t availableBytes = buffer.f_bavail * (uint64_t)buffer.f_bsize;

    if (availableBytes >= (uint64_t)maximumBytes)
        return maximumBytes;

    return (m_off_t)availableBytes;
}

} // namespace

