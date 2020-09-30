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

    bool notifyerr;
    int defaultfilepermissions;
    int defaultfolderpermissions;

    std::unique_ptr<FileAccess> newfileaccess(bool followSymLinks = true) override;
    DirAccess* newdiraccess() override;
    DirNotify* newdirnotify(LocalPath&, LocalPath&, Waiter*) override;

    bool getlocalfstype(const LocalPath& path, FileSystemType& type) const override;

    void tmpnamelocal(LocalPath&) const override;

    void local2path(const string*, string*) const override;
    void path2local(const string*, string*) const override;

    bool getsname(LocalPath&, LocalPath&) const override;

    bool renamelocal(LocalPath&, LocalPath&, bool) override;
    bool copylocal(LocalPath&, LocalPath&, m_time_t) override;
    bool rubbishlocal(string*);
    bool unlinklocal(LocalPath&) override;
    bool rmdirlocal(LocalPath&) override;
    bool mkdirlocal(LocalPath&, bool) override;
    bool setmtimelocal(LocalPath&, m_time_t) override;
    bool chdirlocal(LocalPath&) const override;
    size_t lastpartlocal(const string*) const override;
    bool getextension(const LocalPath&, char*, size_t) const override;
    bool expanselocalpath(LocalPath& path, LocalPath& absolutepath) override;

    void addevents(Waiter*, int) override;
    int checkevents(Waiter*) override;

    void osversion(string*, bool includeArchitecture) const override;
    void statsid(string*) const override;

    static void emptydirlocal(LocalPath&, dev_t = 0);

    int getdefaultfilepermissions();
    void setdefaultfilepermissions(int);
    int getdefaultfolderpermissions();
    void setdefaultfolderpermissions(int);

    PosixFileSystemAccess(int = -1);
    ~PosixFileSystemAccess();
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

    bool fopen(LocalPath&, bool read, bool write, DirAccess* iteratingDir = nullptr, bool ignoreAttributes = false) override;

    void updatelocalname(LocalPath&) override;
    bool fread(string *, unsigned, unsigned, m_off_t);
    bool fwrite(const byte *, unsigned, m_off_t) override;

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

class MEGA_API PosixDirNotify : public DirNotify
{
public:
    PosixFileSystemAccess* fsaccess;

    void addnotify(LocalNode*, string*) override;
    void delnotify(LocalNode*) override;

    fsfp_t fsfingerprint() const override;
    bool fsstableids() const override;

    PosixDirNotify(LocalPath&, const LocalPath&);
};

} // namespace

#endif
