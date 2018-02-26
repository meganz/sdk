/**
 * @file mega/win32/megafs.h
 * @brief Win32 filesystem/directory access/notification (Unicode)
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
#define FSACCESS_CLASS WinFileSystemAccess

#define DEBRISFOLDER "Rubbish"

namespace mega {
struct MEGA_API WinDirAccess : public DirAccess
{
    bool ffdvalid;
    WIN32_FIND_DATAW ffd;
    HANDLE hFind;
    string globbase;

public:
    bool dopen(string*, FileAccess*, bool);
    bool dnext(string*, string*, bool, nodetype_t*);

    WinDirAccess();
    virtual ~WinDirAccess();
};

struct MEGA_API WinDirNotify;
class MEGA_API WinFileSystemAccess : public FileSystemAccess
{
public:
    FileAccess* newfileaccess();
    DirAccess* newdiraccess();
    DirNotify* newdirnotify(string*, string*);

    bool issyncsupported(string*, bool* = NULL);

    void tmpnamelocal(string*) const;

    void path2local(string*, string*) const;
    void local2path(string*, string*) const;

    static int sanitizedriveletter(string*);

    bool getsname(string*, string*) const;

    bool renamelocal(string*, string*, bool);
    bool copylocal(string*, string*, m_time_t);
    bool unlinklocal(string*);
    bool rmdirlocal(string*);
    bool mkdirlocal(string*, bool);
    bool setmtimelocal(string *, m_time_t);
    bool chdirlocal(string*) const;
    size_t lastpartlocal(string*) const;
    bool getextension(string*, char*, int) const;
    bool expanselocalpath(string *path, string *absolutepath);

    void addevents(Waiter*, int);

    static bool istransient(DWORD);
    bool istransientorexists(DWORD);

    void osversion(string*) const;
    void statsid(string*) const;

    static void emptydirlocal(string*, dev_t = 0);

    WinFileSystemAccess();
    ~WinFileSystemAccess();

    std::set<WinDirNotify*> dirnotifys;
};

struct MEGA_API WinDirNotify : public DirNotify
{
    WinFileSystemAccess* fsaccess;

    LocalNode* localrootnode;

    HANDLE hDirectory;

    bool enabled;
    bool exit;
    int active;
    string notifybuf[2];

    DWORD dwBytes;
    OVERLAPPED overlapped;

    void addnotify(LocalNode*, string*);

    static VOID CALLBACK completion(DWORD dwErrorCode, DWORD dwBytes, LPOVERLAPPED lpOverlapped);
    void process(DWORD wNumberOfBytesTransfered);
    void readchanges();

    fsfp_t fsfingerprint();

    WinDirNotify(string*, string*);
    ~WinDirNotify();
};

#ifndef WINDOWS_PHONE
struct MEGA_API WinAsyncIOContext : public AsyncIOContext
{
    WinAsyncIOContext();
    virtual ~WinAsyncIOContext();
    virtual void finish();

    OVERLAPPED *overlapped;
};
#endif

class MEGA_API WinFileAccess : public FileAccess
{
    HANDLE hFile;

public:
    HANDLE hFind;
    WIN32_FIND_DATAW ffd;

    bool fopen(string*, bool, bool);
    bool fopen(string*, bool, bool, bool);
    void updatelocalname(string*);
    bool fread(string *, unsigned, unsigned, m_off_t);
    bool frawread(byte *, unsigned, m_off_t);
    bool fwrite(const byte *, unsigned, m_off_t);

    bool sysread(byte *, unsigned, m_off_t);
    bool sysstat(m_time_t*, m_off_t*);
    bool sysopen(bool async = false);
    void sysclose();

    // async interface
    virtual bool asyncavailable();
    virtual void asyncsysopen(AsyncIOContext* context);
    virtual void asyncsysread(AsyncIOContext* context);
    virtual void asyncsyswrite(AsyncIOContext* context);

    static bool skipattributes(DWORD);

    WinFileAccess(Waiter *w);
    ~WinFileAccess();

protected:
#ifndef WINDOWS_PHONE
    virtual AsyncIOContext* newasynccontext();
    static VOID CALLBACK asyncopfinished(
            DWORD        dwErrorCode,
            DWORD        dwNumberOfBytesTransfered,
            LPOVERLAPPED lpOverlapped);
#endif
};
} // namespace

#endif
