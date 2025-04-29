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

#include <cctype>
#include <cwctype>
#include <iomanip>

#include "mega.h"
#include <limits>
#include <wow64apiset.h>

#if defined(_WIN32)
#include <winioctl.h>
#endif

namespace mega {

class ScopedFileHandle
{
public:
    ScopedFileHandle()
      : mHandle(INVALID_HANDLE_VALUE)
    {
    }

    ScopedFileHandle(HANDLE handle)
      : mHandle(handle)
    {
    }

    MEGA_DISABLE_COPY(ScopedFileHandle);

    ScopedFileHandle(ScopedFileHandle&& other)
      : mHandle(other.mHandle)
    {
        other.mHandle = INVALID_HANDLE_VALUE;
    }

    ~ScopedFileHandle()
    {
        if (mHandle != INVALID_HANDLE_VALUE)
            CloseHandle(mHandle);
    }

    ScopedFileHandle& operator=(ScopedFileHandle&& rhs)
    {
        using std::swap;

        ScopedFileHandle temp(std::move(rhs));

        swap(*this, temp);

        return *this;
    }

    operator bool() const
    {
        return mHandle != INVALID_HANDLE_VALUE;
    }

    HANDLE get() const
    {
        return mHandle;
    }

    void reset(HANDLE handle)
    {
        operator=(ScopedFileHandle(handle));
    }

