/**
 * @file filesystem.cpp
 * @brief Generic host filesystem access interfaces
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
#include <cctype>

#include "mega/filesystem.h"
#include "mega/node.h"
#include "mega/megaclient.h"
#include "mega/logging.h"
#include "mega/mega_utf8proc.h"
#include "mega/sync.h"

#include "megafs.h"

#include <cassert>

#ifdef TARGET_OS_MAC
#include "mega/osx/osxutils.h"
#endif

namespace mega {

CodeCounter::ScopeStats g_compareUtfTimings("compareUtfTimings");

namespace detail {

const int escapeChar = '%';

template<typename CharT>
int decodeEscape(UnicodeCodepointIterator<CharT>& it)
{
    // only call when we already consumed an escapeChar.
    auto tmpit = it;
    auto c1 = tmpit.get();
    auto c2 = tmpit.get();
    if (islchex_high(c1) && islchex_low(c2))
    {
        it = tmpit;
        return hexval(c1) << 4 | hexval(c2);
    }
    else
        return -1;
}

int identity(const int c)
{
    return c;
}

#ifdef _WIN32

template<typename CharT>
UnicodeCodepointIterator<CharT> skipPrefix(const UnicodeCodepointIterator<CharT>& it)
{
    auto i = it;

    // Match leading \\.
    if (!(i.match('\\') && i.match('\\')))
    {
        return it;
    }

    // Match . or ?
    switch (i.peek())
    {
    case '.':
    case '?':
        (void)i.get();
        break;
    default:
        return it;
    }

    // Match \.
    if (!i.match('\\'))
    {
        return it;
    }

    auto j = i;

    // Match drive letter.
    if (j.get() && j.match(':'))
    {
        return i;
    }

    return it;
}

#endif // _WIN32

// the case when the strings are over different character types (just uses match())
template<typename CharT, typename CharU, typename UnaryOperation>
int compareUtf(UnicodeCodepointIterator<CharT> first1, bool unescaping1,
               UnicodeCodepointIterator<CharU> first2, bool unescaping2,
               UnaryOperation transform)
{
    CodeCounter::ScopeTimer rst(g_compareUtfTimings);

#ifdef _WIN32
    first1 = skipPrefix(first1);
    first2 = skipPrefix(first2);
#endif // _WIN32

    while (!(first1.end() || first2.end()))
    {
        int c1 = first1.get();

        if (c1 != escapeChar && first2.match(c1))
        {
            continue;
        }

        int c2 = first2.get();

        if (unescaping1 || unescaping2)
        {
            int c1e = -1;
            int c2e = -1;
            auto first1e = first1;
            auto first2e = first2;

            if (unescaping1 && c1 == escapeChar)
            {
                c1e = decodeEscape(first1e);
            }
            if (unescaping2 && c2 == escapeChar)
            {
                c2e = decodeEscape(first2e);
            }

            // so we have preferred to consume the escape if it's a match (even if there is a match before considering escapes)
            if (c1e != -1 && c2e != -1)
            {
                if (transform(c1e) == transform(c2e))
                {
                    first1 = first1e;
                    first2 = first2e;
                    c1 = c1e;
                    c2 = c2e;
                }
            }
            else if (c1e != -1)
            {
                if (transform(c1e) == transform(c2) ||
                    transform(c1) != transform(c2))
                {
                    // even if it's not a match, still consume the escape if the other is not a match, for sorting purposes
                    first1 = first1e;
                    c1 = c1e;
                }
            }
            else if (c2e != -1)
            {
                if (transform(c2e) == transform(c1) ||
                    transform(c2) != transform(c1))
                {
                    // even if it's not a match, still consume the escape if the other is not a match, for sorting purposes
                    first2 = first2e;
                    c2 = c2e;
                }
            }
        }

        if (c1 != c2)
        {
            c1 = transform(c1);
            c2 = transform(c2);

            if (c1 != c2)
            {
                return c1 - c2;
            }
        }
    }

    if (first1.end() && first2.end())
    {
        return 0;
    }

    if (first1.end())
    {
        return -1;
    }

    return 1;
}

} // detail


int compareUtf(const string& s1, bool unescaping1, const string& s2, bool unescaping2, bool caseInsensitive)
{
    return detail::compareUtf(
                unicodeCodepointIterator(s1), unescaping1,
                unicodeCodepointIterator(s2), unescaping2,
                caseInsensitive ? Utils::toUpper: detail::identity);
}

int compareUtf(const string& s1, bool unescaping1, const LocalPath& s2, bool unescaping2, bool caseInsensitive)
{
    return detail::compareUtf(
        unicodeCodepointIterator(s1), unescaping1,
        unicodeCodepointIterator(s2.localpath), unescaping2,
        caseInsensitive ? Utils::toUpper: detail::identity);
}

int compareUtf(const LocalPath& s1, bool unescaping1, const string& s2, bool unescaping2, bool caseInsensitive)
{
    return detail::compareUtf(
        unicodeCodepointIterator(s1.localpath), unescaping1,
        unicodeCodepointIterator(s2), unescaping2,
        caseInsensitive ? Utils::toUpper: detail::identity);
}

int compareUtf(const LocalPath& s1, bool unescaping1, const LocalPath& s2, bool unescaping2, bool caseInsensitive)
{
    return detail::compareUtf(
        unicodeCodepointIterator(s1.localpath), unescaping1,
        unicodeCodepointIterator(s2.localpath), unescaping2,
        caseInsensitive ? Utils::toUpper: detail::identity);
}

bool isCaseInsensitive(const FileSystemType type)
{
    if    (type == FS_EXFAT
        || type == FS_FAT32
        || type == FS_NTFS
        || type == FS_UNKNOWN)
    {
        return true;
    }
#ifdef WIN32
    return true;
#else
    return false;
#endif
}


bool IsContainingPathOf(const string& a, const char* b, size_t bLength, char sep)
{
    // a's longer than b so a can't contain b.
    if (bLength < a.size()) return false;

    // b's longer than a so there should be a separator.
    if (bLength > a.size() && b[a.size()] != sep) return false;

    // a and b must share a common prefix.
    return !a.compare(0, a.size(), b, a.size());
}


bool IsContainingCloudPathOf(const string& a, const string& b)
{
    return IsContainingPathOf(a, b.c_str(), b.size(), '/');
}

bool IsContainingCloudPathOf(const string& a, const char* b, size_t bLength)
{
    return IsContainingPathOf(a, b, bLength, '/');
}

bool IsContainingLocalPathOf(const string& a, const string& b)
{
#ifdef _WIN32
    return IsContainingPathOf(a, b.c_str(), b.size(), '\\');
#else
    return IsContainingPathOf(a, b.c_str(), b.size(), '/');
#endif
}

bool IsContainingLocalPathOf(const string& a, const char* b, size_t bLength)
{
#ifdef _WIN32
    return IsContainingPathOf(a, b, bLength, '\\');
#else
    return IsContainingPathOf(a, b, bLength, '/');
#endif
}

// TODO: may or may not be needed
void LocalPath::removeTrailingSeparators()
{
    assert(invariant());

    // Remove trailing separator if present.
    while (localpath.size() > 1 &&
           localpath.back() == localPathSeparator)
    {
        localpath.pop_back();
    }

    assert(invariant());
}

void LocalPath::normalizeAbsolute()
{
    assert(!localpath.empty());

#ifdef USE_IOS
    // iOS is a tricky case.
    // We need to be able to use and persist absolute paths.  however on iOS app restart,
    // our app base path may be different.  So, we only record the path beyond that app
    // base path.   That is what the app supplies for absolute paths, that's what is persisted.
    // Actual filesystem functions passed such an "absolute" path will prepend the app base path
    // unless it already started with /
    // and that's how it worked before we added the "absolute" feature to LocalPath.
    // As a result of that though, there's nothing to adjust or check here for iOS.
    isFromRoot = true;

    // In addition, for iOS, should the app try to use ".", or "./", we interpret that to mean
    // that really it's relative to the app base path.  So we convert:
    if (!localpath.empty() && localpath.front() == '.')
    {
        if (localpath.size() == 1 || localpath[1] == localPathSeparator)
        {
            localpath.erase(0, 1);
            while (!localpath.empty() &&
                    localpath.front() == localPathSeparator)
            {
                localpath.erase(0, 1);
            }
        }
    }

#elif WIN32

    // Add a drive separator if necessary.
    // append \ to bare Windows drive letter paths
    // GetFullPathNameW does all of this for windows.
    // The documentation says to prepend \\?\ to deal with long names, but it makes the function fail
    // it seems to work with long names anyway.

    // We also convert to absolute if it isn't already, which GetFullPathNameW does also.
    // So that when working with LocalPath, we always have the full path.
    // Historically, relative paths can come into the system, this will convert them.

    if (PathIsRelativeW(localpath.c_str()))
    {
        // ms: In the ANSI version of this function, the name is limited to MAX_PATH characters.
        // ms: To extend this limit to 32,767 wide characters, call the Unicode version of the function (GetFullPathNameW), and prepend "\\?\" to the path
        WCHAR buffer[32768];
        DWORD stringLen = GetFullPathNameW(localpath.c_str(), 32768, buffer, NULL);
        assert(stringLen < 32768);

        localpath = wstring(buffer, stringLen);
    }

    isFromRoot = true;

    // See https://docs.microsoft.com/en-us/dotnet/standard/io/file-path-formats
    // Also https://docs.microsoft.com/en-us/windows/win32/fileio/maximum-file-path-limitation
    // Basically, \\?\ is the magic prefix that means "don't mess with the path I gave you",
    // and lets us access otherwise inaccessible files (trailing ' ', '.', very long names, etc).
    // "Unless the path starts exactly with \\?\ (note the use of the canonical backslash), it is normalized."

    // TODO:  add long-path-aware manifest? (see 2nd link)


    if (localpath.substr(0,2) == L"\\\\")
    {
        // The caller already passed in a path that should be precise either with \\?\ or \\.\ or \\<server> etc.
        // Let's trust they know what they are doing and leave the path alone
    }
    else
    {
        localpath.insert(0, L"\\\\?\\");
    }

#else
    // convert to absolute if it isn't already
    if (!localpath.empty() && localpath[0] != localPathSeparator)
    {
        LocalPath lp;
        PosixFileSystemAccess::cwd_static(lp);
        lp.appendWithSeparator(*this, false);
        localpath = move(lp.localpath);
    }
    isFromRoot = true;
#endif

    assert(invariant());
}

bool LocalPath::invariant() const
{
#ifdef USE_IOS
    // iOS is a tricky case.
    // We need to be able to use and persist absolute paths.  however on iOS app restart,
    // our app base path may be different.  So, we only record the path beyond that app
    // base path.   That is what the app supplies for absolute paths, that's what is persisted.
    // Actual filesystem functions passed such an "absolute" path will prepend the app base path
    // unless it already started with /
    // and that's how it worked before we added the "absolute" feature to LocalPath.
    // As a result of that though, there's nothing to adjust or check here for iOS.
#elif WIN32
    if (isFromRoot)
    {
        // if it starts with \\ then it's absolute, either by us or provided
        if (localpath.size() >= 2 && localpath[0] == '\\' && localpath[1] == '\\') return true;
        // otherwise it must contain a drive letter
        if (localpath.find(L":") == string_type::npos) return false;
        // ok so probably relative then, but double check:
        if (PathIsRelativeW(localpath.c_str())) return false;
    }
    else
    {
        // must not contain a drive letter
        if (localpath.find(L":") != string_type::npos) return false;
        // must not start "\\"
        if (localpath.size() >= 2 &&
            localpath.substr(0, 2) == L"\\\\") return false;
     }
#else
    if (isFromRoot)
    {
        // must start /
        if (localpath.size() < 1) return false;
        if (localpath.front() != localPathSeparator) return false;
    }
    else
    {
        // this could contain /relative for appending etc.
    }
#endif
    return true;
}

FileSystemAccess::FileSystemAccess()
#ifdef ENABLE_SYNC
    : notifyerr(false)
    , notifyfailed(false)
#endif
{
}

void FileSystemAccess::captimestamp(m_time_t* t)
{
    // FIXME: remove upper bound before the year 2100 and upgrade server-side timestamps to BIGINT
    if (*t > (uint32_t)-1) *t = (uint32_t)-1;
    else if (*t < 0) *t = 0;
}

bool FileSystemAccess::decodeEscape(const char* s, char& escapedChar) const
{
    // s must be part of a null terminated c-style string
    if (s && *s == '%'
        && islchex_high(s[1]) // must be 0..127
        && islchex_low(s[2]))
    {
        escapedChar = char((hexval(s[1]) << 4) | hexval(s[2]));
        return true;
    }
    return false;
}


const char *FileSystemAccess::fstypetostring(FileSystemType type) const
{
    switch (type)
    {
        case FS_NTFS:
            return "NTFS";
        case FS_EXFAT:
            return "EXFAT";
        case FS_FAT32:
            return "FAT32";
        case FS_EXT:
            return "EXT";
        case FS_HFS:
            return "HFS";
        case FS_APFS:
            return "APFS";
        case FS_FUSE:
            return "FUSE";
        case FS_SDCARDFS:
            return "SDCARDFS";
        case FS_F2FS:
            return "F2FS";
        case FS_XFS:
            return "XFS";
        case FS_CIFS:
            return "CIFS";
        case FS_NFS:
            return "NFS";
        case FS_SMB:
            return "SMB";
        case FS_SMB2:
            return "SMB2";
        case FS_UNKNOWN:    // fall through
            return "UNKNOWN FS";
    }

    return "UNKNOWN FS";
}

FileSystemType FileSystemAccess::getlocalfstype(const LocalPath& path) const
{
    // Not enough information to determine path.
    if (path.empty())
    {
        return FS_UNKNOWN;
    }

    FileSystemType type;

    // Try and get the type from the path we were given.
    if (getlocalfstype(path, type))
    {
        // Path exists.
        return type;
    }

    // Try and get the type based on our parent's path.
    LocalPath parentPath(path);

    // Remove trailing separator, if any.
    parentPath.trimNonDriveTrailingSeparator();

    // Did the path consist solely of that separator?
    if (parentPath.empty())
    {
        return FS_UNKNOWN;
    }

    // Where does our name begin?
    auto index = parentPath.getLeafnameByteIndex();

    // We have a parent.
    if (index)
    {
        // Remove the current leaf name.
        parentPath.truncate(index);

        // Try and get our parent's filesystem type.
        if (getlocalfstype(parentPath, type))
        {
            return type;
        }
    }

    return FS_UNKNOWN;
}

// Group different filesystems types in families, according to its restricted charsets
bool FileSystemAccess::islocalfscompatible(unsigned char c, bool, FileSystemType) const
{
    return c >= ' ' && !strchr("\\/:?\"<>|*", c);
}

// replace characters that are not allowed in local fs names with a %xx escape sequence
void FileSystemAccess::escapefsincompatible(string* name, FileSystemType fileSystemType) const
{
    if (!name->compare(".."))
    {
        name->replace(0, 2, "%2e%2e");
        return;
    }
    if (!name->compare("."))
    {
        name->replace(0, 1, "%2e");
        return;
    }

    char buf[4];
    size_t utf8seqsize = 0;
    size_t i = 0;
    unsigned char c = '0';
    while (i < name->size())
    {
        c = static_cast<unsigned char>((*name)[i]);
        utf8seqsize = Utils::utf8SequenceSize(c);
        assert (utf8seqsize);
        if (utf8seqsize == 1 && !islocalfscompatible(c, true, fileSystemType))
        {
            sprintf(buf, "%%%02x", c);
            name->replace(i, 1, buf);
            // Logging these at such a low level is too frequent and verbose
            //LOG_debug << "Escape incompatible character for filesystem type "
            //    << fstypetostring(fileSystemType)
            //    << ", replace '" << char(c) << "' by '" << buf << "'\n";
        }
        i += utf8seqsize;
    }
}

void FileSystemAccess::unescapefsincompatible(string *name) const
{
    if (!name->compare("%2e%2e"))
    {
        name->replace(0, 6, "..");
        return;
    }

    if (!name->compare("%2e"))
    {
        name->replace(0, 3, ".");
        return;
    }

    for (size_t i = 0; i < name->size(); ++i)
    {
        char c;
        if (decodeEscape(name->c_str() + i, c) && // it must be a null terminated c-style string passed here
            !std::iscntrl(c))
        {
            // Substitute in the decoded character.
            name->replace(i, 3, 1, c);
        }
    }
}

void LocalPath::utf8_normalize(string* filename)
{
    if (!filename) return;

    const char* cfilename = filename->c_str();
    size_t fnsize = filename->size();
    string result;

    for (size_t i = 0; i < fnsize; )
    {
        // allow NUL bytes between valid UTF-8 sequences
        if (!cfilename[i])
        {
            result.append("", 1);
            i++;
            continue;
        }

        const char* substring = cfilename + i;
        char* normalized = (char*)utf8proc_NFC((uint8_t*)substring);

        if (!normalized)
        {
            filename->clear();
            return;
        }

        result.append(normalized);
        free(normalized);

        i += strlen(substring);
    }

    *filename = std::move(result);
}

std::unique_ptr<LocalPath> FileSystemAccess::fsShortname(const LocalPath& localname)
{
    LocalPath s;
    if (getsname(localname, s))
    {
        return ::mega::make_unique<LocalPath>(std::move(s));
    }
    return nullptr;
}

handle FileSystemAccess::fsidOf(const LocalPath& path, bool follow)
{
    auto fileAccess = newfileaccess(follow);

    if (fileAccess->fopen(path, true, false))
        return fileAccess->fsid;

    return UNDEF;
}

#ifdef ENABLE_SYNC

bool FileSystemAccess::initFilesystemNotificationSystem()
{
    return true;
}

#endif // ENABLE_SYNC

bool FileSystemAccess::fileExistsAt(const LocalPath& path)
{
    auto fa = newfileaccess(false);
    return fa->isfile(path);
}

#ifdef ENABLE_SYNC

// default DirNotify: no notification available
DirNotify::DirNotify(const LocalPath& clocalbasepath, const LocalPath& cignore, Sync* s)
{
    assert(!clocalbasepath.empty());
    localbasepath = clocalbasepath;
    ignore = cignore;

    mFailed = 1;
    mFailReason = "Not initialized";
    mErrorCount = 0;
    sync = s;
}


void DirNotify::setFailed(int errCode, const string& reason)
{
    std::lock_guard<std::mutex> g(mMutex);
    mFailed = errCode;
    mFailReason = reason;
}

int DirNotify::getFailed(string& reason)
{
    if (mFailed)
    {
        reason = mFailReason;
    }
    return mFailed;
}


bool DirNotify::empty()
{
    for (auto& q : notifyq)
    {
        if (!q.empty())
        {
            return false;
        }
    }

    return true;
}

// notify base LocalNode + relative path/filename
void DirNotify::notify(notifyqueue queue, LocalNode* node, LocalPath&& path, bool immediate, bool recursive)
{
    // We may be executing on a thread here so we can't access the LocalNode data structures.  Queue everything, and
    // filter when the notifications are processed.  Also, queueing it here is faster than logging the decision anyway.
    auto timestamp = immediate ? 0 : Waiter::ds;

    notifyq[queue].pushBack(Notification(timestamp, std::move(path), node, recursive));

#ifdef ENABLE_SYNC
    sync->client->syncactivity |= queue == DIREVENTS || queue == EXTRA;
#endif // ENABLE_SYNC
}

DirNotify* FileSystemAccess::newdirnotify(const LocalPath& localpath, const LocalPath& ignore, Waiter*, LocalNode* syncroot)
{
    return new DirNotify(localpath, ignore, syncroot->sync);
}

#endif  // ENABLE_SYNC

FileAccess::FileAccess(Waiter *waiter)
{
    this->waiter = waiter;
    this->isAsyncOpened = false;
    this->numAsyncReads = 0;
}

FileAccess::~FileAccess()
{
    // All AsyncIOContext objects must be deleted before
    assert(!numAsyncReads && !isAsyncOpened);
}

// open file for reading
bool FileAccess::fopen(const LocalPath& name)
{
    updatelocalname(name, true);

    return sysstat(&mtime, &size);
}

bool FileAccess::isfile(const LocalPath& path)
{
    return fopen(path) && type == FILENODE;
}

bool FileAccess::isfolder(const LocalPath& path)
{
    fopen(path);
    return type == FOLDERNODE;
}

// check if size and mtime are unchanged, then open for reading
bool FileAccess::openf()
{
    if (nonblocking_localname.empty())
    {
        // file was not opened in nonblocking mode
        return true;
    }

    m_time_t curr_mtime;
    m_off_t curr_size;
    if (!sysstat(&curr_mtime, &curr_size))
    {
        LOG_warn << "Error opening sync file handle (sysstat) "
                 << curr_mtime << " - " << mtime
                 << curr_size  << " - " << size;
        return false;
    }

    if (curr_mtime != mtime || curr_size != size)
    {
        mtime = curr_mtime;
        size = curr_size;
        retry = false;
        return false;
    }

    return sysopen();
}

void FileAccess::closef()
{
    if (!nonblocking_localname.empty())
    {
        sysclose();
    }
}

void FileAccess::asyncopfinished(void *param)
{
    Waiter *waiter = (Waiter *)param;
    if (waiter)
    {
        waiter->notify();
    }
}

AsyncIOContext *FileAccess::asyncfopen(const LocalPath& f)
{
    updatelocalname(f, true);

    LOG_verbose << "Async open start";
    AsyncIOContext *context = newasynccontext();
    context->op = AsyncIOContext::OPEN;
    context->access = AsyncIOContext::ACCESS_READ;
    context->openPath = f;
    context->waiter = waiter;
    context->userCallback = asyncopfinished;
    context->userData = waiter;
    context->posOfBuffer = size;
    context->fa = this;

    context->failed = !sysstat(&mtime, &size);
    context->retry = this->retry;
    context->finished = true;
    context->userCallback(context->userData);
    return context;
}

bool FileAccess::asyncopenf()
{
    numAsyncReads++;
    if (nonblocking_localname.empty())
    {
        return true;
    }

    if (isAsyncOpened)
    {
        return true;
    }

    m_time_t curr_mtime = 0;
    m_off_t curr_size = 0;
    if (!sysstat(&curr_mtime, &curr_size))
    {
        LOG_warn << "Error opening async file handle (sysstat) "
                 << curr_mtime << " - " << mtime
                 << curr_size  << " - " << size;
        return false;
    }

    if (curr_mtime != mtime || curr_size != size)
    {
        mtime = curr_mtime;
        size = curr_size;
        retry = false;
        return false;
    }

    LOG_debug << "Opening async file handle for reading";
    bool result = sysopen(true);
    if (result)
    {
        isAsyncOpened = true;
    }
    else
    {
        LOG_warn << "Error opening async file handle (sysopen)";
    }
    return result;
}

void FileAccess::asyncclosef()
{
    numAsyncReads--;
    if (isAsyncOpened && !numAsyncReads)
    {
        LOG_debug << "Closing async file handle";
        isAsyncOpened = false;
        sysclose();
    }
}

AsyncIOContext *FileAccess::asyncfopen(const LocalPath& f, bool read, bool write, m_off_t pos)
{
    LOG_verbose << "Async open start";
    AsyncIOContext *context = newasynccontext();
    context->op = AsyncIOContext::OPEN;
    context->access = AsyncIOContext::ACCESS_NONE
            | (read ? AsyncIOContext::ACCESS_READ : 0)
            | (write ? AsyncIOContext::ACCESS_WRITE : 0);

    context->openPath = f;
    context->waiter = waiter;
    context->userCallback = asyncopfinished;
    context->userData = waiter;
    context->posOfBuffer = pos;
    context->fa = this;

    asyncsysopen(context);
    return context;
}

void FileAccess::asyncsysopen(AsyncIOContext *context)
{
    context->failed = true;
    context->retry = false;
    context->finished = true;
    if (context->userCallback)
    {
        context->userCallback(context->userData);
    }
}

AsyncIOContext *FileAccess::asyncfread(string *dst, unsigned len, unsigned pad, m_off_t pos)
{
    LOG_verbose << "Async read start";
    dst->resize(len + pad);

    AsyncIOContext *context = newasynccontext();
    context->op = AsyncIOContext::READ;
    context->posOfBuffer = pos;
    context->pad = pad;
    context->dataBuffer = (byte*)dst->data();
    context->dataBufferLen = len;
    context->waiter = waiter;
    context->userCallback = asyncopfinished;
    context->userData = waiter;
    context->fa = this;

    if (!asyncopenf())
    {
        LOG_err << "Error in asyncopenf";
        context->failed = true;
        context->retry = this->retry;
        context->finished = true;
        context->userCallback(context->userData);
        return context;
    }

    asyncsysread(context);
    return context;
}

void FileAccess::asyncsysread(AsyncIOContext *context)
{
    context->failed = true;
    context->retry = false;
    context->finished = true;
    if (context->userCallback)
    {
        context->userCallback(context->userData);
    }
}

AsyncIOContext *FileAccess::asyncfwrite(const byte* data, unsigned len, m_off_t pos)
{
    LOG_verbose << "Async write start";

    AsyncIOContext *context = newasynccontext();
    context->op = AsyncIOContext::WRITE;
    context->posOfBuffer = pos;
    context->dataBufferLen = len;
    context->dataBuffer = const_cast<byte*>(data);
    context->waiter = waiter;
    context->userCallback = asyncopfinished;
    context->userData = waiter;
    context->fa = this;

    asyncsyswrite(context);
    return context;
}

void FileAccess::asyncsyswrite(AsyncIOContext *context)
{
    context->failed = true;
    context->retry = false;
    context->finished = true;
    if (context->userCallback)
    {
        context->userCallback(context->userData);
    }
}

AsyncIOContext *FileAccess::newasynccontext()
{
    return new AsyncIOContext();
}

bool FileAccess::fread(string* dst, unsigned len, unsigned pad, m_off_t pos)
{
    if (!openf())
    {
        return false;
    }

    bool r;

    dst->resize(len + pad);

    if ((r = sysread((byte*)dst->data(), len, pos)))
    {
        memset((char*)dst->data() + len, 0, pad);
    }

    closef();

    return r;
}

bool FileAccess::frawread(byte* dst, unsigned len, m_off_t pos, bool caller_opened)
{
    if (!caller_opened && !openf())
    {
        return false;
    }

    bool r = sysread(dst, len, pos);

    if (!caller_opened)
    {
        closef();
    }

    return r;
}

AsyncIOContext::~AsyncIOContext()
{
    finish();

    // AsyncIOContext objects must be deleted before the FileAccess object
    if (op == AsyncIOContext::READ)
    {
        fa->asyncclosef();
    }
}

void AsyncIOContext::finish()
{
    if (!finished)
    {
        while (!finished)
        {
            waiter->init(NEVER);
            waiter->wait();
        }

        // We could have been consumed and external event
        waiter->notify();
    }
}

FileInputStream::FileInputStream(FileAccess *fileAccess)
{
    this->fileAccess = fileAccess;
    this->offset = 0;
}

m_off_t FileInputStream::size()
{
    return fileAccess->size;
}

bool FileInputStream::read(byte *buffer, unsigned size)
{
    if (!buffer)
    {
        if ((offset + size) <= fileAccess->size)
        {
            offset += size;
            return true;
        }

        LOG_warn << "Invalid seek on FileInputStream";
        return false;
    }

    if (fileAccess->frawread(buffer, size, offset, true))
    {
        offset += size;
        return true;
    }

    LOG_warn << "Invalid read on FileInputStream";
    return false;
}

bool LocalPath::empty() const
{
    assert(invariant());
    return localpath.empty();
}

void LocalPath::clear()
{
    assert(invariant());
    localpath.clear();
    isFromRoot = false;
    assert(invariant());
}

void LocalPath::truncate(size_t bytePos)
{
    assert(invariant());
    localpath.resize(bytePos);
    assert(invariant());
}

LocalPath LocalPath::leafName() const
{
    assert(invariant());

    auto p = localpath.find_last_of(localPathSeparator);
    p = p == string::npos ? 0 : p + 1;
    LocalPath result;
    result.localpath = localpath.substr(p, localpath.size() - p);
    assert(result.invariant());
    return result;
}

void LocalPath::append(const LocalPath& additionalPath)
{
    assert(invariant());
    localpath.append(additionalPath.localpath);
    assert(invariant());
}

std::string LocalPath::platformEncoded() const
{
    assert(invariant());
#ifdef WIN32
    // this function is typically used where we need to pass a file path to the client app, which expects utf16 in a std::string buffer
    // some other backwards compatible cases need this format also, eg. serialization
    std::string outstr;

    if (localpath.size() >= 4 && 0 == localpath.compare(0, 4, L"\\\\?\\", 4))
    {
        // when a path leaves LocalPath, we can remove prefix which is only needed internally
        outstr.resize((localpath.size() - 4) * sizeof(wchar_t));
        memcpy(const_cast<char*>(outstr.data()), localpath.data() + 4, (localpath.size() - 4) * sizeof(wchar_t));
    }
    else
    {
        outstr.resize(localpath.size() * sizeof(wchar_t));
        memcpy(const_cast<char*>(outstr.data()), localpath.data(), localpath.size() * sizeof(wchar_t));
    }
    return outstr;
#else
    // for non-windows, it's just the same utf8 string we use anyway
    return localpath;
#endif
}


void LocalPath::appendWithSeparator(const LocalPath& additionalPath, bool separatorAlways)
{
#ifdef USE_IOS
    bool originallyUsesAppBasePath = isFromRoot &&
        (localpath.empty() || localpath.front() != localPathSeparator);
#endif

    assert(!additionalPath.isFromRoot);
    if (separatorAlways || localpath.size())
    {
        // still have to be careful about appending a \ to F:\ for example, on windows, which produces an invalid path
        if (!(endsInSeparator() || additionalPath.beginsWithSeparator()))
        {
            localpath.append(1, localPathSeparator);
        }
    }

    localpath.append(additionalPath.localpath);

#ifdef USE_IOS
    if (originallyUsesAppBasePath)
    {
        while (!localpath.empty() && localpath.front() == localPathSeparator)
        {
            localpath.erase(0, 1);
        }
    }
#endif

    assert(invariant());
}

void LocalPath::prependWithSeparator(const LocalPath& additionalPath)
{
    assert(!isFromRoot);
    assert(invariant());
    // no additional separator if there is already one after
    if (!localpath.empty() && localpath[0] != localPathSeparator)
    {
        // no additional separator if there is already one before
        if (!(beginsWithSeparator() || additionalPath.endsInSeparator()))
        {
            localpath.insert(0, 1, localPathSeparator);
        }
    }
    localpath.insert(0, additionalPath.localpath);
    isFromRoot = additionalPath.isFromRoot;
    assert(invariant());
}

LocalPath LocalPath::prependNewWithSeparator(const LocalPath& additionalPath) const
{
    assert(!isFromRoot);
    LocalPath lp = *this;
    lp.prependWithSeparator(additionalPath);
    assert(lp.invariant());
    return lp;
}

void LocalPath::trimNonDriveTrailingSeparator()
{
    assert(invariant());
    if (endsInSeparator())
    {
        // ok so the last character is a directory separator.  But don't remove it for eg. F:\ on windows
        #ifdef WIN32
        if (localpath.size() > 1 &&
            localpath[localpath.size() - 2] == L':')
        {
            return;
        }
        #endif

        localpath.resize(localpath.size() - 1);
    }
    assert(invariant());
}

bool LocalPath::findNextSeparator(size_t& separatorBytePos) const
{
    assert(invariant());
    separatorBytePos = localpath.find(localPathSeparator, separatorBytePos);
    return separatorBytePos != string::npos;
}

bool LocalPath::findPrevSeparator(size_t& separatorBytePos, const FileSystemAccess& fsaccess) const
{
    assert(invariant());
    separatorBytePos = localpath.rfind(LocalPath::localPathSeparator, separatorBytePos);
    return separatorBytePos != string::npos;
}

bool LocalPath::endsInSeparator() const
{
    assert(invariant());
    return !localpath.empty() && localpath.back() == localPathSeparator;
}

bool LocalPath::beginsWithSeparator() const
{
    assert(invariant());
    return !localpath.empty() && localpath.front() == localPathSeparator;
}

size_t LocalPath::getLeafnameByteIndex() const
{
    assert(invariant());
    size_t p = localpath.size();

    while (p && (p -= 1))
    {
        if (localpath[p] == LocalPath::localPathSeparator)
        {
            p += 1;
            break;
        }
    }
    return p;
}

bool LocalPath::backEqual(size_t bytePos, const LocalPath& compareTo) const
{
    auto n = compareTo.localpath.size();
    return bytePos + n == localpath.size() && !localpath.compare(bytePos, n, compareTo.localpath);
}

LocalPath LocalPath::subpathFrom(size_t bytePos) const
{
    assert(invariant());
    LocalPath result;
    result.localpath = localpath.substr(bytePos);
    assert(result.invariant());
    return result;
}

LocalPath LocalPath::subpathTo(size_t bytePos) const
{
    assert(invariant());
    LocalPath p;
    p.localpath = localpath.substr(0, bytePos);
    p.isFromRoot = isFromRoot;
    assert(p.invariant());
    return p;
}

LocalPath LocalPath::parentPath() const
{
    assert(invariant());
    return subpathTo(getLeafnameByteIndex());
}

LocalPath LocalPath::insertFilenameCounter(unsigned counter) const
{
    assert(invariant());

    size_t dotindex = localpath.find_last_of('.');
    size_t sepindex = localpath.find_last_of(LocalPath::localPathSeparator);

    LocalPath result, extension;

    if (dotindex == string::npos || (sepindex != string::npos && sepindex > dotindex))
    {
        result.localpath = localpath;
        result.isFromRoot = isFromRoot;
    }
    else
    {
        result.localpath = localpath.substr(0, dotindex);
        result.isFromRoot = isFromRoot;
        extension.localpath = localpath.substr(dotindex);
    }

    ostringstream oss;
    oss << " (" << counter << ")";

    result.localpath += LocalPath::fromRelativePath(oss.str()).localpath + extension.localpath;
    assert(result.invariant());
    return result;
}


string LocalPath::toPath() const
{
    assert(invariant());
    string path;
    local2path(&localpath, &path);

#ifdef WIN32
    if (path.size() >= 4 && 0 == path.compare(0, 4, "\\\\?\\", 4))
    {
        // when a path leaves LocalPath, we can remove prefix which is only needed internally
        path.erase(0, 4);
    }
#endif

    return path;
}

string LocalPath::toName(const FileSystemAccess& fsaccess) const
{
    string name = toPath();
    fsaccess.unescapefsincompatible(&name);
    return name;
}

LocalPath LocalPath::fromAbsolutePath(const string& path)
{
    assert(!path.empty());
    LocalPath p;
    path2local(&path, &p.localpath);
    p.normalizeAbsolute();
    return p;
}

LocalPath LocalPath::fromRelativePath(const string& path)
{
    LocalPath p;
    path2local(&path, &p.localpath);
    assert(p.invariant());
    return p;
}

LocalPath LocalPath::fromRelativeName(string path, const FileSystemAccess& fsaccess, FileSystemType fsType)
{
    fsaccess.escapefsincompatible(&path, fsType);
    return fromRelativePath(path);
}

LocalPath LocalPath::fromPlatformEncodedRelative(string path)
{
    LocalPath p;
#if defined(_WIN32)
    assert(!(path.size() % 2));
    p.localpath.resize(path.size() / sizeof(wchar_t));
    memcpy(const_cast<wchar_t*>(p.localpath.data()), path.data(), p.localpath.size() * sizeof(wchar_t));
#else
    p.localpath = std::move(path);
#endif
    assert(p.invariant());
    return p;
}

LocalPath LocalPath::fromPlatformEncodedAbsolute(string path)
{
    LocalPath p;
#if defined(_WIN32)
    assert(!(path.size() % 2));
    p.localpath.resize(path.size() / sizeof(wchar_t));
    memcpy(const_cast<wchar_t*>(p.localpath.data()), path.data(), p.localpath.size() * sizeof(wchar_t));
#else
    p.localpath = std::move(path);
#endif
    p.normalizeAbsolute();
    return p;
}


#if defined(_WIN32)
LocalPath LocalPath::fromPlatformEncodedRelative(wstring&& wpath)
{
    LocalPath p;
    p.localpath = std::move(wpath);
    assert(p.invariant());
    return p;
}

LocalPath LocalPath::fromPlatformEncodedAbsolute(wstring&& wpath)
{
    LocalPath p;
    p.localpath = std::move(wpath);
    p.normalizeAbsolute();
    return p;
}

wchar_t LocalPath::driveLetter()
{
    assert(isFromRoot);
    assert(invariant());
    auto drivepos = localpath.find(L':');
    return drivepos == wstring::npos || drivepos < 1 ? 0 : localpath[drivepos-1];
}


// convert UTF-8 to Windows Unicode
void LocalPath::path2local(const string* path, string* local)
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
void LocalPath::path2local(const string* path, std::wstring* local)
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
void LocalPath::local2path(const string* local, string* path)
{
    path->resize((local->size() + 1) * 4 / sizeof(wchar_t) + 1);

    path->resize(WideCharToMultiByte(CP_UTF8, 0, (wchar_t*)local->data(),
        int(local->size() / sizeof(wchar_t)),
        (char*)path->data(),
        int(path->size()),
        NULL, NULL));
    utf8_normalize(path);
    }

void LocalPath::local2path(const std::wstring* local, string* path)
{
    path->resize((local->size() * sizeof(wchar_t) + 1) * 4 / sizeof(wchar_t) + 1);

    path->resize(WideCharToMultiByte(CP_UTF8, 0, local->data(),
        int(local->size()),
        (char*)path->data(),
        int(path->size()),
        NULL, NULL));

    utf8_normalize(path);
}

#else

void LocalPath::path2local(const string* path, string* local)
{
#ifdef __MACH__
    path2localMac(path, local);
#else
    *local = *path;
#endif
}

void LocalPath::local2path(const string* local, string* path)
{
    *path = *local;
    LocalPath::utf8_normalize(path);
}

#endif


std::atomic<unsigned> LocalPath_tmpNameLocal_counter{};

LocalPath LocalPath::tmpNameLocal()
{
    char buf[128];
#ifdef WIN32
    sprintf(buf, ".getxfer.%lu.%u.mega", (unsigned long)GetCurrentProcessId(), ++LocalPath_tmpNameLocal_counter);
#else
    sprintf(buf, ".getxfer.%lu.%u.mega", (unsigned long)getpid(), ++LocalPath_tmpNameLocal_counter);
#endif
    return LocalPath::fromRelativePath(buf);
}

bool LocalPath::isContainingPathOf(const LocalPath& path, size_t* subpathIndex) const
{
    assert(!empty());
    assert(!path.empty());
    assert(isFromRoot == path.isFromRoot);
    assert(invariant());
    assert(path.invariant());

    if (path.localpath.size() >= localpath.size()
        && !Utils::pcasecmp(path.localpath, localpath, localpath.size()))
    {
       if (path.localpath.size() == localpath.size())
       {
           if (subpathIndex) *subpathIndex = localpath.size();
           return true;
       }
       else if (path.localpath[localpath.size()] == localPathSeparator)
       {
           if (subpathIndex) *subpathIndex = localpath.size() + 1;
           return true;
       }
       else if (!localpath.empty() &&
                path.localpath[localpath.size() - 1] == localPathSeparator)
       {
           if (subpathIndex) *subpathIndex = localpath.size();
           return true;
       }
    }
    return false;
}

bool LocalPath::nextPathComponent(size_t& subpathIndex, LocalPath& component) const
{
    assert(invariant());

    while (subpathIndex < localpath.size() && localpath[subpathIndex] == localPathSeparator)
    {
        ++subpathIndex;
    }
    size_t start = subpathIndex;
    if (start >= localpath.size())
    {
        return false;
    }
    else if (findNextSeparator(subpathIndex))
    {
        component.localpath = localpath.substr(start, subpathIndex - start);
        assert(component.invariant());
        return true;
    }
    else
    {
        component.localpath = localpath.substr(start, localpath.size() - start);
        subpathIndex = localpath.size();
        assert(component.invariant());
        return true;
    }
}

bool LocalPath::hasNextPathComponent(size_t index) const
{
    assert(invariant());
    return index < localpath.size();
}

ScopedLengthRestore::ScopedLengthRestore(LocalPath& p)
    : path(p)
    , length(path.localpath.size())
{
    assert(path.invariant());
}
ScopedLengthRestore::~ScopedLengthRestore()
{
    path.localpath.resize(length);
};

FilenameAnomalyType isFilenameAnomaly(const LocalPath& localPath, const string& remoteName, nodetype_t type)
{
    // toPath() to make sure the name is in NFC.
    auto localName = localPath.leafName().toPath();

    if (localName != remoteName)
    {
        return FILENAME_ANOMALY_NAME_MISMATCH;
    }
    else if (isReservedName(remoteName, type))
    {
        return FILENAME_ANOMALY_NAME_RESERVED;
    }

    return FILENAME_ANOMALY_NONE;
}

FilenameAnomalyType isFilenameAnomaly(const LocalPath& localPath, const Node* node)
{
    assert(node);

    return isFilenameAnomaly(localPath, node->displayname(), node->type);
}

#ifdef ENABLE_SYNC
FilenameAnomalyType isFilenameAnomaly(const LocalNode& node)
{
    return isFilenameAnomaly(node.localname, node.name, node.type);
}
#endif


bool isNetworkFilesystem(FileSystemType type)
{
    return type == FS_CIFS
           || type == FS_NFS
           || type == FS_SMB
           || type == FS_SMB2;
}

} // namespace

