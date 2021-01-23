/**
 * @file win32/fs.cpp
 * @brief Win32 filesystem/directory access/notification
 *
 * (c) 2013 by Mega Limited, Auckland, New Zealand
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

#include <cwctype>

#include "mega.h"
#include <wow64apiset.h>

#if defined(_WIN32) || defined(WINDOWS_PHONE)
#include <winsock2.h>
#include <Windows.h>
#endif

namespace mega {

WinFileSystemAccess gWfsa;

int sanitizedriveletter(std::wstring& localpath);

LocalPath NormalizeAbsolute(const LocalPath& path)
{
    LocalPath result = path;

    // Convenience.
    wstring& raw = result.localpath;

    // Absolute paths should never be empty.
    assert(!raw.empty());

    // Add a drive separator if necessary.
    if (raw.back() == L':')
    {
        raw.push_back(L'\\');
    }

    if (raw.size() > 1)
    {
        // Remove trailing separator if we're not the root.
        if (raw.back() == L'\\')
        {
            if (raw[raw.size() - 2] != L':')
            {
                raw.pop_back();
            }
        }
    }

    return result;
}

int platformCompareUtf(const string& p1, bool unescape1, const string& p2, bool unescape2)
{
    return compareUtf(p1, unescape1, p2, unescape2, true);
}

int platformCompareUtf(const string& p1, bool unescape1, const LocalPath& p2, bool unescape2)
{
    return compareUtf(p1, unescape1, p2, unescape2, true);
}

int platformCompareUtf(const LocalPath& p1, bool unescape1, const string& p2, bool unescape2)
{
    return compareUtf(p1, unescape1, p2, unescape2, true);
}

int platformCompareUtf(const LocalPath& p1, bool unescape1, const LocalPath& p2, bool unescape2)
{
    return compareUtf(p1, unescape1, p2, unescape2, true);
}

WinFileAccess::WinFileAccess(Waiter *w) : FileAccess(w)
{
    hFile = INVALID_HANDLE_VALUE;
    hFind = INVALID_HANDLE_VALUE;

    fsidvalid = false;
}

WinFileAccess::~WinFileAccess()
{
    if (hFile != INVALID_HANDLE_VALUE)
    {
        CloseHandle(hFile);
        assert(hFind == INVALID_HANDLE_VALUE);
    }
    else if (hFind != INVALID_HANDLE_VALUE)
    {
        FindClose(hFind);
    }
}

bool WinFileAccess::sysread(byte* dst, unsigned len, m_off_t pos)
{
    DWORD dwRead;
    assert(hFile != INVALID_HANDLE_VALUE);

    if (!SetFilePointerEx(hFile, *(LARGE_INTEGER*)&pos, NULL, FILE_BEGIN))
    {
        DWORD e = GetLastError();
        retry = WinFileSystemAccess::istransient(e);
        LOG_err << "SetFilePointerEx failed for reading. Error: " << e;
        return false;
    }

    if (!ReadFile(hFile, (LPVOID)dst, (DWORD)len, &dwRead, NULL))
    {
        DWORD e = GetLastError();
        retry = WinFileSystemAccess::istransient(e);
        LOG_err << "ReadFile failed. Error: " << e;
        return false;
    }

    if (dwRead != len)
    {
        retry = false;
        LOG_err << "ReadFile failed (dwRead) " << dwRead << " - " << len;
        return false;
    }
    return true;
}

bool WinFileAccess::fwrite(const byte* data, unsigned len, m_off_t pos)
{
    DWORD dwWritten;

    if (!SetFilePointerEx(hFile, *(LARGE_INTEGER*)&pos, NULL, FILE_BEGIN))
    {
        DWORD e = GetLastError();
        retry = WinFileSystemAccess::istransient(e);
        LOG_err << "SetFilePointerEx failed for writing. Error: " << e;
        return false;
    }

    if (!WriteFile(hFile, (LPCVOID)data, (DWORD)len, &dwWritten, NULL))
    {
        DWORD e = GetLastError();
        retry = WinFileSystemAccess::istransient(e);
        LOG_err << "WriteFile failed. Error: " << e;
        return false;
    }

     if (dwWritten != len)
     {
         retry = false;
         LOG_err << "WriteFile failed (dwWritten) " << dwWritten << " - " << len;
         return false;
     }

     if (!FlushFileBuffers(hFile))
     {
         DWORD e = GetLastError();
         retry = WinFileSystemAccess::istransient(e);
         LOG_err << "FlushFileBuffers failed. Error: " << e;
         return false;
     }
     return true;
}

bool WinFileAccess::ftruncate()
{
    LARGE_INTEGER zero;

    zero.QuadPart = 0x0;

    // Set the file pointer to the start of the file.
    if (SetFilePointerEx(hFile, zero, nullptr, FILE_BEGIN))
    {
        // Truncate the file.
        if (SetEndOfFile(hFile))
        {
            return true;
        }
    }

    // Why couldn't we truncate the file?
    auto error = GetLastError();

    // Is it a transient error?
    retry = WinFileSystemAccess::istransient(error);

    return false;
}

m_time_t FileTime_to_POSIX(FILETIME* ft)
{
    LARGE_INTEGER date;

    date.HighPart = ft->dwHighDateTime;
    date.LowPart = ft->dwLowDateTime;

    // remove the diff between 1970 and 1601 and convert back from 100-nanoseconds to seconds
    int64_t t = date.QuadPart - 11644473600000 * 10000;

    // clamp
    if (t < 0) return 0;

    t /= 10000000;

    FileSystemAccess::captimestamp(&t);

    return t;
}

bool WinFileAccess::sysstat(m_time_t* mtime, m_off_t* size)
{
    assert(!nonblocking_localname.empty());
    WIN32_FILE_ATTRIBUTE_DATA fad;

    type = TYPE_UNKNOWN;
    if (!GetFileAttributesExW(nonblocking_localname.localpath.c_str(), GetFileExInfoStandard, (LPVOID)&fad))
    {
        DWORD e = GetLastError();
        errorcode = e;
        retry = WinFileSystemAccess::istransient(e);
        return false;
    }

    errorcode = 0;
    if (SimpleLogger::logCurrentLevel >= logDebug && skipattributes(fad.dwFileAttributes))
    {
        LOG_debug << "Incompatible attributes (" << fad.dwFileAttributes << ") for file " << nonblocking_localname.toPath(gWfsa);
    }

    if (fad.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
    {
        type = FOLDERNODE;
        retry = false;
        return false;
    }

    retry = false;
    type = FILENODE;
    *mtime = FileTime_to_POSIX(&fad.ftLastWriteTime);
    *size = ((m_off_t)fad.nFileSizeHigh << 32) + (m_off_t)fad.nFileSizeLow;

    return true;
}

bool WinFileAccess::sysopen(bool async)
{
    assert(hFile == INVALID_HANDLE_VALUE);
    assert(!nonblocking_localname.empty());

    if (hFile != INVALID_HANDLE_VALUE)
    {
        sysclose();
    }

#ifdef WINDOWS_PHONE
    hFile = CreateFile2(localname.localpath.data(), GENERIC_READ,
                        FILE_SHARE_WRITE | FILE_SHARE_READ,
                        OPEN_EXISTING, NULL);
#else
    hFile = CreateFileW(nonblocking_localname.localpath.c_str(), GENERIC_READ,
                        FILE_SHARE_WRITE | FILE_SHARE_READ,
                        NULL, OPEN_EXISTING, async ? FILE_FLAG_OVERLAPPED : 0, NULL);
#endif

    if (hFile == INVALID_HANDLE_VALUE)
    {
        DWORD e = GetLastError();
        LOG_debug << "Unable to open file (sysopen). Error code: " << e;
        retry = WinFileSystemAccess::istransient(e);
        return false;
    }

    return true;
}

void WinFileAccess::sysclose()
{
    assert(!nonblocking_localname.empty());
    assert(hFile != INVALID_HANDLE_VALUE);

    if (hFile != INVALID_HANDLE_VALUE)
    {
        CloseHandle(hFile);
        hFile = INVALID_HANDLE_VALUE;
    }
}

#ifndef WINDOWS_PHONE

WinAsyncIOContext::WinAsyncIOContext() : AsyncIOContext()
{
    overlapped = NULL;
}

WinAsyncIOContext::~WinAsyncIOContext()
{
    LOG_verbose << "Deleting WinAsyncIOContext";
    finish();
}

void WinAsyncIOContext::finish()
{
    if (overlapped)
    {
        if (!finished)
        {
            LOG_debug << "Synchronously waiting for async operation";
            AsyncIOContext::finish();
        }

        delete overlapped;
        overlapped = NULL;
    }
    assert(finished);
}

AsyncIOContext *WinFileAccess::newasynccontext()
{
    return new WinAsyncIOContext();
}

VOID WinFileAccess::asyncopfinished(DWORD dwErrorCode, DWORD dwNumberOfBytesTransfered, LPOVERLAPPED lpOverlapped)
{
    WinAsyncIOContext *context = (WinAsyncIOContext *)(lpOverlapped->hEvent);
    context->failed = dwErrorCode || dwNumberOfBytesTransfered != context->dataBufferLen;
    if (!context->failed)
    {
        if (context->op == AsyncIOContext::READ)
        {
            memset(context->dataBuffer + context->dataBufferLen, 0, context->pad);
            LOG_verbose << "Async read finished OK";
        }
        else
        {
            LOG_verbose << "Async write finished OK";
        }
    }
    else
    {
        LOG_warn << "Async operation finished with error: " << dwErrorCode;
    }

    context->retry = WinFileSystemAccess::istransient(dwErrorCode);
    context->finished = true;
    if (context->userCallback)
    {
        context->userCallback(context->userData);
    }
}

#endif

bool WinFileAccess::asyncavailable()
{
#ifdef WINDOWS_PHONE
	return false;
#endif
    return true;
}

void WinFileAccess::asyncsysopen(AsyncIOContext *context)
{
#ifndef WINDOWS_PHONE
    bool read = context->access & AsyncIOContext::ACCESS_READ;
    bool write = context->access & AsyncIOContext::ACCESS_WRITE;

    context->failed = !fopen_impl(context->openPath, read, write, true, nullptr, false);
    context->retry = retry;
    context->finished = true;
    if (context->userCallback)
    {
        context->userCallback(context->userData);
    }
#endif
}

void WinFileAccess::asyncsysread(AsyncIOContext *context)
{
#ifndef WINDOWS_PHONE
    if (!context)
    {
        return;
    }

    WinAsyncIOContext *winContext = dynamic_cast<WinAsyncIOContext*>(context);
    if (!winContext)
    {
        context->failed = true;
        context->retry = false;
        context->finished = true;
        if (context->userCallback)
        {
            context->userCallback(context->userData);
        }
        return;
    }

    OVERLAPPED *overlapped = new OVERLAPPED;
    memset(overlapped, 0, sizeof (OVERLAPPED));

    overlapped->Offset = winContext->posOfBuffer & 0xFFFFFFFF;
    overlapped->OffsetHigh = (winContext->posOfBuffer >> 32) & 0xFFFFFFFF;
    overlapped->hEvent = winContext;
    winContext->overlapped = overlapped;

    if (!ReadFileEx(hFile, winContext->dataBuffer, (DWORD)winContext->dataBufferLen,
                   overlapped, asyncopfinished))
    {
        DWORD e = GetLastError();
        winContext->retry = WinFileSystemAccess::istransient(e);
        winContext->failed = true;
        winContext->finished = true;
        winContext->overlapped = NULL;
        delete overlapped;

        LOG_warn << "Async read failed at startup: " << e;
        if (winContext->userCallback)
        {
            winContext->userCallback(winContext->userData);
        }
    }
#endif
}

void WinFileAccess::asyncsyswrite(AsyncIOContext *context)
{
#ifndef WINDOWS_PHONE
    if (!context)
    {
        return;
    }

    WinAsyncIOContext *winContext = dynamic_cast<WinAsyncIOContext*>(context);
    if (!winContext)
    {
        context->failed = true;
        context->retry = false;
        context->finished = true;
        if (context->userCallback)
        {
            context->userCallback(context->userData);
        }
        return;
    }

    OVERLAPPED *overlapped = new OVERLAPPED;
    memset(overlapped, 0, sizeof (OVERLAPPED));
    overlapped->Offset = winContext->posOfBuffer & 0xFFFFFFFF;
    overlapped->OffsetHigh = (winContext->posOfBuffer >> 32) & 0xFFFFFFFF;
    overlapped->hEvent = winContext;
    winContext->overlapped = overlapped;

    if (!WriteFileEx(hFile, winContext->dataBuffer, (DWORD)winContext->dataBufferLen,
                   overlapped, asyncopfinished))
    {
        DWORD e = GetLastError();
        winContext->retry = WinFileSystemAccess::istransient(e);
        winContext->failed = true;
        winContext->finished = true;
        winContext->overlapped = NULL;
        delete overlapped;

        LOG_warn << "Async write failed at startup: "  << e;
        if (winContext->userCallback)
        {
            winContext->userCallback(winContext->userData);
        }
    }
#endif
}

// update local name
void WinFileAccess::updatelocalname(const LocalPath& name, bool force)
{
    if (force || !nonblocking_localname.empty())
    {
        nonblocking_localname = name;
        sanitizedriveletter(nonblocking_localname.localpath);
    }
}

// true if attribute set should not be considered for syncing
// (SYSTEM files are only synced if they are not HIDDEN)
bool WinFileAccess::skipattributes(DWORD dwAttributes)
{
    return (dwAttributes & (FILE_ATTRIBUTE_REPARSE_POINT
          | FILE_ATTRIBUTE_OFFLINE))
        || (dwAttributes & (FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_HIDDEN))
            == (FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_HIDDEN);
}

// emulates Linux open-directory-as-file semantics
// FIXME #1: How to open files and directories with a single atomic
// CreateFile() operation without first looking at the attributes?
// FIXME #2: How to convert a CreateFile()-opened directory directly to a hFind
// without doing a FindFirstFile()?
bool WinFileAccess::fopen(LocalPath& name, bool read, bool write, DirAccess* iteratingDir, bool ignoreAttributes)
{
    return fopen_impl(name, read, write, false, iteratingDir, ignoreAttributes);
}

bool WinFileAccess::fopen_impl(LocalPath& namePath, bool read, bool write, bool async, DirAccess* iteratingDir, bool ignoreAttributes)
{
    WIN32_FIND_DATA fad = { 0 };
    assert(hFile == INVALID_HANDLE_VALUE);

#ifdef WINDOWS_PHONE
    FILE_ID_INFO bhfi = { 0 };
#else
    BY_HANDLE_FILE_INFORMATION bhfi = { 0 };
#endif

    bool skipcasecheck = false;

    ScopedLengthRestore restoreNamePath(namePath);
    sanitizedriveletter(namePath.localpath);

    if (write)
    {
        type = FILENODE;
    }
    else
    {
        // fill in the `fad` file attribute data in the most efficient way available for its case
        if (iteratingDir)
        {
            fad = static_cast<WinDirAccess*>(iteratingDir)->currentItemAttributes;
        }
        else
        {
            HANDLE  h = namePath.localpath.size() > 1
                    ? FindFirstFileExW(namePath.localpath.c_str(), FindExInfoStandard, &fad,
                                 FindExSearchNameMatch, NULL, 0)
                    : INVALID_HANDLE_VALUE;

            if (h != INVALID_HANDLE_VALUE)
            {
                // success so `fad` is set
                FindClose(h);
            }
            else
            {
                WIN32_FILE_ATTRIBUTE_DATA fatd;
                if (!GetFileAttributesExW(namePath.localpath.c_str(), GetFileExInfoStandard, (LPVOID)&fatd))
                {
                    DWORD e = GetLastError();
                    // this is an expected case so no need to log.  the FindFirstFileEx did not find the file,
                    // GetFileAttributesEx is only expected to find it if it's a network share point
                    // LOG_debug << "Unable to get the attributes of the file. Error code: " << e;
                    retry = WinFileSystemAccess::istransient(e);
                    return false;
                }
                else
                {
                    LOG_debug << "Possible root of network share";
                    skipcasecheck = true;
                    fad.dwFileAttributes = fatd.dwFileAttributes;
                    fad.ftCreationTime = fatd.ftCreationTime;
                    fad.ftLastAccessTime = fatd.ftLastAccessTime;
                    fad.ftLastWriteTime = fatd.ftLastWriteTime;
                    fad.nFileSizeHigh = fatd.nFileSizeHigh;
                    fad.nFileSizeLow = fatd.nFileSizeLow;
                }
            }
        }

        if (!skipcasecheck)
        {
            LocalPath filename = namePath.leafName();

            if (filename.localpath != wstring(fad.cFileName) &&
                filename.localpath != wstring(fad.cAlternateFileName) &&
                filename.localpath != L"." && filename.localpath != L"..")
            {
                LOG_warn << "fopen failed due to invalid case";
                retry = false;
                return false;
            }
        }

        // ignore symlinks - they would otherwise be treated as moves
        // also, ignore some other obscure filesystem object categories
        if (!ignoreAttributes && skipattributes(fad.dwFileAttributes))
        {
            if (SimpleLogger::logCurrentLevel >= logDebug)
            {
                LOG_debug << "Excluded: " << namePath.toPath(gWfsa) << "   Attributes: " << fad.dwFileAttributes;
            }
            retry = false;
            return false;
        }

        type = (fad.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? FOLDERNODE : FILENODE;
    }

    // (race condition between GetFileAttributesEx()/FindFirstFile() possible -
    // fixable with the current Win32 API?)
#ifdef WINDOWS_PHONE
    CREATEFILE2_EXTENDED_PARAMETERS ex = { 0 };
    ex.dwSize = sizeof(ex);

    if (type == FOLDERNODE)
    {
        ex.dwFileFlags = FILE_FLAG_BACKUP_SEMANTICS;
    }
    else if (async)
    {
        ex.dwFileFlags = FILE_FLAG_OVERLAPPED;
    }

    hFile = CreateFile2(name.data(),
                        read ? GENERIC_READ : (write ? GENERIC_WRITE : 0),
                        FILE_SHARE_WRITE | FILE_SHARE_READ,
                        !write ? OPEN_EXISTING : OPEN_ALWAYS,
                        &ex);
#else
    hFile = CreateFileW(namePath.localpath.c_str(),
                        read ? GENERIC_READ : (write ? GENERIC_WRITE : 0),
                        FILE_SHARE_WRITE | FILE_SHARE_READ,
                        NULL,
                        !write ? OPEN_EXISTING : OPEN_ALWAYS,
                        (type == FOLDERNODE) ? FILE_FLAG_BACKUP_SEMANTICS
                                             : (async ? FILE_FLAG_OVERLAPPED : 0),
                        NULL);
#endif

    // FIXME: verify that keeping the directory opened quashes the possibility
    // of a race condition between CreateFile() and FindFirstFile()

    if (hFile == INVALID_HANDLE_VALUE)
    {
        DWORD e = GetLastError();
        LOG_debug << "Unable to open file. Error code: " << e;
        retry = WinFileSystemAccess::istransient(e);
        return false;
    }

    mtime = FileTime_to_POSIX(&fad.ftLastWriteTime);

#ifdef WINDOWS_PHONE
    if (!write && (fsidvalid = !!GetFileInformationByHandleEx(hFile, FileIdInfo, &bhfi, sizeof(bhfi))))
    {
        fsid = *(handle*)&bhfi.FileId;
    }
#else
    if (!write && (fsidvalid = !!GetFileInformationByHandle(hFile, &bhfi)))
    {
        fsid = ((handle)bhfi.nFileIndexHigh << 32) | (handle)bhfi.nFileIndexLow;
    }
#endif

    if (type == FOLDERNODE)
    {
        ScopedLengthRestore undoStar(namePath);
        namePath.appendWithSeparator(LocalPath::fromPlatformEncoded(std::string((const char*)(const wchar_t*)L"*", 2)), true);

#ifdef WINDOWS_PHONE
        hFind = FindFirstFileExW((LPCWSTR)searchName->data(), FindExInfoBasic, &ffd, FindExSearchNameMatch, NULL, 0);
#else
        hFind = FindFirstFileW(namePath.localpath.c_str(), &ffd);
#endif

        if (hFind == INVALID_HANDLE_VALUE)
        {
            DWORD e = GetLastError();
            LOG_debug << "Unable to open folder. Error code: " << e;
            retry = WinFileSystemAccess::istransient(e);
            return false;
        }

        CloseHandle(hFile);
        hFile = INVALID_HANDLE_VALUE;
        retry = false;
        return true;
    }

    if (!write)
    {
        size = ((m_off_t)fad.nFileSizeHigh << 32) + (m_off_t)fad.nFileSizeLow;
        if (!size)
        {
            LOG_debug << "Zero-byte file. mtime: " << mtime << "  ctime: " << FileTime_to_POSIX(&fad.ftCreationTime)
                      << "  attrs: " << fad.dwFileAttributes << "  access: " << FileTime_to_POSIX(&fad.ftLastAccessTime);
        }
    }

    return true;
}

WinFileSystemAccess::WinFileSystemAccess()
{
    notifyerr = false;
    notifyfailed = false;
}

WinFileSystemAccess::~WinFileSystemAccess()
{
    assert(!dirnotifys.size());
}

bool WinFileSystemAccess::cwd(LocalPath& path) const
{
#ifndef WINDOWS_PHONE
    DWORD nRequired = GetCurrentDirectoryW(0, nullptr);

    if (!nRequired)
    {
        return false;
    }

    path.localpath.resize(nRequired);

    DWORD nWritten = GetCurrentDirectoryW(nRequired, &path.localpath[0]);
    path.localpath.resize(nWritten); // doesn't include terminator now
    return nWritten > 0;
#else // WINDOWS_PHONE
    return false;
#endif // ! WINDOWS_PHONE
}

// append \ to bare Windows drive letter paths
int sanitizedriveletter(std::wstring& localpath)
{
    if (localpath.size() > 1 && localpath.back() == L':')
    {
        localpath.append(L"\\");
        return 1;
    }

    return 0;
}

bool WinFileSystemAccess::istransient(DWORD e)
{
    return e == ERROR_ACCESS_DENIED
        || e == ERROR_TOO_MANY_OPEN_FILES
        || e == ERROR_NOT_ENOUGH_MEMORY
        || e == ERROR_OUTOFMEMORY
        || e == ERROR_WRITE_PROTECT
        || e == ERROR_LOCK_VIOLATION
        || e == ERROR_SHARING_VIOLATION;
}

bool WinFileSystemAccess::istransientorexists(DWORD e)
{
    target_exists = e == ERROR_FILE_EXISTS || e == ERROR_ALREADY_EXISTS;

    return istransient(e);
}

void WinFileSystemAccess::addevents(Waiter* w, int)
{
}

// generate unique local filename in the same fs as relatedpath
void WinFileSystemAccess::tmpnamelocal(LocalPath& localname) const
{
    static unsigned tmpindex;
    char buf[128];

    sprintf(buf, ".getxfer.%lu.%u.mega", GetCurrentProcessId(), tmpindex++);
    localname = LocalPath::fromName(buf, *this, FS_UNKNOWN);
}

// convert UTF-8 to Windows Unicode
void WinFileSystemAccess::path2local(const string* path, string* local) const
{
    // make space for the worst case
    local->resize((path->size() + 1) * sizeof(wchar_t));

    int len = MultiByteToWideChar(CP_UTF8, 0,
                                  path->c_str(),
                                  -1,
                                  (wchar_t*)local->data(),
                                  int(local->size() / sizeof(wchar_t) + 1));
    if (len)
    {
        // resize to actual result
        local->resize(sizeof(wchar_t) * (len - 1));
    }
    else
    {
        local->clear();
    }
}

// convert UTF-8 to Windows Unicode
void WinFileSystemAccess::path2local(const string* path, std::wstring* local) const
{
    // make space for the worst case
    local->resize(path->size() + 2);

    int len = MultiByteToWideChar(CP_UTF8, 0,
        path->c_str(),
        -1,
        const_cast<wchar_t*>(local->data()),
        int(local->size()));
    if (len)
    {
        // resize to actual result
        local->resize(len - 1);
    }
    else
    {
        local->clear();
    }
}

// convert Windows Unicode to UTF-8
void WinFileSystemAccess::local2path(const string* local, string* path) const
{
    path->resize((local->size() + 1) * 4 / sizeof(wchar_t) + 1);

    path->resize(WideCharToMultiByte(CP_UTF8, 0, (wchar_t*)local->data(),
                                     int(local->size() / sizeof(wchar_t)),
                                     (char*)path->data(),
                                     int(path->size()),
                                     NULL, NULL));
    normalize(path);
}

void WinFileSystemAccess::local2path(const std::wstring* local, string* path) const
{
    path->resize((local->size() * sizeof(wchar_t) + 1) * 4 / sizeof(wchar_t) + 1);

    path->resize(WideCharToMultiByte(CP_UTF8, 0, local->data(),
        int(local->size()),
        (char*)path->data(),
        int(path->size()),
        NULL, NULL));

    normalize(path);
}

// write short name of the last path component to sname
bool WinFileSystemAccess::getsname(const LocalPath& namePath, LocalPath& snamePath) const
{
#ifdef WINDOWS_PHONE
    return false;
#else
    const std::wstring& name = namePath.localpath;
    std::wstring& sname = snamePath.localpath;

    DWORD r = DWORD(name.size());
    sname.resize(r);

    DWORD rr = GetShortPathNameW(name.data(), const_cast<wchar_t*>(sname.data()), r);

    sname.resize(rr);

    if (rr >= r)
    {
        rr = GetShortPathNameW(name.data(), const_cast<wchar_t*>(sname.data()), rr);
        sname.resize(rr);
    }

    if (!rr)
    {
        DWORD e = GetLastError();
        LOG_warn << "Unable to get short path name: " << namePath.toPath(gWfsa) << ". Error code: " << e;
        sname.clear();
        return false;
    }

    // we are only interested in the path's last component
    wchar_t* ptr;

    if ((ptr = wcsrchr(const_cast<wchar_t*>(sname.data()), L'\\')) ||
        (ptr = wcsrchr(const_cast<wchar_t*>(sname.data()), L':')))
    {
        sname.erase(0, ptr - sname.data() + 1);
    }
    return sname.size();
#endif
}

// FIXME: if a folder rename fails because the target exists, do a top-down
// recursive copy/delete
bool WinFileSystemAccess::renamelocal(LocalPath& oldnamePath, LocalPath& newnamePath, bool replace)
{
    bool r = !!MoveFileExW(oldnamePath.localpath.c_str(), newnamePath.localpath.c_str(), replace ? MOVEFILE_REPLACE_EXISTING : 0);

    if (!r)
    {
        DWORD e = GetLastError();
        if (SimpleLogger::logCurrentLevel >= logWarning && !skip_errorreport)
        {
            LOG_warn << "Unable to move file: " << oldnamePath.toPath(gWfsa) <<
                        " to " << newnamePath.toPath(gWfsa) << ". Error code: " << e;
        }
        transient_error = istransientorexists(e);
    }

    return r;
}

bool WinFileSystemAccess::copylocal(LocalPath& oldnamePath, LocalPath& newnamePath, m_time_t)
{
#ifdef WINDOWS_PHONE
    bool r = SUCCEEDED(CopyFile2(oldnamePath.localpath.c_str(), newnamePath.localpath.c_str(), NULL));
#else
    bool r = !!CopyFileW(oldnamePath.localpath.c_str(), newnamePath.localpath.c_str(), FALSE);
#endif

    if (!r)
    {
        DWORD e = GetLastError();
        LOG_debug << "Unable to copy file. Error code: " << e;
        transient_error = istransientorexists(e);
    }

    return r;
}

bool WinFileSystemAccess::rmdirlocal(LocalPath& namePath)
{
    bool r = !!RemoveDirectoryW(namePath.localpath.data());
    if (!r)
    {
        DWORD e = GetLastError();
        LOG_debug << "Unable to delete folder. Error code: " << e;
        transient_error = istransient(e);
    }

    return r;
}

bool WinFileSystemAccess::unlinklocal(LocalPath& namePath)
{
    bool r = !!DeleteFileW(namePath.localpath.data());

    if (!r)
    {
        DWORD e = GetLastError();
        LOG_debug << "Unable to delete file. Error code: " << e;
        transient_error = istransient(e);
    }

    return r;
}

// delete all files and folders contained in the specified folder
// (does not recurse into mounted devices)
void WinFileSystemAccess::emptydirlocal(LocalPath& namePath, dev_t basedev)
{
    HANDLE hDirectory, hFind;
    dev_t currentdev;

    ScopedLengthRestore restoreNamePath(namePath);
    sanitizedriveletter(namePath.localpath);

    WIN32_FILE_ATTRIBUTE_DATA fad;
    if (!GetFileAttributesExW(namePath.localpath.c_str(), GetFileExInfoStandard, (LPVOID)&fad)
        || !(fad.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        || fad.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
    {
        return;
    }

#ifdef WINDOWS_PHONE
    CREATEFILE2_EXTENDED_PARAMETERS ex = { 0 };
    ex.dwSize = sizeof(ex);
    ex.dwFileFlags = FILE_FLAG_BACKUP_SEMANTICS;
    hDirectory = CreateFile2(name.data(), GENERIC_READ, FILE_SHARE_WRITE | FILE_SHARE_READ,
                        OPEN_EXISTING, &ex);
#else
    hDirectory = CreateFileW(namePath.localpath.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                             NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
#endif
    if (hDirectory == INVALID_HANDLE_VALUE)
    {
        // discard not accessible folders
        return;
    }

#ifdef WINDOWS_PHONE
    FILE_ID_INFO fi = { 0 };
    if(!GetFileInformationByHandleEx(hDirectory, FileIdInfo, &fi, sizeof(fi)))
#else
    BY_HANDLE_FILE_INFORMATION fi;
    if (!GetFileInformationByHandle(hDirectory, &fi))
#endif
    {
        currentdev = 0;
    }
    else
    {
    #ifdef WINDOWS_PHONE
        currentdev = fi.VolumeSerialNumber + 1;
    #else
        currentdev = fi.dwVolumeSerialNumber + 1;
    #endif
    }
    CloseHandle(hDirectory);
    if (basedev && currentdev != basedev)
    {
        // discard folders on different devices
        return;
    }

    bool removed;
    for (;;)
    {
        // iterate over children and delete
        removed = false;

        WIN32_FIND_DATAW ffd;
        {
            ScopedLengthRestore restoreNamePath2(namePath);
            namePath.appendWithSeparator(LocalPath::fromPlatformEncoded(L"*"), true);

            #ifdef WINDOWS_PHONE
                hFind = FindFirstFileExW(namePath.localpath.c_str(), FindExInfoBasic, &ffd, FindExSearchNameMatch, NULL, 0);
            #else
                hFind = FindFirstFileW(namePath.localpath.c_str(), &ffd);
            #endif
        }

        if (hFind == INVALID_HANDLE_VALUE)
        {
            break;
        }

        bool morefiles = true;
        while (morefiles)
        {
            if (!(ffd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
                && (!(ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                    || *ffd.cFileName != '.'
                    || (ffd.cFileName[1] && ((ffd.cFileName[1] != '.')
                    || ffd.cFileName[2]))))
            {
                ScopedLengthRestore restoreNamePath3(namePath);
                namePath.appendWithSeparator(LocalPath::fromPlatformEncoded(ffd.cFileName), true);
                if (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                {
                    emptydirlocal(namePath, currentdev);
                    removed |= !!RemoveDirectoryW(namePath.localpath.c_str());
                }
                else
                {
                    removed |= !!DeleteFileW(namePath.localpath.c_str());
                }
            }
            morefiles = FindNextFileW(hFind, &ffd);
        }

        FindClose(hFind);
        if (!removed)
        {
            break;
        }
    }
}

bool WinFileSystemAccess::mkdirlocal(LocalPath& namePath, bool hidden)
{
    const std::wstring& name = namePath.localpath;

    bool r = !!CreateDirectoryW(name.data(), NULL);

    if (!r)
    {
        DWORD e = GetLastError();
        LOG_debug << "Unable to create folder. Error code: " << e;
        transient_error = istransientorexists(e);
    }
    else if (hidden)
    {
#ifdef WINDOWS_PHONE
        WIN32_FILE_ATTRIBUTE_DATA a = { 0 };
        BOOL res = GetFileAttributesExW(name.data(), GetFileExInfoStandard, &a);

        if (res)
        {
            SetFileAttributesW(name.data(), a.dwFileAttributes | FILE_ATTRIBUTE_HIDDEN);
        }
#else
        DWORD a = GetFileAttributesW(name.data());

        if (a != INVALID_FILE_ATTRIBUTES)
        {
            SetFileAttributesW(name.data(), a | FILE_ATTRIBUTE_HIDDEN);
        }
#endif
    }

    return r;
}

bool WinFileSystemAccess::setmtimelocal(LocalPath& namePath, m_time_t mtime)
{
#ifdef WINDOWS_PHONE
    return false;
#else
    FILETIME lwt;
    LONGLONG ll;
    HANDLE hFile;

    hFile = CreateFileW(namePath.localpath.data(), FILE_WRITE_ATTRIBUTES, FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);

    if (hFile == INVALID_HANDLE_VALUE)
    {
        DWORD e = GetLastError();
        transient_error = istransient(e);
        LOG_warn << "Error opening file to change mtime: " << e;
        return false;
    }

    ll = (mtime + 11644473600) * 10000000;

    lwt.dwLowDateTime = (DWORD)ll;
    lwt.dwHighDateTime = ll >> 32;

    int r = !!SetFileTime(hFile, NULL, NULL, &lwt);
    if (!r)
    {
        DWORD e = GetLastError();
        transient_error = istransient(e);
        LOG_warn << "Error changing mtime: " << e;
    }

    CloseHandle(hFile);

    return r;
#endif
}

bool WinFileSystemAccess::chdirlocal(LocalPath& namePath) const
{
#ifdef WINDOWS_PHONE
    return false;
#else
    int r = SetCurrentDirectoryW(namePath.localpath.c_str());
    return r;
#endif
}

// return lowercased ASCII file extension, including the . separator
bool WinFileSystemAccess::getextension(const LocalPath& filenamePath, std::string &extension) const
{
    const wchar_t* ptr = filenamePath.localpath.data() + filenamePath.localpath.size();

    char c;
    size_t i, j;
    size_t size = filenamePath.localpath.size();

    for (i = 0; i < size; i++)
    {
        if (*--ptr == '.')
        {
            extension.reserve(i+1);

            for (j = 0; j <= i; j++)
            {
                if (*ptr < '.' || *ptr > 'z') return false;

                c = (char)*(ptr++);

                // tolower()
                if (c >= 'A' && c <= 'Z') c |= ' ';

                extension.push_back(c);
            }
			return true;
		}
	}

    return false;
}

bool WinFileSystemAccess::expanselocalpath(LocalPath& pathArg, LocalPath& absolutepathArg)
{
#ifdef WINDOWS_PHONE
    wchar_t full[_MAX_PATH];
    if (_wfullpath(full, pathArg.localpath.data(), _MAX_PATH))
    {
        absolutepathArg.localpath = full;
        return true;
    }
    absolutepathArg.localpath = pathArg.localpath;
    return false;
#else

    int len = GetFullPathNameW(pathArg.localpath.data(), 0, NULL, NULL);
    if (len <= 0)
    {
        absolutepathArg.localpath = pathArg.localpath;
        return false;
    }

    absolutepathArg.localpath.resize(len);
    int newlen = GetFullPathNameW(pathArg.localpath.data(), len, const_cast<wchar_t*>(absolutepathArg.localpath.data()), NULL);
    if (newlen <= 0 || newlen >= len)
    {
        absolutepathArg.localpath = pathArg.localpath;
        return false;
    }

    if (memcmp(absolutepathArg.localpath.data(), L"\\\\?\\", 8))
    {
        if (!memcmp(absolutepathArg.localpath.data(), L"\\\\", 4)) //network location
        {
            absolutepathArg.localpath.insert(0, L"\\\\?\\UNC\\");
        }
        else
        {
            absolutepathArg.localpath.insert(0, L"\\\\?\\");
        }
    }

    return true;
#endif
}

void WinFileSystemAccess::osversion(string* u, bool includeArchExtraInfo) const
{
    char buf[128];

#ifdef WINDOWS_PHONE
    sprintf(buf, "Windows Phone");
#else
    typedef LONG MEGANTSTATUS;
    typedef struct _MEGAOSVERSIONINFOW {
        DWORD dwOSVersionInfoSize;
        DWORD dwMajorVersion;
        DWORD dwMinorVersion;
        DWORD dwBuildNumber;
        DWORD dwPlatformId;
        WCHAR  szCSDVersion[ 128 ];     // Maintenance string for PSS usage
    } MEGARTL_OSVERSIONINFOW, *PMEGARTL_OSVERSIONINFOW;

    typedef MEGANTSTATUS (WINAPI* RtlGetVersionPtr)(PMEGARTL_OSVERSIONINFOW);
    MEGARTL_OSVERSIONINFOW version = { 0 };
    HMODULE hMod = GetModuleHandleW(L"ntdll.dll");
    if (hMod)
    {
        RtlGetVersionPtr RtlGetVersion = (RtlGetVersionPtr)(void*)GetProcAddress(hMod, "RtlGetVersion");
        if (RtlGetVersion)
        {
            RtlGetVersion(&version);
        }
    }
    snprintf(buf, sizeof(buf), "Windows %d.%d.%d", version.dwMajorVersion, version.dwMinorVersion, version.dwBuildNumber);

    if (includeArchExtraInfo)
    {
        BOOL isWOW = FALSE;
        BOOL callSucceeded = IsWow64Process(GetCurrentProcess(), &isWOW);
        if (callSucceeded && isWOW)
        {
            strcat(buf, "/64");  // if the app 32/64 bit matches the OS, then no need to specify the OS separately, so we only need to cover the WOW 32 bit on 64 bit case.
        }
    }
#endif

    u->append(buf);
}

void WinFileSystemAccess::statsid(string *id) const
{
#ifndef WINDOWS_PHONE
    LONG hr;
    HKEY hKey = NULL;
    hr = RegOpenKeyEx(HKEY_LOCAL_MACHINE, L"Software\\Microsoft\\Cryptography", 0,
                      KEY_QUERY_VALUE
#ifdef KEY_WOW64_64KEY
		      | KEY_WOW64_64KEY
#else
		      | 0x0100
#endif
		      , &hKey);
    if (hr == ERROR_SUCCESS)
    {
        WCHAR pszData[256];
        DWORD cbData = sizeof(pszData);
        hr = RegQueryValueExW(hKey, L"MachineGuid", NULL, NULL, (LPBYTE)pszData, &cbData);
        if (hr == ERROR_SUCCESS)
        {
            std::wstring localdata(pszData);
            string utf8data;
            local2path(&localdata, &utf8data);
            id->append(utf8data);
        }
        RegCloseKey(hKey);
    }
#endif
}

// set DirNotify's root LocalNode
void WinDirNotify::addnotify(LocalNode* l, const LocalPath&)
{
#ifdef ENABLE_SYNC
    if (!l->parent)
    {
        localrootnode = l;
    }
#endif
}

fsfp_t WinDirNotify::fsfingerprint() const
{
#ifdef WINDOWS_PHONE
	FILE_ID_INFO fi = { 0 };
	if(!GetFileInformationByHandleEx(hDirectory, FileIdInfo, &fi, sizeof(fi)))
#else
	BY_HANDLE_FILE_INFORMATION fi;
	if (!GetFileInformationByHandle(hDirectory, &fi))
#endif
    {
        LOG_err << "Unable to get fsfingerprint. Error code: " << GetLastError();
        return 0;
    }

#ifdef WINDOWS_PHONE
	return fi.VolumeSerialNumber + 1;
#else
    return fi.dwVolumeSerialNumber + 1;
#endif
}

bool WinDirNotify::fsstableids() const
{
#ifdef WINDOWS_PHONE
#error "Not implemented"
#endif
    TCHAR volume[MAX_PATH + 1];
    if (GetVolumePathNameW(localbasepath.localpath.data(), volume, MAX_PATH + 1))
    {
        TCHAR fs[MAX_PATH + 1];
        if (GetVolumeInformation(volume, NULL, 0, NULL, NULL, NULL, fs, MAX_PATH + 1))
        {
            LOG_info << "Filesystem type: " << fs;
            return _wcsicmp(fs, L"FAT")
                && _wcsicmp(fs, L"FAT32")
                && _wcsicmp(fs, L"exFAT");
        }
    }
    LOG_err << "Failed to get filesystem type. Error code: " << GetLastError();
    return true;
}

VOID CALLBACK WinDirNotify::completion(DWORD dwErrorCode, DWORD dwBytes, LPOVERLAPPED lpOverlapped)
{
    assert( std::this_thread::get_id() == smNotifierThread->get_id());
#ifndef WINDOWS_PHONE
    WinDirNotify *dirnotify = (WinDirNotify*)lpOverlapped->hEvent;
    if (!dirnotify->mOverlappedExit && dwErrorCode != ERROR_OPERATION_ABORTED)
    {
        dirnotify->process(dwBytes);
    }
    else
    {
        dirnotify->mOverlappedEnabled = false;
    }
#endif
}

void WinDirNotify::process(DWORD dwBytes)
{
    assert( std::this_thread::get_id() == smNotifierThread->get_id());

#ifndef WINDOWS_PHONE
    if (!dwBytes)
    {
#ifdef ENABLE_SYNC
        int errCount = ++mErrorCount;
        LOG_err << "Empty filesystem notification: " << (localrootnode ? localrootnode->name.c_str() : "NULL")
                << " errors: " << errCount;
        readchanges();
        notify(DIREVENTS, localrootnode, LocalPath());
#endif
    }
    else
    {
        assert(dwBytes >= offsetof(FILE_NOTIFY_INFORMATION, FileName) + sizeof(wchar_t));

        string processbuf;
        if (dwBytes <= 4096)
        {
            processbuf = notifybuf;  // even under high load, usually the buffer is under 4k.
        }
        else
        {
            processbuf.swap(notifybuf);  // use existing buffer, a new one will be allocated for receiving
        }
        char* ptr = (char*)processbuf.data();

        readchanges();

        // ensure accuracy of the notification timestamps
		WAIT_CLASS::bumpds();

        // we trust the OS to always return conformant data
        for (;;)
        {
            FILE_NOTIFY_INFORMATION* fni = (FILE_NOTIFY_INFORMATION*)ptr;

            // skip the local debris folder
            // also, we skip the old name in case of renames
            if (fni->Action != FILE_ACTION_RENAMED_OLD_NAME
                && (fni->FileNameLength < ignore.localpath.size()
                    || memcmp(fni->FileName, ignore.localpath.data(), ignore.localpath.size() * sizeof(wchar_t))
                    || (fni->FileNameLength > ignore.localpath.size()
                        && fni->FileName[ignore.localpath.size() - 1] == L'\\')))
            {
                if (SimpleLogger::logCurrentLevel >= logDebug)
                {
#ifdef ENABLE_SYNC
                    // Outputting this logging on the notification thread slows it down considerably, risking missing notifications.
                    // Let's skip it and log the ones received on the notify queue

                    //string local, path;
                    //local.assign((char*)fni->FileName, fni->FileNameLength);
                    //path.resize((local.size() + 1) * 4 / sizeof(wchar_t));
                    //path.resize(WideCharToMultiByte(CP_UTF8, 0, (wchar_t*)local.data(),
                    //                                 int(local.size() / sizeof(wchar_t)),
                    //                                 (char*)path.data(),
                    //                                 int(path.size() + 1),
                    //                                 NULL, NULL));

                    //LOG_debug << "Filesystem notification. Root: " << (localrootnode ? localrootnode->name.c_str() : "NULL") << "   Path: " << path;
#endif
                }
#ifdef ENABLE_SYNC
                notify(DIREVENTS, localrootnode, LocalPath::fromPlatformEncoded(std::wstring(fni->FileName, fni->FileNameLength / sizeof(fni->FileName[0]))));
#endif
            }
            else if (SimpleLogger::logCurrentLevel >= logDebug)
            {
#ifdef ENABLE_SYNC
                // Outputting this logging on the notification thread slows it down considerably, risking missing notifications.
                // Let's skip it and log the ones received on the notify queue

                //string local, path;
                //local.assign((char*)fni->FileName, fni->FileNameLength);
                //path.resize((local.size() + 1) * 4 / sizeof(wchar_t));
                //path.resize(WideCharToMultiByte(CP_UTF8, 0, (wchar_t*)local.data(),
                //                                 int(local.size() / sizeof(wchar_t)),
                //                                 (char*)path.data(),
                //                                 int(path.size() + 1),
                //                                 NULL, NULL));
                //LOG_debug << "Skipped filesystem notification. Root: " << (localrootnode ? localrootnode->name.c_str() : "NULL") << "   Path: " << path;
#endif
            }


            if (!fni->NextEntryOffset)
            {
                break;
            }

            ptr += fni->NextEntryOffset;
        }
    }
#endif
    clientWaiter->notify();
}

// request change notifications on the subtree under hDirectory
void WinDirNotify::readchanges()
{
    assert( std::this_thread::get_id() == smNotifierThread->get_id());

#ifndef WINDOWS_PHONE
    if (notifybuf.size() != 65534)
    {
        // Use 65534 for the buffer size becaues (from doco):
        // ReadDirectoryChangesW fails with ERROR_INVALID_PARAMETER when the buffer length is greater than 64 KB and the application is
        // monitoring a directory over the network. This is due to a packet size limitation with the underlying file sharing protocols.
        notifybuf.resize(65534);
    }
    auto readRet = ReadDirectoryChangesW(hDirectory, (LPVOID)notifybuf.data(),
                              (DWORD)notifybuf.size(), TRUE,
                              FILE_NOTIFY_CHANGE_FILE_NAME
                            | FILE_NOTIFY_CHANGE_DIR_NAME
                            | FILE_NOTIFY_CHANGE_LAST_WRITE
                            | FILE_NOTIFY_CHANGE_SIZE
                            | FILE_NOTIFY_CHANGE_CREATION,
                              &dwBytes, &overlapped, completion);

    if (readRet)
    {
        setFailed(0, "");
        mOverlappedEnabled = true;
    }
    else
    {
        mOverlappedEnabled = false;
        DWORD e = GetLastError();
        LOG_warn << "ReadDirectoryChanges not available. Error code: " << e << " errors: " << mErrorCount.load();
        if (e == ERROR_NOTIFY_ENUM_DIR && mErrorCount < 10)
        {
            // notification buffer overflow
            mErrorCount++;
            readchanges();
        }
        else
        {
            // permanent failure - switch to scanning mode
            setFailed(e, "Fatal error returned by ReadDirectoryChangesW");
        }
    }
#endif
}

std::mutex WinDirNotify::smNotifyMutex;
std::atomic<unsigned> WinDirNotify::smNotifierCount{0};
HANDLE WinDirNotify::smEventHandle = NULL;
std::deque<std::function<void()>> WinDirNotify::smQueue;
std::unique_ptr<std::thread> WinDirNotify::smNotifierThread;

void WinDirNotify::notifierThreadFunction()
{
    LOG_debug << "Filesystem notify thread started";
    bool recheck = false;
    for (;;)
    {
        if (!recheck)
        {
            WaitForSingleObjectEx(smEventHandle, INFINITE, TRUE);  // alertable, so filesystem notify callbacks can occur on this thread during this time.
            ResetEvent(smEventHandle);
        }
        recheck = false;

        std::function<void()> f;
        {
            std::unique_lock<std::mutex> g(smNotifyMutex);
            if (!smQueue.empty())
            {
                f = std::move(smQueue.front());
                if (!f) break;   // nullptr to cause the thread to exit
                smQueue.pop_front();
            }
        }
        if (f)
        {
            f();
            recheck = true;
        }
    }
    LOG_debug << "Filesystem notify thread stopped";
}

WinDirNotify::WinDirNotify(LocalPath& localbasepath, const LocalPath& ignore, WinFileSystemAccess* owner, Waiter* waiter) : DirNotify(localbasepath, ignore)
{
    fsaccess = owner;
    fsaccess->dirnotifys.insert(this);
    clientWaiter = waiter;

    {
        // If this is the first Notifier created, start the thread that queries the OS for notifications.
        std::lock_guard<std::mutex> g(smNotifyMutex);
        if (++smNotifierCount == 1)
        {
            smQueue.clear();
            smEventHandle = CreateEvent(NULL, FALSE, FALSE, NULL);

            // One thread to notify them all
            smNotifierThread.reset(new std::thread([](){ notifierThreadFunction(); }));
        }
    }

#ifndef WINDOWS_PHONE
    ZeroMemory(&overlapped, sizeof(overlapped));
    overlapped.hEvent = this;
    mOverlappedEnabled = false;
    mOverlappedExit = false;

    ScopedLengthRestore restoreLocalbasePath(localbasepath);
    sanitizedriveletter(localbasepath.localpath);

    // ReadDirectoryChangesW: If you opened the file using the short name, you can receive change notifications for the short name.  (so make sure it's a long name)
    std::wstring longname;
    auto r = localbasepath.localpath.size() + 20;
    longname.resize(r);
    auto rr = GetLongPathNameW(localbasepath.localpath.data(), const_cast<wchar_t*>(longname.data()), DWORD(r));

    longname.resize(rr);
    if (rr >= r)
    {
        rr = GetLongPathNameW(localbasepath.localpath.data(), const_cast<wchar_t*>(longname.data()), rr);
        longname.resize(rr);
    }

    if ((hDirectory = CreateFileW(longname.data(),
                                  FILE_LIST_DIRECTORY,
                                  FILE_SHARE_READ | FILE_SHARE_WRITE,
                                  NULL,
                                  OPEN_EXISTING,
                                  FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
                                  NULL)) != INVALID_HANDLE_VALUE)
    {
        setFailed(0, "");

        {
            std::lock_guard<std::mutex> g(smNotifyMutex);
            smQueue.push_back([this](){ readchanges(); });
        }
        SetEvent(smEventHandle);
    }
    else
    {
        int err = GetLastError();
        setFailed(err, "CreateFileW was unable to open the folder");
        LOG_err << "Unable to initialize filesystem notifications. Error: " << err;
    }
#endif
}

WinDirNotify::~WinDirNotify()
{
    mOverlappedExit = true;

#ifndef WINDOWS_PHONE
    if (hDirectory != INVALID_HANDLE_VALUE)
    {
        if (mOverlappedEnabled)
        {
            {
                std::lock_guard<std::mutex> g(smNotifyMutex);
                smQueue.push_back([this](){ CancelIo(hDirectory); });
            }
            SetEvent(smEventHandle);
            while (mOverlappedEnabled)
            {
                SleepEx(10, true);
            }
        }

        CloseHandle(hDirectory);
    }
    fsaccess->dirnotifys.erase(this);
#endif

    {
        if (--smNotifierCount == 0)
        {
            {
                std::lock_guard<std::mutex> g(smNotifyMutex);
                smQueue.push_back(nullptr);
            }
            SetEvent(smEventHandle);
            smNotifierThread->join();
            smNotifierThread.reset();
            CloseHandle(smEventHandle);
            smQueue.clear();
        }
    }

}

std::unique_ptr<FileAccess> WinFileSystemAccess::newfileaccess(bool followSymLinks)
{
    return std::unique_ptr<FileAccess>(new WinFileAccess(waiter));
}

bool WinFileSystemAccess::getlocalfstype(const LocalPath& path, FileSystemType& type) const
{
    using std::wstring;

    // Where is the volume containing our file mounted?
    wstring mountPoint(MAX_PATH + 1, L'\0');

    if (!GetVolumePathNameW(path.localpath.c_str(),
                            const_cast<wchar_t*>(mountPoint.data()),
                            MAX_PATH + 1))
    {
        return type = FS_UNKNOWN, false;
    }

    // Get the name of the volume's filesystem.
    wstring filesystemName(MAX_PATH + 1, L'\0');
    DWORD volumeFlags = 0;

    // What kind of filesystem is the volume using?
    if (GetVolumeInformationW(mountPoint.c_str(),
                              nullptr,
                              0,
                              nullptr,
                              nullptr,
                              &volumeFlags,
                              &filesystemName[0],
                              MAX_PATH + 1))
    {
        // Assume we can't find a matching filesystem.
        type = FS_UNKNOWN;

        if (!wcscmp(filesystemName.c_str(), L"NTFS"))
        {
            type = FS_NTFS;
        }
        else if (!wcscmp(filesystemName.c_str(), L"FAT32"))
        {
            type = FS_FAT32;
        }
        else if (!wcscmp(filesystemName.c_str(), L"exFAT"))
        {
            type = FS_EXFAT;
        }

        return true;
    }

    // We couldn't get any information on the volume.
    return type = FS_UNKNOWN, false;
}

DirAccess* WinFileSystemAccess::newdiraccess()
{
    return new WinDirAccess();
}

DirNotify* WinFileSystemAccess::newdirnotify(LocalPath& localpath, LocalPath& ignore, Waiter* waiter)
{
    return new WinDirNotify(localpath, ignore, this, waiter);
}

bool WinFileSystemAccess::issyncsupported(const LocalPath& localpathArg, bool& isnetwork, SyncError& syncError, SyncWarning& syncWarning)
{
    WCHAR VBoxSharedFolderFS[] = L"VBoxSharedFolderFS";
    std::wstring path, fsname;
    bool result = true;
    isnetwork = false;
    syncError = NO_SYNC_ERROR;
    syncWarning = NO_SYNC_WARNING;

#ifndef WINDOWS_PHONE
    path.resize(MAX_PATH * sizeof(WCHAR));
    fsname.resize(MAX_PATH * sizeof(WCHAR));

    if (GetVolumePathNameW(localpathArg.localpath.data(), const_cast<wchar_t*>(path.data()), MAX_PATH)
        && GetVolumeInformationW((LPCWSTR)path.data(), NULL, 0, NULL, NULL, NULL, (LPWSTR)fsname.data(), MAX_PATH))
    {
        if (!memcmp(fsname.data(), VBoxSharedFolderFS, sizeof(VBoxSharedFolderFS)))
        {
            LOG_warn << "VBoxSharedFolderFS is not supported because it doesn't provide ReadDirectoryChanges() nor unique file identifiers";
            syncError = VBOXSHAREDFOLDER_UNSUPPORTED;
            result = false;
        }
        else if ((!memcmp(fsname.data(), L"FAT", 6) || !memcmp(fsname.data(), L"exFAT", 10))) // TODO: have these checks for !windows too
        {
            LOG_warn << "You are syncing a local folder formatted with a FAT filesystem. "
                        "That filesystem has deficiencies managing big files and modification times "
                        "that can cause synchronization problems (e.g. when daylight saving changes), "
                        "so it's strongly recommended that you only sync folders formatted with more "
                        "reliable filesystems like NTFS (more information at https://help.mega.nz/megasync/syncing.html#can-i-sync-fat-fat32-partitions-under-windows.";
            syncWarning = LOCAL_IS_FAT;
        }
        else if (!memcmp(fsname.data(), L"HGFS", 8))
        {
            LOG_warn << "You are syncing a local folder shared with VMWare. Those folders do not support filesystem notifications "
            "so MEGAsync will have to be continuously scanning to detect changes in your files and folders. "
            "Please use a different folder if possible to reduce the CPU usage.";
            syncWarning = LOCAL_IS_HGFS;
        }
    }

    if (GetDriveTypeW(path.data()) == DRIVE_REMOTE)
    {
        LOG_debug << "Network folder detected";
        isnetwork = true;
    }

    string utf8fsname;
    local2path(&fsname, &utf8fsname);
    LOG_debug << "Filesystem type: " << utf8fsname;

#endif

    return result;
}

bool WinDirAccess::dopen(LocalPath* nameArg, FileAccess* f, bool glob)
{
    assert(nameArg || f);
    assert(!(glob && f));

    if (f)
    {
        if ((hFind = ((WinFileAccess*)f)->hFind) != INVALID_HANDLE_VALUE)
        {
            ffd = ((WinFileAccess*)f)->ffd;
            ((WinFileAccess*)f)->hFind = INVALID_HANDLE_VALUE;
        }
    }
    else
    {
        std::wstring name = nameArg->localpath;
        if (!glob)
        {
            name.append(L"\\*");
        }

#ifdef WINDOWS_PHONE
        hFind = FindFirstFileExW(name.c_str(), FindExInfoBasic, &ffd, FindExSearchNameMatch, NULL, 0);
#else
        hFind = FindFirstFileW(name.c_str(), &ffd);
#endif

        if (glob)
        {
            if (size_t index = nameArg->getLeafnameByteIndex(gWfsa))
            {
                globbase = *nameArg;
                globbase.truncate(index);
            }
            else
            {
                globbase.clear();
            }
        }
    }

    if (!(ffdvalid = hFind != INVALID_HANDLE_VALUE))
    {
        return false;
    }

    return true;
}

// FIXME: implement followsymlinks
bool WinDirAccess::dnext(LocalPath& /*path*/, LocalPath& nameArg, bool /*followsymlinks*/, nodetype_t* type)
{
    for (;;)
    {
        if (ffdvalid
         && !WinFileAccess::skipattributes(ffd.dwFileAttributes)
         && (!(ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
          || *ffd.cFileName != '.'
          || (ffd.cFileName[1] && ((ffd.cFileName[1] != '.') || ffd.cFileName[2]))))
        {
            nameArg.localpath.assign(ffd.cFileName, wcslen(ffd.cFileName));
            if (!globbase.empty())
            {
                nameArg.prependWithSeparator(globbase);
            }

            if (type)
            {
                *type = (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? FOLDERNODE : FILENODE;
            }

            ffdvalid = false;
            currentItemAttributes = ffd;
            return true;
        }
        else
        {
            if (ffdvalid && SimpleLogger::logCurrentLevel >= logDebug)
            {
                if (*ffd.cFileName != '.' && (ffd.cFileName[1] && ((ffd.cFileName[1] != '.') || ffd.cFileName[2])))
                    LOG_debug << "Excluded: " << ffd.cFileName << "   Attributes: " << ffd.dwFileAttributes;
            }
        }

        if (!(ffdvalid = FindNextFileW(hFind, &ffd) != 0))
        {
            return false;
        }
    }
}

WinDirAccess::WinDirAccess()
{
    ffdvalid = false;
    hFind = INVALID_HANDLE_VALUE;
}

WinDirAccess::~WinDirAccess()
{
    if (hFind != INVALID_HANDLE_VALUE)
    {
        FindClose(hFind);
    }
}
} // namespace
