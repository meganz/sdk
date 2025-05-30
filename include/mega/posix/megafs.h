/**
 * @file mega/posix/megafs.h
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
 * You should have received a copy of the license along with this
 * program.
 */

#ifndef MEGA_POSIX_FS_H
#define MEGA_POSIX_FS_H

#ifdef  __APPLE__
// Apple calls it sendfile, but it isn't
#undef HAVE_SENDFILE
#define O_DIRECT 0
#include <sys/param.h>
#include <sys/mount.h>
#elif defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__) || defined(__DragonFly__)
#include <sys/mount.h>
#else
#include <sys/vfs.h>
#include <dirent.h>
#endif

#ifdef HAVE_AIO_RT
#include <aio.h>
#endif

#include "mega.h"

#define DEBRISFOLDER ".debris"

namespace mega {

namespace detail {

using AdjustBasePathResult = std::string;

AdjustBasePathResult adjustBasePath(const LocalPath& path);

} // detail

struct MEGA_API PosixDirAccess : public DirAccess
{
    DIR* dp;
    bool globbing;
    glob_t globbuf;
    unsigned globindex;

    struct stat currentItemStat;
    bool currentItemFollowedSymlink;

    bool dopen(LocalPath*, FileAccess*, bool) override;
    bool dnext(LocalPath&, LocalPath&, bool, nodetype_t*) override;

    PosixDirAccess();
    virtual ~PosixDirAccess();
};

class MEGA_API PosixFileSystemAccess : public FileSystemAccess
{
public:
    using FileSystemAccess::getlocalfstype;

    int defaultfilepermissions;
    int defaultfolderpermissions;

    unique_ptr<FileAccess> newfileaccess(bool followSymLinks = true) override;
    unique_ptr<DirAccess>  newdiraccess() override;

    bool getlocalfstype(const LocalPath& path, FileSystemType& type) const override;
    bool issyncsupported(const LocalPath& localpathArg, bool& isnetwork, SyncError& syncError, SyncWarning& syncWarning) override;

    bool getsname(const LocalPath&, LocalPath&) const override;

    bool renamelocal(const LocalPath&, const LocalPath&, bool) override;
    bool copylocal(const LocalPath&, const LocalPath&, m_time_t) override;
    bool rubbishlocal(string*);
    bool unlinklocal(const LocalPath&) override;
    bool rmdirlocal(const LocalPath&) override;
    bool mkdirlocal(const LocalPath&, bool hidden, bool logAlreadyExistsError) override;
    bool setmtimelocal(const LocalPath&, m_time_t) override;
    bool chdirlocal(LocalPath&) const override;
    bool expanselocalpath(const LocalPath& path, LocalPath& absolutepath) override;

    void osversion(string*, bool includeArchitecture) const override;
    void statsid(string*) const override;

    // Returns true if provided error as param is considered a transient error (an error lasting
    // only for a short period of time). Otherwise returns false
    static bool isTransient(const int e);
    static void emptydirlocal(const LocalPath&, dev_t = 0);

    int getdefaultfilepermissions() override;
    void setdefaultfilepermissions(int) override;
    int getdefaultfolderpermissions() override;
    void setdefaultfolderpermissions(int) override;

    PosixFileSystemAccess();

    static bool cwd_static(LocalPath& path);
    bool cwd(LocalPath& path) const override;

    ScanResult directoryScan(const LocalPath& path,
                             handle expectedFsid,
                             map<LocalPath, FSNode>& known,
                             std::vector<FSNode>& results,
                             bool followSymLinks,
                             unsigned& nFingerprinted) override;

#ifdef ENABLE_SYNC
    bool fsStableIDs(const LocalPath& path) const override;

#endif // ENABLE_SYNC

    bool hardLink(const LocalPath& source, const LocalPath& target) override;

    m_off_t availableDiskSpace(const LocalPath& drivePath) override;
};

#ifdef HAVE_AIO_RT
struct MEGA_API PosixAsyncIOContext : public AsyncIOContext
{
    PosixAsyncIOContext();
    ~PosixAsyncIOContext() override;
    void finish() override;

    struct aiocb *aiocb;
};
#endif

class MEGA_API PosixFileAccess : public FileAccess
{
private:
    int fd;
public:
    int stealFileDescriptor();
    int defaultfilepermissions;

    static bool mFoundASymlink;

#ifndef HAVE_FDOPENDIR
    DIR* dp;
#endif

    bool fopen(const LocalPath&, bool read, bool write, FSLogging,
               DirAccess* iteratingDir = nullptr, bool ignoreAttributes = false, bool skipcasecheck = false, LocalPath* actualLeafNameIfDifferent = nullptr) override;

    void updatelocalname(const LocalPath&, bool force) override;
    bool fread(string *, unsigned, unsigned, m_off_t);
    void fclose() override;
    bool fwrite(const byte *, unsigned, m_off_t) override;

    bool fstat(m_time_t& modified, m_off_t& size) override;

    bool ftruncate(m_off_t size) override;

    bool sysread(byte *, unsigned, m_off_t) override;
    bool sysstat(m_time_t*, m_off_t*, FSLogging) override;
    bool sysopen(bool async, FSLogging) override;
    void sysclose() override;

    PosixFileAccess(Waiter *w, int defaultfilepermissions = 0600, bool followSymLinks = true);

    // async interface
    bool asyncavailable() override;
    void asyncsysopen(AsyncIOContext* context) override;
    void asyncsysread(AsyncIOContext* context) override;
    void asyncsyswrite(AsyncIOContext* context) override;

    ~PosixFileAccess();

#ifdef HAVE_AIO_RT
protected:
    AsyncIOContext* newasynccontext() override;
    static void asyncopfinished(union sigval sigev_value);
#endif

private:
    bool mFollowSymLinks = true;

};

#ifdef __linux__

#ifndef __ANDROID__
#define FSACCESS_CLASS LinuxFileSystemAccess
#else
#define FSACCESS_CLASS AndroidFileSystemAccess
#endif

class LinuxFileSystemAccess
  : public PosixFileSystemAccess
{
public:
    friend class LinuxDirNotify;

    ~LinuxFileSystemAccess();

    void addevents(Waiter* waiter, int flags) override;

    int checkevents(Waiter* waiter) override;

#ifdef ENABLE_SYNC

    bool initFilesystemNotificationSystem() override;

    DirNotify* newdirnotify(LocalNode& root,
                            const LocalPath& rootPath,
                            Waiter* waiter) override;

private:
    // Tracks which notifiers were created by this instance.
    list<DirNotify*> mNotifiers;

    // Inotify descriptor.
    int mNotifyFd = -EINVAL;

    // Tracks which nodes are associated with what inotify handle.
    WatchMap mWatches;

#endif // ENABLE_SYNC
}; // LinuxFileSystemAccess

#ifdef ENABLE_SYNC

// Convenience.
using AddWatchResult = pair<WatchMapIterator, WatchResult>;

class LinuxDirNotify
  : public DirNotify
{
public:
    LinuxDirNotify(LinuxFileSystemAccess& owner,
                   LocalNode& root,
                   const LocalPath& rootPath);

    ~LinuxDirNotify();

    virtual AddWatchResult addWatch(LocalNode& node, const LocalPath& path, handle fsid);

    void removeWatch(WatchMapIterator entry);

private:
    // The LFSA that we are associated with.
    LinuxFileSystemAccess& mOwner;

    // Our position in our owner's mNotifiers list.
    list<DirNotify*>::iterator mNotifiersIt;
}; // LinuxDirNotify

#endif // ENABLE_SYNC

#endif // __linux__

} // namespace

#endif
