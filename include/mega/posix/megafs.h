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

#ifndef FSACCESS_CLASS
#define FSACCESS_CLASS PosixFileSystemAccess
#endif // ! FSACCESS_CLASS

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

    int notifyfd;

#ifdef USE_INOTIFY
    typedef map<int, LocalNode*> wdlocalnode_map;
    wdlocalnode_map wdnodes;

    // skip the IN_FROM component in moves if followed by IN_TO
    LocalNode* lastlocalnode;
    uint32_t lastcookie;
    string lastname;
#endif

#ifdef USE_IOS
    static char *appbasepath;
#endif

    int defaultfilepermissions;
    int defaultfolderpermissions;

    unique_ptr<FileAccess> newfileaccess(bool followSymLinks = true) override;
    unique_ptr<DirAccess>  newdiraccess() override;
#ifdef ENABLE_SYNC
    DirNotify* newdirnotify(const LocalPath&, const LocalPath&, Waiter*, LocalNode* syncroot) override;
#endif

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
    bool getextension(const LocalPath&, std::string&) const override;
    bool expanselocalpath(const LocalPath& path, LocalPath& absolutepath) override;

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

    static bool cwd_static(LocalPath& path);
    bool cwd(LocalPath& path) const override;

    ScanResult directoryScan(const LocalPath& path,
                             handle expectedFsid,
                             map<LocalPath, FSNode>& known,
                             std::vector<FSNode>& results,
                             bool followSymLinks,
                             unsigned& nFingerprinted) override;
							 
#ifdef ENABLE_SYNC
    fsfp_t fsFingerprint(const LocalPath& path) const override;

    bool fsStableIDs(const LocalPath& path) const override;

    bool initFilesystemNotificationSystem() override;
#endif // ENABLE_SYNC

    bool hardLink(const LocalPath& source, const LocalPath& target) override;

    m_off_t availableDiskSpace(const LocalPath& drivePath) override;
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

    bool fopen(const LocalPath&, bool read, bool write, DirAccess* iteratingDir = nullptr, bool ignoreAttributes = false, bool skipcasecheck = false) override;

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

    void addnotify(LocalNode*, const LocalPath&) override;
    void delnotify(LocalNode*) override;

    PosixDirNotify(const LocalPath&, const LocalPath&, Sync* s);
};
#endif

} // namespace

#endif
