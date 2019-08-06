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

#include "mega.h"

namespace mega {
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
    WIN32_FILE_ATTRIBUTE_DATA fad;

    type = TYPE_UNKNOWN;
    if (!GetFileAttributesExW((LPCWSTR)localname.data(), GetFileExInfoStandard, (LPVOID)&fad))
    {
        DWORD e = GetLastError();
        errorcode = e;
        retry = WinFileSystemAccess::istransient(e);
        return false;
    }

    errorcode = 0;
    if (SimpleLogger::logCurrentLevel >= logDebug && skipattributes(fad.dwFileAttributes))
    {
        string utf8path;
        utf8path.resize((localname.size() + 1) * 4 / sizeof(wchar_t));
        utf8path.resize(WideCharToMultiByte(CP_UTF8, 0, (wchar_t*)localname.data(),
                                         int(localname.size() / sizeof(wchar_t)),
                                         (char*)utf8path.data(),
                                         int(utf8path.size() + 1),
                                         NULL, NULL));

        LOG_debug << "Incompatible attributes (" << fad.dwFileAttributes << ") for file " << utf8path;
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
#ifdef WINDOWS_PHONE
    hFile = CreateFile2((LPCWSTR)localname.data(), GENERIC_READ,
                        FILE_SHARE_WRITE | FILE_SHARE_READ,
                        OPEN_EXISTING, NULL);
#else
    hFile = CreateFileW((LPCWSTR)localname.data(), GENERIC_READ,
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
    if (localname.size())
    {
        assert (hFile != INVALID_HANDLE_VALUE);
        if (hFile != INVALID_HANDLE_VALUE)
        {
            CloseHandle(hFile);
            hFile = INVALID_HANDLE_VALUE;
        }
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
    context->failed = dwErrorCode || dwNumberOfBytesTransfered != context->len;
    if (!context->failed)
    {
        if (context->op == AsyncIOContext::READ)
        {
            memset((void *)(((char *)(context->buffer)) + context->len), 0, context->pad);
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
    string path;
    path.assign((char *)context->buffer, context->len);
    bool read = context->access & AsyncIOContext::ACCESS_READ;
    bool write = context->access & AsyncIOContext::ACCESS_WRITE;

    context->failed = !fopen(&path, read, write, true);
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

    overlapped->Offset = winContext->pos & 0xFFFFFFFF;
    overlapped->OffsetHigh = (winContext->pos >> 32) & 0xFFFFFFFF;
    overlapped->hEvent = winContext;
    winContext->overlapped = overlapped;

    if (!ReadFileEx(hFile, (LPVOID)winContext->buffer, (DWORD)winContext->len,
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
    overlapped->Offset = winContext->pos & 0xFFFFFFFF;
    overlapped->OffsetHigh = (winContext->pos >> 32) & 0xFFFFFFFF;
    overlapped->hEvent = winContext;
    winContext->overlapped = overlapped;

    if (!WriteFileEx(hFile, (LPVOID)winContext->buffer, (DWORD)winContext->len,
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
void WinFileAccess::updatelocalname(string* name)
{
    if (localname.size())
    {
        localname = *name;
        WinFileSystemAccess::sanitizedriveletter(&localname);
        localname.append("", 1);
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
bool WinFileAccess::fopen(string *name, bool read, bool write)
{
    return fopen(name, read, write, false);
}

bool WinFileAccess::fopen(string* name, bool read, bool write, bool async)
{
    WIN32_FIND_DATA fad = { 0 };

#ifdef WINDOWS_PHONE
    FILE_ID_INFO bhfi = { 0 };
#else
    BY_HANDLE_FILE_INFORMATION bhfi = { 0 };
#endif

    bool skipcasecheck = false;
    int added = WinFileSystemAccess::sanitizedriveletter(name);
    
    name->append("", 1);

    if (write)
    {
        type = FILENODE;
    }
    else
    {
        HANDLE  h = name->size() > sizeof(wchar_t)
                ? FindFirstFileExW((LPCWSTR)name->data(), FindExInfoStandard, &fad,
                             FindExSearchNameMatch, NULL, 0)
                : INVALID_HANDLE_VALUE;

        if (h != INVALID_HANDLE_VALUE)
        {
            FindClose(h);
        }
        else
        {
            WIN32_FILE_ATTRIBUTE_DATA fatd;
            if (!GetFileAttributesExW((LPCWSTR)name->data(), GetFileExInfoStandard, (LPVOID)&fatd))
            {
                DWORD e = GetLastError();
                // this is an expected case so no need to log.  the FindFirstFileEx did not find the file, 
                // GetFileAttributesEx is only expected to find it if it's a network share point
                // LOG_debug << "Unable to get the attributes of the file. Error code: " << e;
                retry = WinFileSystemAccess::istransient(e);
                name->resize(name->size() - added - 1);
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

        if (!skipcasecheck)
        {
            const char *filename = name->data() + name->size() - 1;
            int filenamesize = 0;
            bool separatorfound = false;
            do {
                filename -= sizeof(wchar_t);
                filenamesize += sizeof(wchar_t);
                separatorfound = !memcmp(L"\\", filename, sizeof(wchar_t)) || !memcmp(L"/", filename, sizeof(wchar_t)) || !memcmp(L":", filename, sizeof(wchar_t));
            } while (filename > name->data() && !separatorfound);

            if (filenamesize > sizeof(wchar_t) || !separatorfound)
            {
                if (separatorfound)
                {
                    filename += sizeof(wchar_t);
                }
                else
                {
                    filenamesize += sizeof(wchar_t);
                }

                if (memcmp(filename, fad.cFileName, filenamesize < sizeof(fad.cFileName) ? filenamesize : sizeof(fad.cFileName))
                        && (filenamesize > sizeof(fad.cAlternateFileName) || memcmp(filename, fad.cAlternateFileName, filenamesize))
                        && !((filenamesize == 4 && !memcmp(filename, L".", 4))
                             || (filenamesize == 6 && !memcmp(filename, L"..", 6))))
                {
                    LOG_warn << "fopen failed due to invalid case";
                    retry = false;
                    name->resize(name->size() - added - 1);
                    return false;
                }
            }
        }

        // ignore symlinks - they would otherwise be treated as moves
        // also, ignore some other obscure filesystem object categories
        if (!added && skipattributes(fad.dwFileAttributes))
        {            
            name->resize(name->size() - 1);
            if (SimpleLogger::logCurrentLevel >= logDebug)
            {
                string excluded;
                excluded.resize((name->size() + 1) * 4 / sizeof(wchar_t));
                excluded.resize(WideCharToMultiByte(CP_UTF8, 0, (wchar_t*)name->data(),
                                                 int(name->size() / sizeof(wchar_t)),
                                                 (char*)excluded.data(),
                                                 int(excluded.size() + 1),
                                                 NULL, NULL));
                LOG_debug << "Excluded: " << excluded << "   Attributes: " << fad.dwFileAttributes;
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

    hFile = CreateFile2((LPCWSTR)name->data(),
                        read ? GENERIC_READ : (write ? GENERIC_WRITE : 0),
                        FILE_SHARE_WRITE | FILE_SHARE_READ,
                        !write ? OPEN_EXISTING : OPEN_ALWAYS,
                        &ex);
#else
    hFile = CreateFileW((LPCWSTR)name->data(),
                        read ? GENERIC_READ : (write ? GENERIC_WRITE : 0),
                        FILE_SHARE_WRITE | FILE_SHARE_READ,
                        NULL,
                        !write ? OPEN_EXISTING : OPEN_ALWAYS,
                        (type == FOLDERNODE) ? FILE_FLAG_BACKUP_SEMANTICS
                                             : (async ? FILE_FLAG_OVERLAPPED : 0),
                        NULL);
#endif

    name->resize(name->size() - added - 1);

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
        name->append((const char*)(const wchar_t*)L"\\*", 5);

#ifdef WINDOWS_PHONE
        hFind = FindFirstFileExW((LPCWSTR)name->data(), FindExInfoBasic, &ffd, FindExSearchNameMatch, NULL, 0);
#else
        hFind = FindFirstFileW((LPCWSTR)name->data(), &ffd);
#endif

        name->resize(name->size() - 5);

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

    localseparator.assign((const char*)(const wchar_t*)L"\\", sizeof(wchar_t));
}

WinFileSystemAccess::~WinFileSystemAccess()
{
    assert(!dirnotifys.size());
}

// append \ to bare Windows drive letter paths
int WinFileSystemAccess::sanitizedriveletter(string* localpath)
{
    if (localpath->size() > sizeof(wchar_t) && !memcmp(localpath->data() + localpath->size() - sizeof(wchar_t), (const char*)(const wchar_t*)L":", sizeof(wchar_t)))
    {
        localpath->append((const char*)(const wchar_t*)L"\\", sizeof(wchar_t));
        return sizeof(wchar_t);
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
void WinFileSystemAccess::tmpnamelocal(string* localname) const
{
    static unsigned tmpindex;
    char buf[128];

    sprintf(buf, ".getxfer.%lu.%u.mega", GetCurrentProcessId(), tmpindex++);
    *localname = buf;
    name2local(localname);
}

// convert UTF-8 to Windows Unicode
void WinFileSystemAccess::path2local(string* path, string* local) const
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

// convert Windows Unicode to UTF-8
void WinFileSystemAccess::local2path(string* local, string* path) const
{
    path->resize((local->size() + 1) * 4 / sizeof(wchar_t));

    path->resize(WideCharToMultiByte(CP_UTF8, 0, (wchar_t*)local->data(),
                                     int(local->size() / sizeof(wchar_t)),
                                     (char*)path->data(),
                                     int(path->size() + 1),
                                     NULL, NULL));
    normalize(path);
}

// write short name of the last path component to sname
bool WinFileSystemAccess::getsname(string* name, string* sname) const
{
#ifdef WINDOWS_PHONE
    return false;
#else
    int r, rr;

    name->append("", 1);

    r = int(name->size() / sizeof(wchar_t) + 1);

    sname->resize(r * sizeof(wchar_t));
    rr = GetShortPathNameW((LPCWSTR)name->data(), (LPWSTR)sname->data(), r);

    sname->resize(rr * sizeof(wchar_t));

    if (rr >= r)
    {
        rr = GetShortPathNameW((LPCWSTR)name->data(), (LPWSTR)sname->data(), rr);
        sname->resize(rr * sizeof(wchar_t));
    }

    name->resize(name->size() - 1);

    if (!rr)
    {
        sname->clear();
        return false;
    }

    // we are only interested in the path's last component
    wchar_t* ptr;

    if ((ptr = wcsrchr((wchar_t*)sname->data(), '\\')) || (ptr = wcsrchr((wchar_t*)sname->data(), ':')))
    {
        sname->erase(0, (char*)ptr - sname->data() + sizeof(wchar_t));
    }

    return sname->size();
#endif
}

// FIXME: if a folder rename fails because the target exists, do a top-down
// recursive copy/delete
bool WinFileSystemAccess::renamelocal(string* oldname, string* newname, bool replace)
{
    oldname->append("", 1);
    newname->append("", 1);
    bool r = !!MoveFileExW((LPCWSTR)oldname->data(), (LPCWSTR)newname->data(), replace ? MOVEFILE_REPLACE_EXISTING : 0);
    newname->resize(newname->size() - 1);
    oldname->resize(oldname->size() - 1);

    if (!r)
    {
        DWORD e = GetLastError();
        if (SimpleLogger::logCurrentLevel >= logWarning && !skip_errorreport)
        {
            string utf8oldname;
            client->fsaccess->local2path(oldname, &utf8oldname);

            string utf8newname;
            client->fsaccess->local2path(newname, &utf8newname);
            LOG_warn << "Unable to move file: " << utf8oldname.c_str() << " to " << utf8newname.c_str() << ". Error code: " << e;
        }
        transient_error = istransientorexists(e);
    }

    return r;
}

bool WinFileSystemAccess::copylocal(string* oldname, string* newname, m_time_t)
{
    oldname->append("", 1);
    newname->append("", 1);

#ifdef WINDOWS_PHONE
    bool r = SUCCEEDED(CopyFile2((LPCWSTR)oldname->data(), (LPCWSTR)newname->data(), NULL));
#else
    bool r = !!CopyFileW((LPCWSTR)oldname->data(), (LPCWSTR)newname->data(), FALSE);
#endif

    newname->resize(newname->size() - 1);
    oldname->resize(oldname->size() - 1);

    if (!r)
    {
        DWORD e = GetLastError();
        LOG_debug << "Unable to copy file. Error code: " << e;
        transient_error = istransientorexists(e);
    }

    return r;
}

bool WinFileSystemAccess::rmdirlocal(string* name)
{
    name->append("", 1);
    bool r = !!RemoveDirectoryW((LPCWSTR)name->data());
    name->resize(name->size() - 1);

    if (!r)
    {
        DWORD e = GetLastError();
        LOG_debug << "Unable to delete folder. Error code: " << e;
        transient_error = istransient(e);
    }

    return r;
}

bool WinFileSystemAccess::unlinklocal(string* name)
{
    name->append("", 1);
    bool r = !!DeleteFileW((LPCWSTR)name->data());
    name->resize(name->size() - 1);

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
void WinFileSystemAccess::emptydirlocal(string* name, dev_t basedev)
{
    HANDLE hDirectory, hFind;
    dev_t currentdev;

    int added = WinFileSystemAccess::sanitizedriveletter(name);
    name->append("", 1);

    WIN32_FILE_ATTRIBUTE_DATA fad;
    if (!GetFileAttributesExW((LPCWSTR)name->data(), GetFileExInfoStandard, (LPVOID)&fad)
        || !(fad.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        || fad.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
    {
        // discard files and resparse points (links, etc.)
        name->resize(name->size() - added - 1);
        return;
    }

#ifdef WINDOWS_PHONE
    CREATEFILE2_EXTENDED_PARAMETERS ex = { 0 };
    ex.dwSize = sizeof(ex);
    ex.dwFileFlags = FILE_FLAG_BACKUP_SEMANTICS;
    hDirectory = CreateFile2((LPCWSTR)name->data(), GENERIC_READ, FILE_SHARE_WRITE | FILE_SHARE_READ,
                        OPEN_EXISTING, &ex);
#else
    hDirectory = CreateFileW((LPCWSTR)name->data(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                             NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
#endif
    name->resize(name->size() - added - 1);
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
        name->append((const char*)(const wchar_t*)L"\\*", 5);
        WIN32_FIND_DATAW ffd;
    #ifdef WINDOWS_PHONE
        hFind = FindFirstFileExW((LPCWSTR)name->data(), FindExInfoBasic, &ffd, FindExSearchNameMatch, NULL, 0);
    #else
        hFind = FindFirstFileW((LPCWSTR)name->data(), &ffd);
    #endif
        name->resize(name->size() - 5);
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
                string childname = *name;
                childname.append((const char*)(const wchar_t*)L"\\", 2);
                childname.append((char*)ffd.cFileName, sizeof(wchar_t) * wcslen(ffd.cFileName));
                if (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                {
                    emptydirlocal(&childname , currentdev);
                    childname.append("", 1);
                    removed |= !!RemoveDirectoryW((LPCWSTR)childname.data());
                }
                else
                {
                    childname.append("", 1);
                    removed |= !!DeleteFileW((LPCWSTR)childname.data());
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

bool WinFileSystemAccess::mkdirlocal(string* name, bool hidden)
{
    name->append("", 1);
    bool r = !!CreateDirectoryW((LPCWSTR)name->data(), NULL);

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
        BOOL res = GetFileAttributesExW((LPCWSTR)name->data(), GetFileExInfoStandard, &a);

        if (res)
        {
            SetFileAttributesW((LPCWSTR)name->data(), a.dwFileAttributes | FILE_ATTRIBUTE_HIDDEN);
        }
#else
        DWORD a = GetFileAttributesW((LPCWSTR)name->data());

        if (a != INVALID_FILE_ATTRIBUTES)
        {
            SetFileAttributesW((LPCWSTR)name->data(), a | FILE_ATTRIBUTE_HIDDEN);
        }
#endif
    }

    name->resize(name->size() - 1);

    return r;
}

bool WinFileSystemAccess::setmtimelocal(string* name, m_time_t mtime)
{
#ifdef WINDOWS_PHONE
    return false;
#else
    FILETIME lwt;
    LONGLONG ll;
    HANDLE hFile;

    name->append("", 1);
    hFile = CreateFileW((LPCWSTR)name->data(), FILE_WRITE_ATTRIBUTES, FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    name->resize(name->size() - 1);

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

bool WinFileSystemAccess::chdirlocal(string* name) const
{
#ifdef WINDOWS_PHONE
    return false;
#else
    name->append("", 1);
    int r = SetCurrentDirectoryW((LPCWSTR)name->data());
    name->resize(name->size() - 1);

    return r;
#endif
}

size_t WinFileSystemAccess::lastpartlocal(string* name) const
{
    for (size_t i = name->size() / sizeof(wchar_t); i--;)
    {
        if (((wchar_t*)name->data())[i] == '\\'
                || ((wchar_t*)name->data())[i] == '/'
                || ((wchar_t*)name->data())[i] == ':')
        {
            return (i + 1) * sizeof(wchar_t);
        }
    }

    return 0;
}

// return lowercased ASCII file extension, including the . separator
bool WinFileSystemAccess::getextension(string* filename, char* extension, size_t size) const
{
    const wchar_t* ptr = (const wchar_t*)(filename->data() + filename->size() 
        - (filename->size() & 1));   // if the string has had an extra null char added for surety, get back on wchar_t boundary.

    char c;
    size_t i, j;

	size--;

	if (size * sizeof(wchar_t) > filename->size())
	{
		size = int(filename->size() / sizeof(wchar_t));
	}

	for (i = 0; i < size; i++)
	{
		if (*--ptr == '.')
		{
			for (j = 0; j <= i; j++)
			{
				if (*ptr < '.' || *ptr > 'z') return false;

				c = (char)*(ptr++);

				// tolower()
				if (c >= 'A' && c <= 'Z') c |= ' ';
                
                extension[j] = c;
			}
			
            extension[j] = 0;
            
			return true;
		}
	}

	return false;
}

bool WinFileSystemAccess::expanselocalpath(string *path, string *absolutepath)
{
    string localpath = *path;
    localpath.append("", 1);

#ifdef WINDOWS_PHONE
    wchar_t full[_MAX_PATH];
    if (_wfullpath(full, (wchar_t *)localpath.data(), _MAX_PATH))
    {
        absolutepath->assign((char *)full, wcslen(full) * sizeof(wchar_t));
        return true;
    }
    *absolutepath = *path;
    return false;
#else

    int len = GetFullPathNameW((LPCWSTR)localpath.data(), 0, NULL, NULL);
    if (len <= 0)
    {
        *absolutepath = *path;
        return false;
    }

    absolutepath->resize(len * sizeof(wchar_t));
    int newlen = GetFullPathNameW((LPCWSTR)localpath.data(), len, (LPWSTR)absolutepath->data(), NULL);
    if (newlen <= 0 || newlen >= len)
    {
        *absolutepath = *path;
        return false;
    }

    if (memcmp(absolutepath->data(), L"\\\\?\\", 8))
    {
        if (!memcmp(absolutepath->data(), L"\\\\", 4)) //network location
        {
            absolutepath->insert(0, (const char *)(const wchar_t*)L"\\\\?\\UNC\\", 16);
        }
        else
        {
            absolutepath->insert(0, (const char *)(const wchar_t*)L"\\\\?\\", 8);
        }
    }
    absolutepath->resize(absolutepath->size() - 2);
    return true;
#endif
}

void WinFileSystemAccess::osversion(string* u) const
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
            string localdata;
            string utf8data;
            localdata.assign((char *)pszData, cbData - sizeof(WCHAR));
            local2path(&localdata, &utf8data);
            id->append(utf8data);
        }
        RegCloseKey(hKey);
    }
#endif
}

// set DirNotify's root LocalNode
void WinDirNotify::addnotify(LocalNode* l, string*)
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
    if (GetVolumePathNameW((LPCWSTR)localbasepath.data(), volume, MAX_PATH + 1))
    {
        TCHAR fs[MAX_PATH + 1];
        if (GetVolumeInformation(volume, NULL, 0, NULL, NULL, NULL, fs, MAX_PATH + 1))
        {
            LOG_info << "Filesystem type: " << fs;
            return wcsicmp(fs, L"FAT")
                && wcsicmp(fs, L"FAT32")
                && wcsicmp(fs, L"exFAT");
        }
    }
    LOG_err << "Failed to get filesystem type. Error code: " << GetLastError();
    return true;
}

VOID CALLBACK WinDirNotify::completion(DWORD dwErrorCode, DWORD dwBytes, LPOVERLAPPED lpOverlapped)
{
#ifndef WINDOWS_PHONE
    WinDirNotify *dirnotify = (WinDirNotify*)lpOverlapped->hEvent;
    if (!dirnotify->exit && dwErrorCode != ERROR_OPERATION_ABORTED)
    {
        dirnotify->process(dwBytes);
    }
    else
    {
        dirnotify->enabled = false;
    }
#endif
}

void WinDirNotify::process(DWORD dwBytes)
{
#ifndef WINDOWS_PHONE
    if (!dwBytes)
    {
#ifdef ENABLE_SYNC
        LOG_err << "Empty filesystem notification: " << (localrootnode ? localrootnode->name.c_str() : "NULL")
                << " errors: " << error;
        error++;
        readchanges();
        notify(DIREVENTS, localrootnode, NULL, 0);
#endif
    }
    else
    {
        assert(dwBytes >= offsetof(FILE_NOTIFY_INFORMATION, FileName) + sizeof(wchar_t));

        char* ptr = (char*)notifybuf[active].data();

        active ^= 1;

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
             && (fni->FileNameLength < ignore.size()
              || memcmp((char*)fni->FileName, ignore.data(), ignore.size())
              || (fni->FileNameLength > ignore.size()
               && memcmp((char*)fni->FileName + ignore.size(), (const char*)(const wchar_t*)L"\\", sizeof(wchar_t)))))
            {
                if (SimpleLogger::logCurrentLevel >= logDebug)
                {
                    string local, path;
                    local.assign((char*)fni->FileName, fni->FileNameLength);
                    path.resize((local.size() + 1) * 4 / sizeof(wchar_t));
                    path.resize(WideCharToMultiByte(CP_UTF8, 0, (wchar_t*)local.data(),
                                                     int(local.size() / sizeof(wchar_t)),
                                                     (char*)path.data(),
                                                     int(path.size() + 1),
                                                     NULL, NULL));
#ifdef ENABLE_SYNC

                    LOG_debug << "Filesystem notification. Root: " << (localrootnode ? localrootnode->name.c_str() : "NULL") << "   Path: " << path;
#endif
                }
#ifdef ENABLE_SYNC
                notify(DIREVENTS, localrootnode, (char*)fni->FileName, fni->FileNameLength);
#endif
            }
            else if (SimpleLogger::logCurrentLevel >= logDebug)
            {
                string local, path;
                local.assign((char*)fni->FileName, fni->FileNameLength);
                path.resize((local.size() + 1) * 4 / sizeof(wchar_t));
                path.resize(WideCharToMultiByte(CP_UTF8, 0, (wchar_t*)local.data(),
                                                 int(local.size() / sizeof(wchar_t)),
                                                 (char*)path.data(),
                                                 int(path.size() + 1),
                                                 NULL, NULL));
#ifdef ENABLE_SYNC
                LOG_debug << "Skipped filesystem notification. Root: " << (localrootnode ? localrootnode->name.c_str() : "NULL") << "   Path: " << path;
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
}

// request change notifications on the subtree under hDirectory
void WinDirNotify::readchanges()
{
#ifndef WINDOWS_PHONE
    if (ReadDirectoryChangesW(hDirectory, (LPVOID)notifybuf[active].data(),
                              (DWORD)notifybuf[active].size(), TRUE,
                              FILE_NOTIFY_CHANGE_FILE_NAME
                            | FILE_NOTIFY_CHANGE_DIR_NAME
                            | FILE_NOTIFY_CHANGE_LAST_WRITE
                            | FILE_NOTIFY_CHANGE_SIZE
                            | FILE_NOTIFY_CHANGE_CREATION,
                              &dwBytes, &overlapped, completion))
    {
        failed = 0;
        enabled = true;
    }
    else
    {
        enabled = false;
        DWORD e = GetLastError();
        LOG_warn << "ReadDirectoryChanges not available. Error code: " << e << " errors: " << error;
        if (e == ERROR_NOTIFY_ENUM_DIR && error < 10)
        {
            // notification buffer overflow
            error++;
            readchanges();
        }
        else
        {
            // permanent failure - switch to scanning mode
            failed = e;
            failreason = "Fatal error returned by ReadDirectoryChangesW";
        }
    }
#endif
}

WinDirNotify::WinDirNotify(string* localbasepath, string* ignore) : DirNotify(localbasepath, ignore)
{
#ifndef WINDOWS_PHONE
    ZeroMemory(&overlapped, sizeof(overlapped));
    overlapped.hEvent = this;
    enabled = false;
    exit = false;
    active = 0;

    notifybuf[0].resize(65534);
    notifybuf[1].resize(65534);

    int added = WinFileSystemAccess::sanitizedriveletter(localbasepath);
    localbasepath->append("", 1);

    if ((hDirectory = CreateFileW((LPCWSTR)localbasepath->data(),
                                  FILE_LIST_DIRECTORY,
                                  FILE_SHARE_READ | FILE_SHARE_WRITE,
                                  NULL,
                                  OPEN_EXISTING,
                                  FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
                                  NULL)) != INVALID_HANDLE_VALUE)
    {
        failed = 0;
        readchanges();
    }
    else
    {
        failed = GetLastError();
        failreason = "CreateFileW was unable to open the folder";
        LOG_err << "Unable to initialize filesystem notifications. Error: " << failed;
    }

    localbasepath->resize(localbasepath->size() - added - 1);
#endif
}

WinDirNotify::~WinDirNotify()
{
     exit = true;

#ifndef WINDOWS_PHONE
    if (hDirectory != INVALID_HANDLE_VALUE)
    {
        if (enabled)
        {
            CancelIo(hDirectory);
            while (enabled)
            {
                SleepEx(INFINITE, true);
            }
        }

        CloseHandle(hDirectory);
    }
    fsaccess->dirnotifys.erase(this);
#endif
}

FileAccess* WinFileSystemAccess::newfileaccess()
{
    return new WinFileAccess(waiter);
}

DirAccess* WinFileSystemAccess::newdiraccess()
{
    return new WinDirAccess();
}

DirNotify* WinFileSystemAccess::newdirnotify(string* localpath, string* ignore)
{
    WinDirNotify *dirnotify = new WinDirNotify(localpath, ignore);
    dirnotify->fsaccess = this;
    dirnotifys.insert(dirnotify);
    return dirnotify;
}

bool WinFileSystemAccess::issyncsupported(string *localpath, bool *isnetwork)
{
    WCHAR VBoxSharedFolderFS[] = L"VBoxSharedFolderFS";
    string path, fsname;
    bool result = true;

#ifndef WINDOWS_PHONE
    localpath->append("", 1);
    path.resize(MAX_PATH * sizeof(WCHAR));
    fsname.resize(MAX_PATH * sizeof(WCHAR));

    if (GetVolumePathNameW((LPCWSTR)localpath->data(), (LPWSTR)path.data(), MAX_PATH)
        && GetVolumeInformationW((LPCWSTR)path.data(), NULL, 0, NULL, NULL, NULL, (LPWSTR)fsname.data(), MAX_PATH)
        && !memcmp(fsname.data(), VBoxSharedFolderFS, sizeof(VBoxSharedFolderFS)))
    {
        LOG_warn << "VBoxSharedFolderFS is not supported because it doesn't provide ReadDirectoryChanges() nor unique file identifiers";
        result = false;
    }

    if (GetDriveTypeW((LPCWSTR)path.data()) == DRIVE_REMOTE)
    {
        LOG_debug << "Network folder detected";
        if (isnetwork)
        {
            *isnetwork = true;
        }
    }

    string utf8fsname;
    local2path(&fsname, &utf8fsname);
    LOG_debug << "Filesystem type: " << utf8fsname;

    localpath->resize(localpath->size() - 1);
#endif

    return result;
}

bool WinDirAccess::dopen(string* name, FileAccess* f, bool glob)
{
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
        if (!glob)
        {
            name->append((const char*)(const wchar_t*)L"\\*", 5);
        }
        else
        {
            name->append("", 1);
        }

#ifdef WINDOWS_PHONE
        hFind = FindFirstFileExW((LPCWSTR)name->data(), FindExInfoBasic, &ffd, FindExSearchNameMatch, NULL, 0);
#else
        hFind = FindFirstFileW((LPCWSTR)name->data(), &ffd);
#endif

        if (glob)
        {
            wchar_t* bp = (wchar_t*)name->data();

            // store base path for glob() emulation
            int p = int(wcslen(bp));

            while (p--)
            {
                if (bp[p] == '/' || bp[p] == '\\')
                {
                    break;
                }
            }

            if (p >= 0)
            {
                globbase.assign((char*)bp, (p + 1) * sizeof(wchar_t));
            }
            else
            {
                globbase.clear();
            }
        }

        name->resize(name->size() - (glob ? 1 : 5));
    }

    if (!(ffdvalid = hFind != INVALID_HANDLE_VALUE))
    {
        return false;
    }

    return true;
}

// FIXME: implement followsymlinks
bool WinDirAccess::dnext(string* /*path*/, string* name, bool /*followsymlinks*/, nodetype_t* type)
{
    for (;;)
    {
        if (ffdvalid
         && !WinFileAccess::skipattributes(ffd.dwFileAttributes)
         && (!(ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
          || *ffd.cFileName != '.'
          || (ffd.cFileName[1] && ((ffd.cFileName[1] != '.') || ffd.cFileName[2]))))
        {
            name->assign((char*)ffd.cFileName, sizeof(wchar_t) * wcslen(ffd.cFileName));
            name->insert(0, globbase);

            if (type)
            {
                *type = (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? FOLDERNODE : FILENODE;
            }

            ffdvalid = false;
            return true;
        }
        else
        {
            if (ffdvalid && SimpleLogger::logCurrentLevel >= logDebug)
            {
                if (*ffd.cFileName != '.' && (ffd.cFileName[1] && ((ffd.cFileName[1] != '.') || ffd.cFileName[2])))
                {
                    string local, excluded;
                    local.assign((char*)ffd.cFileName, sizeof(wchar_t) * wcslen(ffd.cFileName));
                    excluded.resize((local.size() + 1) * 4 / sizeof(wchar_t));
                    excluded.resize(WideCharToMultiByte(CP_UTF8, 0, (wchar_t*)local.data(),
                                                     int(local.size() / sizeof(wchar_t)),
                                                     (char*)excluded.data(),
                                                     int(excluded.size() + 1),
                                                     NULL, NULL));
                    LOG_debug << "Excluded: " << excluded << "   Attributes: " << ffd.dwFileAttributes;
                }
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
