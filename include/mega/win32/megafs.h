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
    bool dopen(string*, FileAccess*, bool) override;
    bool dnext(string*, string*, bool, nodetype_t*) override;

    WinDirAccess();
    virtual ~WinDirAccess();
};

struct MEGA_API WinDirNotify;
class MEGA_API WinFileSystemAccess : public FileSystemAccess
{
public:
    FileAccess* newfileaccess() override;
    DirAccess* newdiraccess() override;
    DirNotify* newdirnotify(string*, string*) override;

    bool issyncsupported(string*, bool* = NULL) override;

    void tmpnamelocal(string*) const override;

    void path2local(string*, string*) const override;
    void local2path(string*, string*) const override;

    static int sanitizedriveletter(string*);

    bool getsname(string*, string*) const override;

    bool renamelocal(string*, string*, bool) override;
    bool copylocal(string*, string*, m_time_t) override;
    bool unlinklocal(string*) override;
    bool rmdirlocal(string*) override;
    bool mkdirlocal(string*, bool) override;
    bool setmtimelocal(string *, m_time_t) override;
    bool chdirlocal(string*) const override;
    size_t lastpartlocal(string*) const override;
    bool getextension(string*, char*, size_t) const override;
    bool expanselocalpath(string *path, string *absolutepath) override;

    void addevents(Waiter*, int) override;

    static bool istransient(DWORD);
    bool istransientorexists(DWORD);

    void osversion(string*) const override;
    void statsid(string*) const override;

    static void emptydirlocal(string*, dev_t = 0);

    WinFileSystemAccess();
    ~WinFileSystemAccess();

    std::set<WinDirNotify*> dirnotifys;
};

struct MEGA_API WinDirNotify : public DirNotify
{
    WinFileSystemAccess* fsaccess;

#ifdef ENABLE_SYNC
    LocalNode* localrootnode;
#endif

    HANDLE hDirectory;

    bool enabled;
    bool exit;
    int active;
    string notifybuf[2];

    DWORD dwBytes;
    OVERLAPPED overlapped;

    void addnotify(LocalNode*, string*) override;

    static VOID CALLBACK completion(DWORD dwErrorCode, DWORD dwBytes, LPOVERLAPPED lpOverlapped);
    void process(DWORD wNumberOfBytesTransfered);
    void readchanges();

    fsfp_t fsfingerprint() const override;
    bool fsstableids() const override;

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
    void updatelocalname(string*) override;
    bool fread(string *, unsigned, unsigned, m_off_t);
    bool fwrite(const byte *, unsigned, m_off_t);

    bool sysread(byte *, unsigned, m_off_t) override;
    bool sysstat(m_time_t*, m_off_t*) override;
    bool sysopen(bool async = false) override;
    void sysclose() override;

    // async interface
    bool asyncavailable() override;
    void asyncsysopen(AsyncIOContext* context) override;
    void asyncsysread(AsyncIOContext* context) override;
    void asyncsyswrite(AsyncIOContext* context) override;

    static bool skipattributes(DWORD);

    WinFileAccess(Waiter *w);
    ~WinFileAccess();

protected:
#ifndef WINDOWS_PHONE
    AsyncIOContext* newasynccontext() override;
    static VOID CALLBACK asyncopfinished(
            DWORD        dwErrorCode,
            DWORD        dwNumberOfBytesTransfered,
            LPOVERLAPPED lpOverlapped);
#endif
};
} // namespace

#endif
