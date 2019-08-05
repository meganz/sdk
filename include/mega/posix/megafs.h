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

    bool dopen(string*, FileAccess*, bool);
    bool dnext(string*, string*, bool, nodetype_t*);

    PosixDirAccess();
    virtual ~PosixDirAccess();
};

class MEGA_API PosixFileSystemAccess : public FileSystemAccess
{
public:
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

    FileAccess* newfileaccess();
    DirAccess* newdiraccess();
    DirNotify* newdirnotify(string*, string*);

    void tmpnamelocal(string*) const;

    void local2path(string*, string*) const;
    void path2local(string*, string*) const;

    bool getsname(string*, string*) const;

    bool renamelocal(string*, string*, bool);
    bool copylocal(string*, string*, m_time_t);
    bool rubbishlocal(string*);
    bool unlinklocal(string*);
    bool rmdirlocal(string*);
    bool mkdirlocal(string*, bool);
    bool setmtimelocal(string *, m_time_t);
    bool chdirlocal(string*) const;
    size_t lastpartlocal(string*) const;
    bool getextension(string*, char*, size_t) const;
    bool expanselocalpath(string *path, string *absolutepath);

    void addevents(Waiter*, int);
    int checkevents(Waiter*);

    void osversion(string*) const;
    void statsid(string*) const;

    static void emptydirlocal(string*, dev_t = 0);

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
public:
    int fd;
    int defaultfilepermissions;

#ifndef HAVE_FDOPENDIR
    DIR* dp;
#endif

    bool fopen(string*, bool, bool);
    void updatelocalname(string*);
    bool fread(string *, unsigned, unsigned, m_off_t);
    bool fwrite(const byte *, unsigned, m_off_t);

    bool sysread(byte *, unsigned, m_off_t);
    bool sysstat(m_time_t*, m_off_t*);
    bool sysopen(bool async = false);
    void sysclose();

    PosixFileAccess(Waiter *w, int defaultfilepermissions = 0600);

    // async interface
    virtual bool asyncavailable();
    virtual void asyncsysopen(AsyncIOContext* context);
    virtual void asyncsysread(AsyncIOContext* context);
    virtual void asyncsyswrite(AsyncIOContext* context);

    ~PosixFileAccess();

#ifdef HAVE_AIO_RT
protected:
    virtual AsyncIOContext* newasynccontext();
    static void asyncopfinished(union sigval sigev_value);
#endif
};

class MEGA_API PosixDirNotify : public DirNotify
{
public:
    PosixFileSystemAccess* fsaccess;

    void addnotify(LocalNode*, string*) override;
    void delnotify(LocalNode*) override;

    fsfp_t fsfingerprint() const override;
    bool fsstableids() const override;

    PosixDirNotify(string*, string*);
};
} // namespace

#endif
