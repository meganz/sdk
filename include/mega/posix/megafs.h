/**
 * @file mega/posix/megafs.h
 * @brief POSIX filesystem/directory access/notification
 *
 * (c) 2013-2014 by Mega Limited, Wellsford, New Zealand
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
    bool dnext(string*, nodetype_t* = NULL);

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

    bool notifyerr;

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
    bool setmtimelocal(string *, m_time_t) const;
    bool chdirlocal(string*) const;
    size_t lastpartlocal(string*) const;
    bool getextension(string*, char*, int) const;

    void addevents(Waiter*, int);
    int checkevents(Waiter*);

    void osversion(string*) const;

    PosixFileSystemAccess(int = -1);
    ~PosixFileSystemAccess();
};

class MEGA_API PosixFileAccess : public FileAccess
{
public:
    int fd;

#ifndef USE_FDOPENDIR
    DIR* dp;
#endif

    bool fopen(string*, bool, bool);
    void updatelocalname(string*);
    bool fread(string *, unsigned, unsigned, m_off_t);
    bool frawread(byte *, unsigned, m_off_t);
    bool fwrite(const byte *, unsigned, m_off_t);

    bool sysread(byte *, unsigned, m_off_t);
    bool sysstat(m_time_t*, m_off_t*);
    bool sysopen();
    void sysclose();

    PosixFileAccess();
    ~PosixFileAccess();
};

class MEGA_API PosixDirNotify : public DirNotify
{
public:
    PosixFileSystemAccess* fsaccess;

    void addnotify(LocalNode*, string*);
    void delnotify(LocalNode*);

    PosixDirNotify(string*, string*);
};
} // namespace

#endif
