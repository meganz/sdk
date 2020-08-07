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

class MEGA_API WinFileAccess;

struct MEGA_API WinDirAccess : public DirAccess
{
    bool ffdvalid;
    WIN32_FIND_DATAW ffd;
    HANDLE hFind;
    string globbase;

    WIN32_FIND_DATAW currentItemAttributes;
    friend class WinFileAccess;

public:
    bool dopen(LocalPath*, FileAccess*, bool) override;
    bool dnext(LocalPath&, LocalPath&, bool, nodetype_t*) override;

    WinDirAccess();
    virtual ~WinDirAccess();
};

struct MEGA_API WinDirNotify;
class MEGA_API WinFileSystemAccess : public FileSystemAccess
{
public:
    using FileSystemAccess::getlocalfstype;

    std::unique_ptr<FileAccess> newfileaccess(bool followSymLinks = true) override;
    
    bool getlocalfstype(const LocalPath& path, FileSystemType& type) const override;

    DirAccess* newdiraccess() override;
    DirNotify* newdirnotify(LocalPath&, LocalPath&, Waiter*) override;

    bool issyncsupported(LocalPath&, bool* = NULL) override;

    void tmpnamelocal(LocalPath&) const override;

    void path2local(const string*, string*) const override;
    void local2path(const string*, string*) const override;

    static int sanitizedriveletter(LocalPath&);

    bool getsname(LocalPath&, LocalPath&) const override;

    bool renamelocal(LocalPath&, LocalPath&, bool) override;
    bool copylocal(LocalPath&, LocalPath&, m_time_t) override;
    bool unlinklocal(LocalPath&) override;
    bool rmdirlocal(LocalPath&) override;
    bool mkdirlocal(LocalPath&, bool) override;
    bool setmtimelocal(LocalPath&, m_time_t) override;
    bool chdirlocal(LocalPath&) const override;
    size_t lastpartlocal(const string*) const override;
    bool getextension(const LocalPath&, char*, size_t) const override;
    bool expanselocalpath(LocalPath& path, LocalPath& absolutepath) override;

    void addevents(Waiter*, int) override;

    static bool istransient(DWORD);
    bool istransientorexists(DWORD);

    void osversion(string*, bool includeArchExtraInfo) const override;
    void statsid(string*) const override;

    static void emptydirlocal(LocalPath&, dev_t = 0);

    WinFileSystemAccess();
    ~WinFileSystemAccess();

    std::set<WinDirNotify*> dirnotifys;
};

struct MEGA_API WinDirNotify : public DirNotify
{
private:
    WinFileSystemAccess* fsaccess;

#ifdef ENABLE_SYNC
    LocalNode* localrootnode;
#endif

    HANDLE hDirectory;

    std::atomic<bool> mOverlappedExit;
    std::atomic<bool> mOverlappedEnabled;

    Waiter* clientWaiter;

    string notifybuf;
    DWORD dwBytes;
    OVERLAPPED overlapped;

    static VOID CALLBACK completion(DWORD dwErrorCode, DWORD dwBytes, LPOVERLAPPED lpOverlapped);
    void process(DWORD wNumberOfBytesTransfered);
    void readchanges();

    static std::atomic<unsigned> smNotifierCount;
    static std::mutex smNotifyMutex;
    static HANDLE smEventHandle;
    static std::deque<std::function<void()>> smQueue;
    static std::unique_ptr<std::thread> smNotifierThread;

    static void notifierThreadFunction();

public:

    void addnotify(LocalNode*, string*) override;

    fsfp_t fsfingerprint() const override;
    bool fsstableids() const override;
    
    WinDirNotify(LocalPath&, const LocalPath&, WinFileSystemAccess* owner, Waiter* waiter);
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

    bool fopen(LocalPath&, bool read, bool write, DirAccess* iteratingDir, bool ignoreAttributes) override;
    bool fopen_impl(LocalPath&, bool read, bool write, bool async, DirAccess* iteratingDir, bool ignoreAttributes);
    void updatelocalname(LocalPath&) override;
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
