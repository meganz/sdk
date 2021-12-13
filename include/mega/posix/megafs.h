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

#ifndef FSACCESS_CLASS
#define FSACCESS_CLASS PosixFileSystemAccess

#ifdef  __APPLE__
// Apple calls it sendfile, but it isn't
#undef HAVE_SENDFILE
#define O_DIRECT 0
#include <sys/param.h>
#include <sys/mount.h>
#elif defined(__FreeBSD__)
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

    int notifyfd = -1;

    // Used to store any errors that might be returned by the system when we
    // try and create a filesystem monitor.
    //
    // Say, the reason why we couldn't create an inotify instance.
    int mNotificationError = 0;

#ifdef USE_INOTIFY
    WatchMap mWatches;
#endif

#ifdef __MACH__
    // Correlates filesystem event path to sync root node.
    map<string, Sync*> mRoots;
#endif // __MACH__

#ifdef USE_IOS
    static char *appbasepath;
#endif

    int defaultfilepermissions;
    int defaultfolderpermissions;

    std::unique_ptr<FileAccess> newfileaccess(bool followSymLinks = true) override;
    DirAccess* newdiraccess() override;
#ifdef ENABLE_SYNC
    DirNotify* newdirnotify(LocalNode& root, const LocalPath& rootPath, Waiter* waiter) override;

    // Track notifiers created by this FSA.
    //
    // Necessary so that we can correctly report transient failures.
    //
    // Say, if the event buffer has overflowed. In that case, we need to
    // signal to each sync that they need to perform a rescan.
    list<DirNotify*> mNotifiers;
#endif

    bool getlocalfstype(const LocalPath& path, FileSystemType& type) const override;
    bool issyncsupported(const LocalPath& localpathArg, bool& isnetwork, SyncError& syncError, SyncWarning& syncWarning) override;

    void tmpnamelocal(LocalPath&) const override;

    bool getsname(const LocalPath&, LocalPath&) const override;

    bool renamelocal(const LocalPath&, const LocalPath&, bool) override;
    bool copylocal(LocalPath&, LocalPath&, m_time_t) override;
    bool rubbishlocal(string*);
    bool unlinklocal(const LocalPath&) override;
    bool rmdirlocal(const LocalPath&) override;
    bool mkdirlocal(const LocalPath&, bool hidden, bool logAlreadyExistsError) override;
    bool setmtimelocal(LocalPath&, m_time_t) override;
    bool chdirlocal(LocalPath&) const override;
    bool getextension(const LocalPath&, std::string&) const override;
    bool expanselocalpath(LocalPath& path, LocalPath& absolutepath) override;

    void addevents(Waiter*, int) override;
    int checkevents(Waiter*) override;

    void osversion(string*, bool includeArchitecture) const override;
    void statsid(string*) const override;

    static void emptydirlocal(const LocalPath&, dev_t = 0);

    int getdefaultfilepermissions();
    void setdefaultfilepermissions(int);
    int getdefaultfolderpermissions();
    void setdefaultfolderpermissions(int);

    PosixFileSystemAccess();
    ~PosixFileSystemAccess();

    bool cwd(LocalPath& path) const override;

#ifdef ENABLE_SYNC
    fsfp_t fsFingerprint(const LocalPath& path) const override;

    bool fsStableIDs(const LocalPath& path) const override;

    bool initFilesystemNotificationSystem(int notificationFd) override;
#endif // ENABLE_SYNC
};

#ifdef HAVE_AIO_RT
struct MEGA_API PosixAsyncIOContext : public AsyncIOContext
{
    PosixAsyncIOContext();
    virtual ~PosixAsyncIOContext();
    virtual void finish();

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

    bool fopen(const LocalPath&, bool read, bool write, DirAccess* iteratingDir = nullptr, bool ignoreAttributes = false) override;

    void updatelocalname(const LocalPath&, bool force) override;
    bool fread(string *, unsigned, unsigned, m_off_t);
    bool fwrite(const byte *, unsigned, m_off_t) override;

    bool ftruncate() override;

    bool sysread(byte *, unsigned, m_off_t) override;
    bool sysstat(m_time_t*, m_off_t*) override;
    bool sysopen(bool async = false) override;
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
    virtual AsyncIOContext* newasynccontext();
    static void asyncopfinished(union sigval sigev_value);
#endif

private:
    bool mFollowSymLinks = true;

};

#ifdef ENABLE_SYNC
class MEGA_API PosixDirNotify : public DirNotify
{
public:
    PosixFileSystemAccess* fsaccess;

    PosixDirNotify(PosixFileSystemAccess& fsAccess, LocalNode& root, const LocalPath& rootPath);

    ~PosixDirNotify();

#if defined(ENABLE_SYNC) && defined(USE_INOTIFY)
    pair<WatchMapIterator, WatchResult> addWatch(LocalNode& node, const LocalPath& path, handle fsid);
    void removeWatch(WatchMapIterator entry);
#endif // ENABLE_SYNC && USE_INOTIFY

private:
#ifdef __MACH__
    // Our position in the PFSA::mRoots map.
    map<string, Sync*>::iterator mRootsIt;
#endif // __MACH__

    // Our position in the PFSA::mNotifiers list.
    list<DirNotify*>::iterator mNotifiersIt;
};
#endif

} // namespace

#endif
