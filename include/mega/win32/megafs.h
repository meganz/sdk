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

#include "mega.h"

#define DEBRISFOLDER "Rubbish"

namespace mega {

class MEGA_API WinFileAccess;

struct MEGA_API WinDirAccess : public DirAccess
{
    bool ffdvalid;
    WIN32_FIND_DATAW ffd;
    HANDLE hFind;
    LocalPath globbase;

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

    unique_ptr<DirAccess>  newdiraccess() override;

#ifdef ENABLE_SYNC
    DirNotify* newdirnotify(LocalNode& root,
                            const LocalPath& rootPath,
                            Waiter* notificationWaiter) override;
#endif

    bool issyncsupported(const LocalPath&, bool&, SyncError&, SyncWarning&) override;

    bool getsname(const LocalPath&, LocalPath&) const override;

    bool renamelocal(const LocalPath&, const LocalPath&, bool) override;
    bool copylocal(const LocalPath&, const LocalPath&, m_time_t) override;
    bool unlinklocal(const LocalPath&) override;
    bool rmdirlocal(const LocalPath&) override;
    bool mkdirlocal(const LocalPath&, bool hidden, bool logAlreadyExistsError) override;
    bool setmtimelocal(const LocalPath&, m_time_t) override;
    bool chdirlocal(LocalPath&) const override;
    bool expanselocalpath(const LocalPath& path, LocalPath& absolutepath) override;

    void addevents(Waiter*, int) override;

    static bool istransient(DWORD);
    bool istransientorexists(DWORD);

    bool exists(const LocalPath& path) const;
    bool isPathError(DWORD error) const;

    void osversion(string*, bool includeArchExtraInfo) const override;
    void statsid(string*) const override;

    static void emptydirlocal(const LocalPath&, dev_t = 0);

    ScanResult directoryScan(const LocalPath& path, handle expectedFsid,
        map<LocalPath, FSNode>& known, std::vector<FSNode>& results, bool followSymlinks, unsigned& nFingerprinted) override;

    WinFileSystemAccess();
    ~WinFileSystemAccess();

    bool cwd(LocalPath& path) const override;

#ifdef ENABLE_SYNC
    bool fsStableIDs(const LocalPath& path) const override;

    std::set<WinDirNotify*> dirnotifys;
#endif

    bool hardLink(const LocalPath& source, const LocalPath& target) override;

    m_off_t availableDiskSpace(const LocalPath& drivePath) override;

    static bool checkForSymlink(const LocalPath& lp);
};

#ifdef ENABLE_SYNC
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
    void process(DWORD bytesTransferred);
    void readchanges();

    static std::atomic<unsigned> smNotifierCount;
    static std::mutex smNotifyMutex;
    static HANDLE smEventHandle;
    static std::deque<std::function<void()>> smQueue;
    static std::unique_ptr<std::thread> smNotifierThread;

    static void notifierThreadFunction();

public:
    WinDirNotify(LocalNode& root,
                 const LocalPath& rootPath,
                 WinFileSystemAccess* owner,
                 Waiter* waiter);

    ~WinDirNotify();
};
#endif

struct MEGA_API WinAsyncIOContext : public AsyncIOContext
{
    WinAsyncIOContext();
    virtual ~WinAsyncIOContext();
    virtual void finish();

    OVERLAPPED *overlapped;
};

class MEGA_API WinFileAccess : public FileAccess
{
    HANDLE hFile;

public:
    HANDLE hFind;
    WIN32_FIND_DATAW ffd;

    bool fopen(const LocalPath&, bool read, bool write, FSLogging,
               DirAccess* iteratingDir, bool ignoreAttributes, bool skipcasecheck, LocalPath* actualLeafNameIfDifferent) override;
    bool fopen_impl(const LocalPath&, bool read, bool write, FSLogging,
                    bool async, DirAccess* iteratingDir, bool ignoreAttributes, bool skipcasecheck, LocalPath* actualLeafNameIfDifferent);
    void updatelocalname(const LocalPath&, bool force) override;
    bool fread(string *, unsigned, unsigned, m_off_t);
    void fclose() override;
    bool fwrite(const void* buffer,
                unsigned long length,
                m_off_t position,
                unsigned long* numWritten = nullptr,
                bool* retry = nullptr) override;

    bool fstat(m_time_t& modified, m_off_t& fileSize) override;

    bool ftruncate(m_off_t newSize) override;

    bool sysread(void* buffer, unsigned long length, m_off_t offset, bool* retry) override;
    bool sysstat(m_time_t*, m_off_t*, FSLogging) override;
    bool sysopen(bool async, FSLogging) override;
    void sysclose() override;

    // async interface
    bool asyncavailable() override;
    void asyncsysopen(AsyncIOContext* context) override;
    void asyncsysread(AsyncIOContext* context) override;
    void asyncsyswrite(AsyncIOContext* context) override;

    static bool skipattributes(DWORD);

    WinFileAccess(Waiter *w);
    ~WinFileAccess();

    // Mark this file as a sparse file.
    bool setSparse() override;

protected:
    AsyncIOContext* newasynccontext() override;
    static VOID CALLBACK asyncopfinished(
            DWORD        dwErrorCode,
            DWORD        dwNumberOfBytesTransfered,
            LPOVERLAPPED lpOverlapped);
};
} // namespace

#endif