    void reset()
    {
        operator=(ScopedFileHandle());
    }

private:
    HANDLE mHandle;
};

void FileSystemAccess::setMinimumDirectoryPermissions(int)
{
}

void FileSystemAccess::setMinimumFilePermissions(int)
{
}

int FileSystemAccess::isFileHidden(const LocalPath& path, FSLogging logWhen)
{
    // Try and determine the file's current attributes.
    auto attributes = GetFileAttributesW(path.asPlatformEncoded(false).c_str());

    // Successfully retrieved the file's attributes.
    if (attributes != INVALID_FILE_ATTRIBUTES)
        return (attributes & FILE_ATTRIBUTE_HIDDEN) > 0;

    // Why couldn't we get the file's attributes?
    auto error = GetLastError();

    // Log the error, if necessary.
    if (logWhen.doLog(error))
    {
        LOG_warn << "Unable to retrieve file attributes: path: "
                 << path
                 << ", error code: "
                 << error
                 << ", error message: "
                 << getErrorMessage(error);
    }

    // Couldn't retrieve the file's attributes.
    return -1;
}

bool FileSystemAccess::setFileHidden(const LocalPath& path, FSLogging logWhen)
{
    // Try and retrieve the file's current attributes.
    auto pathStr{path.asPlatformEncoded(false)};
    auto attributes = GetFileAttributesW(pathStr.c_str());

    // File's already marked as hidden.
    if ((attributes & FILE_ATTRIBUTE_HIDDEN))
        return true;

    // File's now marked as hidden.
    if (attributes != INVALID_FILE_ATTRIBUTES &&
        SetFileAttributesW(pathStr.c_str(), attributes | FILE_ATTRIBUTE_HIDDEN))
        return true;

    // Why couldn't we get (or set) the file's attributes?
    auto error = GetLastError();

    // Log error, if necessary.
    if (logWhen.doLog(error))
    {
        LOG_warn << "Unable to set file attributes: path: "
                 << path
                 << ", error code: "
                 << error
                 << ", error message: "
                 << getErrorMessage(error);
    }

    // Couldn't set the file's hidden attribute.
    return false;
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
    fclose();
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

void WinFileAccess::fclose()
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

    hFile = INVALID_HANDLE_VALUE;
    hFind = INVALID_HANDLE_VALUE;
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

bool WinFileAccess::ftruncate(m_off_t newSize)
{
    assert(newSize >= 0);

    auto& position = reinterpret_cast<LARGE_INTEGER&>(newSize);

    // Set the file pointer to the start of the file.
    if (SetFilePointerEx(hFile, position, nullptr, FILE_BEGIN))
    {
        // Truncate the file.
        if (SetEndOfFile(hFile))
        {
            return true;
        }
    }

    // Why couldn't we truncate the file?
    auto error = GetLastError();

    // Latch the error.
    errorcode = error;

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

bool WinFileAccess::fstat(m_time_t& modified, m_off_t& fileSize)
{
    BY_HANDLE_FILE_INFORMATION info;

    // Try and retrieve information the currently open file.
    if (!GetFileInformationByHandle(hFile, &info))
    {
        // Couldn't get information about the file.
        auto error = GetLastError();

        // Latch the error.
        errorcode = error;

        // Was the error transient?
        retry = WinFileSystemAccess::istransient(error);

        // Let the caller know we couldn't get the file's info.
        return false;
    }

    LARGE_INTEGER temp;

    temp.LowPart = info.nFileSizeLow;
    temp.HighPart = info.nFileSizeHigh;

    modified = FileTime_to_POSIX(&info.ftLastWriteTime);
    fileSize = temp.QuadPart;

    // Let the caller know we've retrieved the file's info.
    return true;
}

bool WinFileAccess::sysstat(m_time_t* modificationTime, m_off_t* fileSize, FSLogging fsl)
{
    assert(!nonblocking_localname.empty());
    WIN32_FILE_ATTRIBUTE_DATA fad;

    type = TYPE_UNKNOWN;
    if (!GetFileAttributesExW(nonblocking_localname.asPlatformEncoded(false).c_str(),
                              GetFileExInfoStandard,
                              (LPVOID)&fad))
    {
        DWORD e = GetLastError();
        if (fsl.doLog(e))
        {
            LOG_warn << "Unable to stat: GetFileAttributesExW('" << nonblocking_localname << "'): error code: " << e << ": " << WinFileSystemAccess::getErrorMessage(e);
        }
        errorcode = e;
        retry = WinFileSystemAccess::istransient(e);
        return false;
    }

    errorcode = 0;
    if (SimpleLogger::getLogLevel() >= logDebug && skipattributes(fad.dwFileAttributes))
    {
        LOG_debug << "Incompatible attributes (" << fad.dwFileAttributes << ") for file " << nonblocking_localname;
    }

    if (fad.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
    {
        type = FOLDERNODE;
        retry = false;
        return false;
    }

    retry = false;
    type = FILENODE;
    *modificationTime = FileTime_to_POSIX(&fad.ftLastWriteTime);
    *fileSize = ((m_off_t)fad.nFileSizeHigh << 32) + (m_off_t)fad.nFileSizeLow;

    return true;
}

bool WinFileAccess::sysopen(bool async, FSLogging fsl)
{
    assert(hFile == INVALID_HANDLE_VALUE);
    assert(!nonblocking_localname.empty());

    if (hFile != INVALID_HANDLE_VALUE)
    {
        sysclose();
    }

    hFile = CreateFileW(nonblocking_localname.asPlatformEncoded(false).c_str(),
                        GENERIC_READ,
                        FILE_SHARE_WRITE | FILE_SHARE_READ,
                        NULL,
                        OPEN_EXISTING,
                        async ? FILE_FLAG_OVERLAPPED : 0,
                        NULL);

    if (hFile == INVALID_HANDLE_VALUE)
    {
        DWORD e = GetLastError();
        errorcode = e;
        if (fsl.doLog(errorcode))
        {
            LOG_err << "Unable to open file '" << nonblocking_localname << "': (CreateFileW). Error code: " << e << ": " << WinFileSystemAccess::getErrorMessage(e);
        }
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

bool WinFileAccess::asyncavailable()
{
    return true;
}

void WinFileAccess::asyncsysopen(AsyncIOContext *context)
{
    bool read = context->access & AsyncIOContext::ACCESS_READ;
    bool write = context->access & AsyncIOContext::ACCESS_WRITE;

    context->failed = !fopen_impl(context->openPath, read, write, FSLogging::logOnError, true, nullptr, false, false, nullptr);
    context->retry = retry;
    context->finished = true;
    if (context->userCallback)
    {
        context->userCallback(context->userData);
    }
}

void WinFileAccess::asyncsysread(AsyncIOContext *context)
{
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
}

void WinFileAccess::asyncsyswrite(AsyncIOContext *context)
{
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
}

// update local name
void WinFileAccess::updatelocalname(const LocalPath& name, bool force)
{
    if (force || !nonblocking_localname.empty())
    {
        nonblocking_localname = name;
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
bool WinFileAccess::fopen(const LocalPath& name, bool read, bool write, FSLogging fsl,
                          DirAccess* iteratingDir, bool ignoreAttributes, bool skipcasecheck, LocalPath* actualLeafNameIfDifferent)
{
    fopenSucceeded = fopen_impl(name, read, write, fsl, false, iteratingDir, ignoreAttributes, skipcasecheck, actualLeafNameIfDifferent);
    return fopenSucceeded;
}

bool WinFileAccess::fopen_impl(const LocalPath& namePath, bool read, bool write, FSLogging fsl,
                               bool async, DirAccess* iteratingDir, bool ignoreAttributes, bool skipcasecheck, LocalPath* actualLeafNameIfDifferent)
{
    WIN32_FIND_DATA fad = { 0 };
    assert(hFile == INVALID_HANDLE_VALUE);
    BY_HANDLE_FILE_INFORMATION bhfi = { 0 };

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
            auto namePathStr{namePath.asPlatformEncoded(false)};
            HANDLE h = namePathStr.size() > 1 ? FindFirstFileExW(namePathStr.c_str(),
                                                                 FindExInfoStandard,
                                                                 &fad,
                                                                 FindExSearchNameMatch,
                                                                 NULL,
                                                                 0) :
                                                INVALID_HANDLE_VALUE;

            if (h != INVALID_HANDLE_VALUE)
            {
                // success so `fad` is set
                FindClose(h);
            }
            else
            {
                WIN32_FILE_ATTRIBUTE_DATA fatd;
                if (!GetFileAttributesExW(namePathStr.c_str(),
                                          GetFileExInfoStandard,
                                          (LPVOID)&fatd))
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

        if (actualLeafNameIfDifferent)
        {
            auto actualFilename = LocalPath::fromPlatformEncodedRelative(wstring(fad.cFileName));

            if (actualFilename != namePath.leafName())
            {
                *actualLeafNameIfDifferent = std::move(actualFilename);
            }
        }

        if (!skipcasecheck)
        {
            LocalPath filename = namePath.leafName();

            if (const auto filenameStr = filename.asPlatformEncoded(false);
                filenameStr != std::wstring(fad.cFileName) &&
                filenameStr != std::wstring(fad.cAlternateFileName) && filenameStr != L"." &&
                filenameStr != L"..")
            {
                LOG_warn << "fopen failed due to invalid case: '" << filename
                         << "' vs '" << LocalPath::fromPlatformEncodedRelative(wstring(fad.cFileName)) << "'";
                retry = false;
                return false;
            }
        }

        // ignore symlinks - they would otherwise be treated as moves
        // also, ignore some other obscure filesystem object categories
        if (!ignoreAttributes && skipattributes(fad.dwFileAttributes))
        {
            if (SimpleLogger::getLogLevel() >= logDebug)
            {
                LOG_debug << "Excluded: " << namePath << "   Attributes: " << fad.dwFileAttributes;
            }
            retry = false;
            return false;
        }

        type = (fad.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? FOLDERNODE : FILENODE;

        if (fad.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
        {
            if (WinFileSystemAccess::checkForSymlink(namePath))
            {
                mIsSymLink = true;
            }
        }
    }

    auto desiredAccess = 0u;

    // Caller's interested in reading.
    if (read)
        desiredAccess |= GENERIC_READ;

    // Caller's interested in writing.
    if (write)
        desiredAccess |= GENERIC_WRITE;

    // (race condition between GetFileAttributesEx()/FindFirstFile() possible -
    // fixable with the current Win32 API?)
    hFile = CreateFileW(namePath.asPlatformEncoded(false).c_str(),
                        desiredAccess,
                        FILE_SHARE_WRITE | FILE_SHARE_READ,
                        NULL,
                        !write ? OPEN_EXISTING : OPEN_ALWAYS,
                        (type == FOLDERNODE) ? FILE_FLAG_BACKUP_SEMANTICS :
                                               (async ? FILE_FLAG_OVERLAPPED : 0),
                        NULL);

    // FIXME: verify that keeping the directory opened quashes the possibility
    // of a race condition between CreateFile() and FindFirstFile()

    if (hFile == INVALID_HANDLE_VALUE)
    {
        DWORD e = GetLastError();
        if (fsl.doLog(e))
        {
            LOG_err << "Unable to open file. '" << namePath << "' error code : " << e << " : " << WinFileSystemAccess::getErrorMessage(e);
        }
        errorcode = e;
        retry = WinFileSystemAccess::istransient(e);
        return false;
    }

    mtime = FileTime_to_POSIX(&fad.ftLastWriteTime);

    if (!write)
    {
        fsidvalid = GetFileInformationByHandle(hFile, &bhfi) != FALSE;
        if (fsidvalid)
            fsid = ((handle)bhfi.nFileIndexHigh << 32) | (handle)bhfi.nFileIndexLow;
    }

    if (type == FOLDERNODE)
    {
        LocalPath withStar = namePath;
        withStar.appendWithSeparator(LocalPath::fromPlatformEncodedRelative(L"*"), true);

        hFind = FindFirstFileW(withStar.asPlatformEncoded(false).c_str(), &ffd);

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
    }

    return true;
}

WinFileSystemAccess::WinFileSystemAccess()
{
}

WinFileSystemAccess::~WinFileSystemAccess()
{
#ifdef ENABLE_SYNC
    assert(!dirnotifys.size());
#endif
}

bool WinFileSystemAccess::cwd(LocalPath& path) const
{
    DWORD nRequired = GetCurrentDirectoryW(0, nullptr);

    if (!nRequired)
    {
        return false;
    }

    wstring buf;
    buf.resize(nRequired);
    DWORD nWritten = GetCurrentDirectoryW(nRequired, &buf[0]);
    while (!buf.empty() && buf.back() == 0)
    {
        buf.pop_back();
    }

    path = LocalPath::fromPlatformEncodedAbsolute(std::move(buf));

    return nWritten > 0;
}

bool WinFileSystemAccess::hardLink(const LocalPath& source, const LocalPath& target)
{
    if (!CreateHardLinkW(target.asPlatformEncoded(false).c_str(),
                         source.asPlatformEncoded(false).c_str(),
                         nullptr))
    {
        LOG_warn << "Unable to create hard link from "
                 << source
                 << " to "
                 << target
                 << ". Error code was: "
                 << GetLastError();

        return false;
    }

    return true;
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

void WinFileSystemAccess::addevents(Waiter*, int) {}

// write short name of the last path component to sname
bool WinFileSystemAccess::getsname(const LocalPath& namePath, LocalPath& snamePath) const
{
    assert(namePath.isAbsolute());

    const std::wstring& name = namePath.asPlatformEncoded(false);
    std::wstring sname = snamePath.asPlatformEncoded(false);

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
        LOG_warn << "Unable to get short path name: " << namePath << ". Error code: " << e;
        snamePath.clear();
        return false;
    }

    // we are only interested in the path's last component
    wchar_t* ptr;

    ptr = wcsrchr(const_cast<wchar_t*>(sname.data()), L'\\');
    if (!ptr)
    {
        ptr = wcsrchr(const_cast<wchar_t*>(sname.data()), L':');
    }
    if (ptr)
    {
        sname.erase(0, ptr - sname.data() + 1);
    }

    snamePath = LocalPath::fromPlatformEncodedRelative(std::move(sname));
    return !snamePath.empty();
}

// FIXME: if a folder rename fails because the target exists, do a top-down
// recursive copy/delete
bool WinFileSystemAccess::renamelocal(const LocalPath& oldnamePath, const LocalPath& newnamePath, bool replace)
{
    assert(oldnamePath.isAbsolute());
    assert(newnamePath.isAbsolute());
    bool r = !!MoveFileExW(oldnamePath.asPlatformEncoded(false).c_str(),
                           newnamePath.asPlatformEncoded(false).c_str(),
                           replace ? MOVEFILE_REPLACE_EXISTING : 0);

    if (!r)
    {
        DWORD e = GetLastError();

        target_name_too_long = isPathError(e)
                               && exists(oldnamePath)
                               && exists(newnamePath.parentPath());

        transient_error = istransientorexists(e);

        if (!target_exists || !skip_targetexists_errorreport)
        {
            LOG_warn << "Unable to move file: " << oldnamePath <<
                        " to " << newnamePath << ". Error code: " << e;
        }
    }

    return r;
}

bool WinFileSystemAccess::copylocal(const LocalPath& oldnamePath, const LocalPath& newnamePath, m_time_t)
{
    assert(oldnamePath.isAbsolute());
    assert(newnamePath.isAbsolute());
    bool r = !!CopyFileW(oldnamePath.asPlatformEncoded(false).c_str(),
                         newnamePath.asPlatformEncoded(false).c_str(),
                         FALSE);

    if (!r)
    {
        DWORD e = GetLastError();

        LOG_debug << "Unable to copy file. Error code: " << e;

        target_name_too_long = isPathError(e)
                               && exists(oldnamePath)
                               && exists(newnamePath.parentPath());

        transient_error = istransientorexists(e);
    }

    return r;
}

bool WinFileSystemAccess::rmdirlocal(const LocalPath& namePath)
{
    assert(namePath.isAbsolute());
    bool r = !!RemoveDirectoryW(namePath.asPlatformEncoded(false).data());
    if (!r)
    {
        DWORD e = GetLastError();
        LOG_debug << "Unable to delete folder. Error code: " << e;
        transient_error = istransient(e);
    }

    return r;
}

bool WinFileSystemAccess::unlinklocal(const LocalPath& namePath)
{
    assert(namePath.isAbsolute());
    bool r = !!DeleteFileW(namePath.asPlatformEncoded(false).data());

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
void WinFileSystemAccess::emptydirlocal(const LocalPath& nameParam, dev_t basedev)
{
    assert(nameParam.isAbsolute());
    HANDLE hDirectory, hFind;
    dev_t currentdev;

    WIN32_FILE_ATTRIBUTE_DATA fad;
    if (!GetFileAttributesExW(nameParam.asPlatformEncoded(false).c_str(),
                              GetFileExInfoStandard,
                              (LPVOID)&fad) ||
        !(fad.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
        fad.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
    {
        return;
    }

    hDirectory = CreateFileW(nameParam.asPlatformEncoded(false).c_str(),
                             GENERIC_READ,
                             FILE_SHARE_READ | FILE_SHARE_WRITE,
                             NULL,
                             OPEN_EXISTING,
                             FILE_FLAG_BACKUP_SEMANTICS,
                             NULL);
    if (hDirectory == INVALID_HANDLE_VALUE)
    {
        // discard not accessible folders
        return;
    }

    BY_HANDLE_FILE_INFORMATION fi;
    if (!GetFileInformationByHandle(hDirectory, &fi))
    {
        currentdev = 0;
    }
    else
    {
        currentdev = fi.dwVolumeSerialNumber + 1;
    }
    CloseHandle(hDirectory);
    if (basedev && currentdev != basedev)
    {
        // discard folders on different devices
        return;
    }

    LocalPath namePath = nameParam;
    bool removed;
    for (;;)
    {
        // iterate over children and delete
        removed = false;

        WIN32_FIND_DATAW ffd;
        {
            auto newPath = namePath;
            newPath.appendWithSeparator(LocalPath::fromRelativePath("*"), true);
            hFind = FindFirstFileW(newPath.asPlatformEncoded(false).c_str(), &ffd);
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
                auto newPath = namePath;
                newPath.appendWithSeparator(LocalPath::fromPlatformEncodedRelative(ffd.cFileName),
                                            true);
                if (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                {
                    emptydirlocal(newPath, currentdev);
                    removed |= !!RemoveDirectoryW(newPath.asPlatformEncoded(false).c_str());
                }
                else
                {
                    removed |= !!DeleteFileW(newPath.asPlatformEncoded(false).c_str());
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

bool WinFileSystemAccess::mkdirlocal(const LocalPath& namePath, bool hidden, bool logAlreadyExistsError)
{
    assert(namePath.isAbsolute());
    const std::wstring& name = namePath.asPlatformEncoded(false);

    bool r = !!CreateDirectoryW(name.data(), NULL);

    if (!r)
    {
        DWORD e = GetLastError();

        target_name_too_long = isPathError(e) && exists(namePath.parentPath());
        transient_error = istransientorexists(e);

        if (!target_exists || logAlreadyExistsError)
        {
            LOG_debug << "Unable to create folder. Error code: " << e << " for: " << namePath;
        }
    }
    else if (hidden)
    {
        DWORD a = GetFileAttributesW(name.data());

        if (a != INVALID_FILE_ATTRIBUTES)
        {
            SetFileAttributesW(name.data(), a | FILE_ATTRIBUTE_HIDDEN);
        }
    }

    return r;
}

bool WinFileSystemAccess::setmtimelocal(const LocalPath& namePath, m_time_t mtime)
{
    assert(namePath.isAbsolute());
    FILETIME lwt;
    LONGLONG ll;
    HANDLE hFile;

    hFile = CreateFileW(namePath.asPlatformEncoded(false).data(),
                        FILE_WRITE_ATTRIBUTES,
                        FILE_SHARE_WRITE,
                        NULL,
                        OPEN_EXISTING,
                        0,
                        NULL);

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

    return r != 0;
}

bool WinFileSystemAccess::chdirlocal(LocalPath& namePath) const
{
    assert(namePath.isAbsolute());
    int r = SetCurrentDirectoryW(namePath.asPlatformEncoded(false).c_str());
    return r != 0;
}

bool WinFileSystemAccess::expanselocalpath(const LocalPath& pathArg, LocalPath& absolutepathArg)
{
    int len = GetFullPathNameW(pathArg.asPlatformEncoded(false).c_str(), 0, NULL, NULL);
    // just get size including NUL terminator
    if (len <= 0)
    {
        absolutepathArg = pathArg;
        return false;
    }

    std::wstring newAbsolutepathStr{absolutepathArg.asPlatformEncoded(false)};
    newAbsolutepathStr.resize(len);
    int newlen = GetFullPathNameW(pathArg.asPlatformEncoded(false).c_str(),
                                  len,
                                  const_cast<wchar_t*>(newAbsolutepathStr.data()),
                                  NULL);
    // length not including terminating NUL
    if (newlen <= 0 || newlen >= len)
    {
        absolutepathArg = pathArg;
        return false;
    }
    newAbsolutepathStr.resize(newlen);

    if (!Utils::startswith(newAbsolutepathStr, L"\\\\?\\"))
    {
        if (Utils::startswith(newAbsolutepathStr, L"\\\\")) // network location
        {
            newAbsolutepathStr.insert(0, L"\\\\?\\UNC\\");
        }
        else
        {
            newAbsolutepathStr.insert(0, L"\\\\?\\");
        }
    }

    absolutepathArg = LocalPath::fromPlatformEncodedAbsolute(std::move(newAbsolutepathStr));
    return true;
}

bool WinFileSystemAccess::exists(const LocalPath& path) const
{
    auto attributes = GetFileAttributesW(path.asPlatformEncoded(false).c_str());

    return attributes != INVALID_FILE_ATTRIBUTES;
}

bool WinFileSystemAccess::isPathError(DWORD error) const
{
    return error == ERROR_DIRECTORY
           || error == ERROR_FILE_NOT_FOUND
           || error == ERROR_FILENAME_EXCED_RANGE
           || error == ERROR_INVALID_NAME
           || error == ERROR_PATH_NOT_FOUND;
}

void WinFileSystemAccess::osversion(string* u, bool includeArchExtraInfo) const
{
    char buf[128];

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

    u->append(buf);
}

void WinFileSystemAccess::statsid(string *id) const
{
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
            LocalPath::local2path(&localdata, &utf8data, true);  // true because that was the case historically
            id->append(utf8data);
        }
        RegCloseKey(hKey);
    }
}

fsfp_t FileSystemAccess::fsFingerprint(const LocalPath& path) const
{
    // Convenience.
    static auto failed = []() {
        auto error = GetLastError();

        LOG_err << "Unable to determine volume ID: "
                << getErrorMessage(error);

        return fsfp_t();
    };  // failed

    // Try and open the specified file.
    ScopedFileHandle handle = CreateFileW(path.asPlatformEncoded(false).c_str(),
                                          FILE_LIST_DIRECTORY,
                                          FILE_SHARE_READ | FILE_SHARE_WRITE,
                                          nullptr,
                                          OPEN_EXISTING,
                                          FILE_FLAG_BACKUP_SEMANTICS,
                                          nullptr);

    // Couldn't open the specified file.
    if (!handle)
        return failed();

    BY_HANDLE_FILE_INFORMATION info;

    // Try and retrieve information about the specified file.
    if (!GetFileInformationByHandle(handle.get(), &info))
        return failed();

    // Convert serial number to a string.
    std::ostringstream ostream;

    ostream << std::hex
            << std::setfill('0')
            << std::setw(16)
            << info.dwVolumeSerialNumber;

    // Return ID to caller.
    return fsfp_t(info.dwVolumeSerialNumber + 1, ostream.str());
}

#ifdef ENABLE_SYNC

bool WinFileSystemAccess::fsStableIDs(const LocalPath& path) const
{
    TCHAR volume[MAX_PATH + 1];
    if (GetVolumePathNameW(path.asPlatformEncoded(false).c_str(), volume, MAX_PATH + 1))
    {
        TCHAR fs[MAX_PATH + 1] = { 0, };
        BOOL gotVolInfo = GetVolumeInformation(volume, NULL, 0, NULL, NULL, NULL, fs, MAX_PATH + 1);
        if (!gotVolInfo)
        {
            // Maybe it's a subst drive (created using something like "subst a: c:\Source" DOS command).
            // In such cases, the volume path might include additional characters after ":\\", e.g. "C:\\SomeFolder".
            // To resolve that, we truncate the volume path to end after ":\\" and retry.
            wchar_t* volSep = wcsstr(volume, L":\\");
            if (volSep && *(volSep + 2))
            {
                *(volSep + 2) = L'\0'; // Truncate the volume path
                gotVolInfo = GetVolumeInformation(volume, NULL, 0, NULL, NULL, NULL, fs, MAX_PATH + 1);
            }
        }

        if (gotVolInfo)
        {
            LOG_info << "Filesystem type: " << LocalPath::fromPlatformEncodedRelative(std::wstring(fs));
            return _wcsicmp(fs, L"FAT")
                && _wcsicmp(fs, L"FAT32")
                && _wcsicmp(fs, L"exFAT");
        }
    }
    LOG_err << "Failed to get filesystem type for path: '" << path << "'. Error code: " << GetLastError();
    assert(false);
    return true;
}

VOID CALLBACK WinDirNotify::completion(DWORD dwErrorCode, DWORD dwBytes, LPOVERLAPPED lpOverlapped)
{
    assert( std::this_thread::get_id() == smNotifierThread->get_id());
    WinDirNotify *dirnotify = (WinDirNotify*)lpOverlapped->hEvent;
    if (!dirnotify->mOverlappedExit && dwErrorCode != ERROR_OPERATION_ABORTED)
    {
        dirnotify->process(dwBytes);
    }
    else
    {
        dirnotify->mOverlappedEnabled = false;
    }
}

void WinDirNotify::process(DWORD bytesTransferred)
{
    assert( std::this_thread::get_id() == smNotifierThread->get_id());

    if (!bytesTransferred)
    {
        // No bytes delivered indicates the OS could not deliver some notifications.
        // Maybe it ran out of buffer (maybe we were too slow)
        // Incrementing mErrorCount will cause a full rescan of the sync
        // We used to send an additional notification with localnode and empty path to
        // trigger it but that is not needed anymore

        int errCount = ++mErrorCount;
        LOG_err << "Empty filesystem notification: "
                << (localrootnode ? localrootnode->localname.asPlatformEncoded(false).c_str() :
                                    L"NULL")
                << " errors: " << errCount;

        // reissue request for notifications
        readchanges();
    }
    else
    {
        assert(bytesTransferred >=
               offsetof(FILE_NOTIFY_INFORMATION,
                        FileName)); // 3 uint32_t.  The filename can be entirely absent, with the
                                    // filename length field 0  (via samba share from qnap device)

        string processbuf;
        if (bytesTransferred <= 4096)
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

#ifdef ENABLE_SYNC
            //LOG_verbose << "FS notification: " << fni->Action << " " << LocalPath::fromPlatformEncoded(std::wstring(fni->FileName, fni->FileNameLength / sizeof(fni->FileName[0]))).toPath();

            //FILE_ACTION_RENAMED_OLD_NAME: we let this one through now.  Eg for move of folder out of the sync.  Though, we should also get FILE_ACTION_MODIFIED for its containing folder.  Belt & braces.

            auto scanRequirement = fni->Action == FILE_ACTION_MODIFIED  // FILE_ACTION_MODIFIED: for directories, if an entry was added or removed.  Or if accessibilty permissions of this folder changed.
                ? Notification::FOLDER_NEEDS_SELF_SCAN
                : Notification::NEEDS_PARENT_SCAN;
            notify(fsEventq, localrootnode, scanRequirement,
                LocalPath::fromPlatformEncodedRelative(std::wstring(fni->FileName, fni->FileNameLength / sizeof(fni->FileName[0]))));
#endif

            if (!fni->NextEntryOffset)
            {
                break;
            }

            ptr += fni->NextEntryOffset;
        }
    }
    clientWaiter->notify();
}

// request change notifications on the subtree under hDirectory
void WinDirNotify::readchanges()
{
    assert( std::this_thread::get_id() == smNotifierThread->get_id());

    if (notifybuf.size() != 65534)
    {
        // Use 65534 for the buffer size because (from doco):
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
                            | FILE_NOTIFY_CHANGE_CREATION
                            | FILE_NOTIFY_CHANGE_SECURITY,  // so we can know if we lose/gain access to a scan-blocked folder
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

WinDirNotify::WinDirNotify(LocalNode& root,
                           const LocalPath& rootPath,
                           WinFileSystemAccess* owner,
                           Waiter* waiter)
  : DirNotify(rootPath)
{
    assert(rootPath.isAbsolute());
    fsaccess = owner;
    fsaccess->dirnotifys.insert(this);
    clientWaiter = waiter;

#ifdef ENABLE_SYNC
    localrootnode = &root;
#endif // ENABLE_SYNC

    {
        // If this is the first Notifier created, start the thread that queries the OS for notifications.
        std::lock_guard<std::mutex> g(smNotifyMutex);
        if (++smNotifierCount == 1)
        {
            smQueue.clear();
            smEventHandle = CreateEvent(NULL, FALSE, FALSE, NULL);

            // One thread to notify them all
            smNotifierThread.reset(new std::thread([](){
                // Process directory enumeration requests.
                notifierThreadFunction();
            }));
        }
    }

    ZeroMemory(&overlapped, sizeof(overlapped));
    overlapped.hEvent = this;
    mOverlappedEnabled = false;
    mOverlappedExit = false;

    // ReadDirectoryChangesW: If you opened the file using the short name, you can receive change notifications for the short name.  (so make sure it's a long name)
    std::wstring longname;
    std::wstring name{localbasepath.asPlatformEncoded(false)};
    auto r = name.size() + 20;
    longname.resize(r);
    auto rr = GetLongPathNameW(name.data(), const_cast<wchar_t*>(longname.data()), DWORD(r));

    longname.resize(rr);
    if (rr >= r)
    {
        rr = GetLongPathNameW(name.data(), const_cast<wchar_t*>(longname.data()), rr);
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

        // So we know when we've asked the system for directory notifications.
        std::promise<void> requested;

        {
            std::lock_guard<std::mutex> g(smNotifyMutex);

            smQueue.push_back([&requested, this](){
                // Ask the system to report directory change notifications.
                readchanges();

                // Let queuing thread know we've asked the system for notifications.
                requested.set_value();
            });
        }

        // Let notification thread know there's work to do.
        SetEvent(smEventHandle);

        // Wait until the notification thread has processed our request.
        requested.get_future().get();
    }
    else
    {
        int err = GetLastError();
        setFailed(err, "CreateFileW was unable to open the folder");
        LOG_err << "Unable to initialize filesystem notifications. Error: " << err;
    }
}

WinDirNotify::~WinDirNotify()
{
    mOverlappedExit = true;

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
            {
                std::lock_guard<std::mutex> g(smNotifyMutex);
                smQueue.clear();
            }
        }
    }

}
#endif   // ENABLE_SYNC

std::unique_ptr<FileAccess> WinFileSystemAccess::newfileaccess(bool /*followSymLinks*/)
{
    return std::unique_ptr<FileAccess>(new WinFileAccess(waiter));
}

bool WinFileSystemAccess::getlocalfstype(const LocalPath& path, FileSystemType& type) const
{
    using std::wstring;

    // Where is the volume containing our file mounted?
    wstring mountPoint(MAX_PATH + 1, L'\0');

    if (!GetVolumePathNameW(path.asPlatformEncoded(false).c_str(),
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

unique_ptr<DirAccess> WinFileSystemAccess::newdiraccess()
{
    return unique_ptr<DirAccess>(new WinDirAccess());
}

#ifdef ENABLE_SYNC
DirNotify* WinFileSystemAccess::newdirnotify(LocalNode& root,
                                             const LocalPath& rootPath,
                                             Waiter* notificationWaiter)
{
    return new WinDirNotify(root, rootPath, this, notificationWaiter);
}
#endif

bool WinFileSystemAccess::issyncsupported(const LocalPath& localpathArg, bool& isnetwork, SyncError& syncError, SyncWarning& syncWarning)
{
    static const wchar_t* VBoxSharedFolderFS = L"VBoxSharedFolderFS";
    std::wstring path, fsname;
    bool result = true;
    isnetwork = false;
    syncError = NO_SYNC_ERROR;
    syncWarning = NO_SYNC_WARNING;

    path.resize(MAX_PATH * sizeof(WCHAR));
    fsname.resize(MAX_PATH * sizeof(WCHAR));

    if (GetVolumePathNameW(localpathArg.asPlatformEncoded(false).data(),
                           const_cast<wchar_t*>(path.data()),
                           MAX_PATH) &&
        GetVolumeInformationW((LPCWSTR)path.data(),
                              NULL,
                              0,
                              NULL,
                              NULL,
                              NULL,
                              (LPWSTR)fsname.data(),
                              MAX_PATH))
    {
        if (Utils::startswith(fsname, VBoxSharedFolderFS))
        {
            LOG_warn << "VBoxSharedFolderFS is not supported because it doesn't provide ReadDirectoryChanges() nor unique file identifiers";
            syncError = VBOXSHAREDFOLDER_UNSUPPORTED;
            result = false;
        }
        else if ((Utils::startswith(fsname, L"FAT") ||
                  Utils::startswith(fsname, L"exFAT"))) // TODO: have these checks for !windows too
        {
            LOG_warn << "You are syncing a local folder formatted with a FAT filesystem. "
                        "That filesystem has deficiencies managing big files and modification times "
                        "that can cause synchronization problems (e.g. when daylight saving changes), "
                        "so it's strongly recommended that you only sync folders formatted with more "
                        "reliable filesystems like NTFS (more information at https://help.mega.nz/megasync/syncing.html#can-i-sync-fat-fat32-partitions-under-windows.";
            syncWarning = LOCAL_IS_FAT;
        }
        else if (Utils::startswith(fsname, L"HGFS"))
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
    LocalPath::local2path(&fsname, &utf8fsname, false);
    LOG_debug << "Filesystem type: " << utf8fsname;

    return result;
}

bool reuseFingerprint(const FSNode& lhs, const FSNode& rhs)
{
    // it might look like fingerprint.crc comparison has been missed out here
    // but that is intentional.  The point is to avoid re-fingerprinting files
    // when we rescan a folder, if we already have good info about them and
    // nothing we can see from outside the file had changed,
    // mtime is the same, and no notifications arrived
    // for this particular file.
    return lhs.type == rhs.type
        && lhs.fsid == rhs.fsid
        && lhs.fingerprint.mtime == rhs.fingerprint.mtime
        && lhs.fingerprint.size == rhs.fingerprint.size;
};

bool  WinFileSystemAccess::checkForSymlink(const LocalPath& lp)
{
    ScopedFileHandle rightTypeHandle =
        CreateFileW(lp.asPlatformEncoded(false).c_str(),
                    GENERIC_READ,
                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                    NULL,
                    OPEN_EXISTING,
                    FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS,
                    NULL);

    typedef struct _REPARSE_DATA_BUFFER {
        ULONG  ReparseTag;
        USHORT ReparseDataLength;
        USHORT Reserved;
        union {
            struct {
                USHORT SubstituteNameOffset;
                USHORT SubstituteNameLength;
                USHORT PrintNameOffset;
                USHORT PrintNameLength;
                ULONG  Flags;
                WCHAR  PathBuffer[1];
            } SymbolicLinkReparseBuffer;
            struct {
                USHORT SubstituteNameOffset;
                USHORT SubstituteNameLength;
                USHORT PrintNameOffset;
                USHORT PrintNameLength;
                WCHAR  PathBuffer[1];
            } MountPointReparseBuffer;
            struct {
                UCHAR DataBuffer[1];
            } GenericReparseBuffer;
        } DUMMYUNIONNAME;
    } REPARSE_DATA_BUFFER, *PREPARSE_DATA_BUFFER;

    union rightSizeBuffer {
        REPARSE_DATA_BUFFER rdb;
        char pad[MAXIMUM_REPARSE_DATA_BUFFER_SIZE];
    } rdb;

    DWORD bytesReturned = 0;

    if (rightTypeHandle.get() != INVALID_HANDLE_VALUE  &&
        DeviceIoControl(
        rightTypeHandle.get(),
        FSCTL_GET_REPARSE_POINT,
        NULL,
        0,
        &rdb,
        sizeof(rdb),
        &bytesReturned,
        NULL))
    {
        return rdb.rdb.ReparseTag == IO_REPARSE_TAG_SYMLINK;
    }

    return false;
}

ScanResult WinFileSystemAccess::directoryScan(const LocalPath& path,
                                              handle expectedFsid,
                                              map<LocalPath, FSNode>& known,
                                              std::vector<FSNode>& results,
                                              [[maybe_unused]] bool followSymLinks,
                                              unsigned& nFingerprinted)
{
    assert(path.isAbsolute());
    assert(!followSymLinks && "Symlinks are not supported on Windows!");

    ScopedFileHandle rightTypeHandle =
        CreateFileW(path.asPlatformEncoded(false).c_str(),
                    GENERIC_READ,
                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                    NULL,
                    OPEN_EXISTING,
                    FILE_FLAG_BACKUP_SEMANTICS,
                    NULL);

    if (rightTypeHandle.get() == INVALID_HANDLE_VALUE)
    {
        LOG_warn << "Failed to directoryScan, no handle for: " << path;
        return SCAN_INACCESSIBLE;
    }

    BY_HANDLE_FILE_INFORMATION bhfi = { 0 };
    if (!GetFileInformationByHandle(rightTypeHandle.get(), &bhfi))
    {
        LOG_warn << "Failed to directoryScan, no info for: " << path;
        return SCAN_INACCESSIBLE;
    }

    auto folderFsid = ((handle)bhfi.nFileIndexHigh << 32) | (handle)bhfi.nFileIndexLow;
    if (folderFsid != expectedFsid)
    {
        LOG_warn << "Failed to directoryScan, mismatch on expected FSID: " << path;
        return SCAN_FSID_MISMATCH;
    }

    if (!(bhfi.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
    {
        LOG_warn << "Failed to directoryScan, not a directory: " << path;
        return SCAN_INACCESSIBLE;
    }

    alignas(8) byte bytes[1024 * 10];

    if (GetFileInformationByHandleEx( rightTypeHandle.get(),
        FileIdBothDirectoryRestartInfo,  // starts the listing from the beginning
        bytes, sizeof(bytes)))
    {
        do
        {
            bool loopDone = false;
            for (byte* ptr = bytes; !loopDone;)
            {
                auto info = reinterpret_cast<FILE_ID_BOTH_DIR_INFO*>(ptr);
                if (!info->NextEntryOffset) loopDone = true;
                else ptr += info->NextEntryOffset;

                FSNode result;
                result.localname = LocalPath::fromPlatformEncodedRelative(wstring(info->FileName, info->FileNameLength/2));
                std::wstring localNameStr = result.localname.asPlatformEncoded(false);
                assert(localNameStr.back() != 0);

                if (localNameStr == L"." || localNameStr == L"..")
                {
                    continue;
                }

                // Are we dealing with a reparse point?
                if ((info->FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
                {
                    auto filePath = path;

                    filePath.appendWithSeparator(result.localname, false);

                    LOG_warn << "directoryScan: "
                             << "Encountered a reparse point: "
                             << filePath;

                    // Provide basic information about the reparse point.
                    result.fingerprint.mtime = FileTime_to_POSIX((FILETIME*)&info->LastWriteTime);
                    result.fingerprint.size = (m_off_t)info->EndOfFile.QuadPart;
                    result.fsid = (handle)info->FileId.QuadPart;
                    result.isSymlink = checkForSymlink(filePath);
                    result.type = result.isSymlink ? TYPE_SYMLINK : TYPE_SPECIAL;


                    results.emplace_back(std::move(result));

                    // Process the next directory entry.
                    continue;
                }

                // For now at least, do the same as the old system: ignore system+hidden.
                // desktop.ini seem to be at least one problem solved this way, they are
                // (at least sometimes) unopenable
                // anyway, so no valid fingerprint can be extracted.
                if (WinFileAccess::skipattributes(info->FileAttributes))
                {
                    result.type = TYPE_DONOTSYNC;
                    LOG_debug << "do-not-sync path identified by attributes: " << path;
                }
                else
                {
                    result.type = (info->FileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? FOLDERNODE : FILENODE;
                }
                result.fsid = (handle) info->FileId.QuadPart;

                result.fingerprint.mtime = FileTime_to_POSIX((FILETIME*)&info->LastWriteTime);
                result.fingerprint.size = (m_off_t)info->EndOfFile.QuadPart;

                if (info->ShortNameLength > 0)
                {
                    wstring wstr(info->ShortName, info->ShortNameLength/2);
                    assert(wstr.back() != 0);
                    if (wstr != result.localname.asPlatformEncoded(false))
                    {
                        result.shortname.reset(new LocalPath(LocalPath::fromPlatformEncodedRelative(std::move(wstr))));
                    }
                }

                if (result.shortname &&
                    *result.shortname == result.localname)
                {
                    result.shortname.reset();
                }

                    //// Warn about symlinks.
                    //if (result.isSymlink)
                    //{
                    //    LOG_debug << "Interrogated path is a symlink: "
                    //        << path.toPath();
                    //}

                if (result.type == FOLDERNODE)
                {
                    memset(result.fingerprint.crc.data(), 0, sizeof(result.fingerprint.crc));
                }
                else if (result.type == FILENODE)
                {
                    // Fingerprint the file if it's new or changed
                    // (caller has to not supply 'known' items we aready know changed in case mtime+size is still a match)
                    auto it = known.find(result.localname);
                    if (it != known.end() && reuseFingerprint(it->second, result))
                    {
                        result.fingerprint = std::move(it->second.fingerprint);
                        known.erase(it);
                    }
                    else
                    {
                        LocalPath p = path;
                        p.appendWithSeparator(result.localname, false);
                        auto fa = newfileaccess();
                        if (fa->fopen(p, true, false, FSLogging::logOnError))
                        {
                            result.fingerprint.genfingerprint(fa.get());
                            nFingerprinted += 1;
                        }
                        else
                        {
                            // The file may be opened exclusively by another process
                            // In this case, the fingerprint (the crc portion) is invalid (for now)
                        }
                    }
                }

                results.push_back(std::move(result));
            }
        }
        while (GetFileInformationByHandleEx( rightTypeHandle.get(),
            FileIdBothDirectoryInfo,  // continues but does not restart
            bytes, sizeof(bytes)));

    }

    auto err = GetLastError();
    if (err != ERROR_NO_MORE_FILES)
    {
        LOG_err << "Failed in directoryScan, error " << err;
        return SCAN_INACCESSIBLE;
    }

    return SCAN_SUCCESS;
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
        std::wstring name = nameArg->asPlatformEncoded(false);
        if (!glob)
        {
            if (!name.empty() && name.back() != L'\\')
            {
                name.append(L"\\");
            }
            name.append(L"*");
        }

        hFind = FindFirstFileW(name.c_str(), &ffd);

        if (glob)
        {
            if (nameArg->getLeafnameByteIndex())
            {
                globbase = nameArg->parentPath();
            }
            else
            {
                globbase.clear();
            }
        }
    }

    ffdvalid = hFind != INVALID_HANDLE_VALUE;
    if (!ffdvalid)
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
            nameArg = LocalPath::fromPlatformEncodedRelative(
                std::wstring{ffd.cFileName, wcslen(ffd.cFileName)});

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
            if (ffdvalid && SimpleLogger::getLogLevel() >= logDebug)
            {
                if (*ffd.cFileName != '.' && (ffd.cFileName[1] && ((ffd.cFileName[1] != '.') || ffd.cFileName[2])))
                    LOG_debug << "Excluded: " << ffd.cFileName << "   Attributes: " << ffd.dwFileAttributes;
            }
        }

        ffdvalid = FindNextFileW(hFind, &ffd) != FALSE;
        if (!ffdvalid)
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

m_off_t WinFileSystemAccess::availableDiskSpace(const LocalPath& drivePath)
{
    m_off_t maximumBytes = std::numeric_limits<m_off_t>::max();
    ULARGE_INTEGER numBytes;

    if (!GetDiskFreeSpaceExW(drivePath.asPlatformEncoded(false).c_str(),
                             &numBytes,
                             nullptr,
                             nullptr))
    {
        auto result = GetLastError();

        LOG_warn << "Unable to retrieve available disk space for: "
                 << drivePath
                 << ". Error code was: "
                 << result;

         return maximumBytes;
    }

    if (numBytes.QuadPart >= (uint64_t)maximumBytes)
        return maximumBytes;

    return (m_off_t)numBytes.QuadPart;
}

std::string FileSystemAccess::getErrorMessage(int error)
{
    return winErrorMessage(error);
}

bool FSLogging::isFileNotFound(int error)
{
    return error == ERROR_FILE_NOT_FOUND;
}

} // namespace
