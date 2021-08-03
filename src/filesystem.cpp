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
    if (islchex(c1) && islchex(c2))
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

CodeCounter::ScopeStats g_compareUtfTimings("compareUtfTimings");

// the case when the strings are over diffent character types (just uses match())
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



LocalPath NormalizeRelative(const LocalPath& path)
{
#ifdef WIN32
    using string_type = wstring;
#else // _WIN32
    using string_type = string;
#endif // ! _WIN32

    LocalPath result = path;

    // Convenience.
    string_type& raw = result.localpath;
    auto sep = LocalPath::localPathSeparator;

    // Nothing to do if the path's empty.
    if (raw.empty())
    {
        return result;
    }

    // Remove trailing separator if present.
    if (raw.back() == sep)
    {
        raw.pop_back();
    }

    // Remove leading separator if present.
    if (!raw.empty() && raw.front() == sep)
    {
        raw.erase(0, 1);
    }

    return result;
}

FileSystemAccess::FileSystemAccess()
    : waiter(NULL)
    , skip_errorreport(false)
    , transient_error(false)
#ifdef ENABLE_SYNC
    //, notifyerr(false)
    //, notifyfailed(false)
#endif
    , target_exists(false)
    , client(NULL)
{
}

void FileSystemAccess::captimestamp(m_time_t* t)
{
    // FIXME: remove upper bound before the year 2100 and upgrade server-side timestamps to BIGINT
    if (*t > (uint32_t)-1) *t = (uint32_t)-1;
    else if (*t < 0) *t = 0;
}

int FileSystemAccess::decodeEscape(const char *s) const
{
    if (!isEscape(s))
    {
        return -1;
    }

    return hexval(s[1]) << 4 | hexval(s[2]);
}

bool FileSystemAccess::isEscape(const char* s) const
{
    return *s == '%'
           && islchex(s[1])
           && islchex(s[2]);
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
    auto index = parentPath.getLeafnameByteIndex(*this);

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

bool FileSystemAccess::islocalfscompatible(const int character, const FileSystemType type) const
{
    // NUL is always escaped.
    if (!character)
    {
        return false;
    }

    // Escape '%' if it is not encoding a control character.
    if (character == '%')
    {
        return false;
    }

    // Filesystem-specific policies.
    switch (type)
    {
    case FS_APFS:
    case FS_HFS:
        return character != ':' && character != '/';
    case FS_EXT:
    case FS_F2FS:
    case FS_XFS:
        return character != '/';
    case FS_EXFAT:
    case FS_FAT32:
    case FS_FUSE:
    case FS_NTFS:
    case FS_SDCARDFS:
    case FS_UNKNOWN:
    default:
        return !(std::iscntrl(character) || strchr("\\/:?\"<>|*", character));
    }
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
        assert(utf8seqsize);
        if (utf8seqsize == 1 && !islocalfscompatible(c, fileSystemType))
        {
            const char incompatibleChar = name->at(i);
            sprintf(buf, "%%%02x", c);
            name->replace(i, 1, buf);
            LOG_debug << "Escape incompatible character for filesystem type "
                << fstypetostring(fileSystemType)
                << ", replace '" << incompatibleChar << "' by '" << buf << "'\n";
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
        // For convenience.
        const char* s = &(*name)[i];

        // Are we looking at a raw control character?
        int character = static_cast<uint8_t>(*s);

        // Are we processing an escape sequence?
        if ((character = decodeEscape(s)) < 0)
        {
            // Nope, continue.
            continue;
        }

        // Substitute in the decoded character.
        name->replace(i, 3, 1, static_cast<char>(character));
    }
}


const char *FileSystemAccess::getPathSeparator()
{
#if defined (__linux__) || defined (__ANDROID__) || defined  (__APPLE__) || defined (USE_IOS)
    return "/";
#elif defined(_WIN32) || defined(WINDOWS_PHONE)
    return "\\";
#else
    // Default case
    LOG_warn << "No path separator found";
    return "\\/";
#endif
}

void FileSystemAccess::normalize(string* filename)
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

bool FileSystemAccess::fileExistsAt(const LocalPath& path)
{
    auto fa = newfileaccess(false);
    return fa->isfile(path);
}

#ifdef ENABLE_SYNC

// default DirNotify: no notification available
DirNotify::DirNotify(const LocalPath& rootPath)
{
    localbasepath = rootPath;

    mFailed = 1;
    mFailReason = "Not initialized";
    mErrorCount = 0;
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
    return fsEventq.empty() && fsDelayedNetworkEventq.empty();
}

// notify base LocalNode + relative path/filename
void DirNotify::notify(NotificationDeque& q, LocalNode* l, Notification::ScanRequirement sr, LocalPath&& path, bool immediate)
{
    // We may be executing on a thread here so we can't access the LocalNode data structures.  Queue everything, and
    // filter when the notifications are processed.  Also, queueing it here is faster than logging the decision anyway.

    Notification n(immediate ? 0 : Waiter::ds, sr, std::move(path), l);
    q.pushBack(std::move(n));
}

// default: no fingerprint
fsfp_t DirNotify::fsfingerprint() const
{
    return 0;
}

bool DirNotify::fsstableids() const
{
    return true;
}

DirNotify* FileSystemAccess::newdirnotify(LocalNode&, LocalPath& rootPath, Waiter*)
{
    return new DirNotify(rootPath);
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
    return localpath.empty();
}

void LocalPath::clear()
{
    localpath.clear();
}

void LocalPath::erase(size_t pos, size_t count)
{
    localpath.erase(pos, count);
}

void LocalPath::truncate(size_t bytePos)
{
    localpath.resize(bytePos);
}

LocalPath LocalPath::leafName() const
{
    auto p = localpath.find_last_of(localPathSeparator);
    p = p == string::npos ? 0 : p + 1;
    LocalPath result;
    result.localpath = localpath.substr(p, localpath.size() - p);
    return result;
}

void LocalPath::append(const LocalPath& additionalPath)
{
    localpath.append(additionalPath.localpath);
}

std::string LocalPath::platformEncoded() const
{
#ifdef WIN32
    // this function is typically used where we need to pass a file path to the client app, which expects utf16 in a std::string buffer
    // some other backwards compatible cases need this format also, eg. serialization
    std::string outstr;
    outstr.resize(localpath.size() * sizeof(wchar_t));
    memcpy(const_cast<char*>(outstr.data()), localpath.data(), localpath.size() * sizeof(wchar_t));
    return outstr;
#else
    // for non-windows, it's just the same utf8 string we use anyway
    return localpath;
#endif
}


void LocalPath::appendWithSeparator(const LocalPath& additionalPath, bool separatorAlways)
{
    if (separatorAlways || localpath.size())
    {
        // still have to be careful about appending a \ to F:\ for example, on windows, which produces an invalid path
        if (!(endsInSeparator() || additionalPath.beginsWithSeparator()))
        {
            localpath.append(1, localPathSeparator);
        }
    }

    localpath.append(additionalPath.localpath);
}

void LocalPath::prependWithSeparator(const LocalPath& additionalPath)
{
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
}

LocalPath LocalPath::prependNewWithSeparator(const LocalPath& additionalPath) const
{
    LocalPath lp = *this;
    lp.prependWithSeparator(additionalPath);
    return lp;
}

void LocalPath::trimNonDriveTrailingSeparator()
{
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
}

bool LocalPath::findNextSeparator(size_t& separatorBytePos) const
{
    separatorBytePos = localpath.find(localPathSeparator, separatorBytePos);
    return separatorBytePos != string::npos;
}

bool LocalPath::findPrevSeparator(size_t& separatorBytePos, const FileSystemAccess& fsaccess) const
{
    separatorBytePos = localpath.rfind(LocalPath::localPathSeparator, separatorBytePos);
    return separatorBytePos != string::npos;
}

bool LocalPath::endsInSeparator() const
{
    return !localpath.empty() && localpath.back() == localPathSeparator;
}

bool LocalPath::beginsWithSeparator() const
{
    return !localpath.empty() && localpath.front() == localPathSeparator;
}

size_t LocalPath::getLeafnameByteIndex(const FileSystemAccess& fsaccess) const
{
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
    LocalPath result;
    result.localpath = localpath.substr(bytePos);
    return result;
}

void LocalPath::ensureWinExtendedPathLenPrefix()
{
#if defined(_WIN32) && !defined(WINDOWS_PHONE)
    if (!PathIsRelativeW(localpath.c_str()) && ((localpath.size() < 2) || memcmp(localpath.data(), L"\\\\", 4)))
    {
        localpath.insert(0, L"\\\\?\\", 4);
    }
#endif
}

LocalPath LocalPath::subpathTo(size_t bytePos) const
{
    LocalPath p;
    p.localpath = localpath.substr(0, bytePos);
    return p;
}


LocalPath LocalPath::insertFilenameCounter(unsigned counter, const FileSystemAccess& fsaccess)
{
    size_t dotindex = localpath.find_last_of('.');
    size_t sepindex = localpath.find_last_of(LocalPath::localPathSeparator);

    LocalPath result, extension;

    if (dotindex == string::npos || (sepindex != string::npos && sepindex > dotindex))
    {
        result.localpath = localpath;
    }
    else
    {
        result.localpath = localpath.substr(0, dotindex);
        extension.localpath = localpath.substr(dotindex);
    }

    ostringstream oss;
    oss << " (" << counter << ")";

    result.localpath += LocalPath::fromPath(oss.str(), fsaccess).localpath + extension.localpath;
    return result;
}


string LocalPath::toPath(const FileSystemAccess& fsaccess) const
{
    string path;
    fsaccess.local2path(&localpath, &path);
    return path;
}

string LocalPath::toPath() const
{
    // only use this one for logging, until we find out if it works for all platforms
    static FSACCESS_CLASS fsAccess;
    return toPath(fsAccess);  // fsAccess synchronization not needed, only the data passed to it is modified
}

string LocalPath::toName(const FileSystemAccess& fsaccess) const
{
    string name = toPath(fsaccess);
    fsaccess.unescapefsincompatible(&name);
    return name;
}

LocalPath LocalPath::fromPath(const string& path, const FileSystemAccess& fsaccess)
{
    LocalPath p;
    fsaccess.path2local(&path, &p.localpath);
    return p;
}

LocalPath LocalPath::fromName(string path, const FileSystemAccess& fsaccess, FileSystemType fsType)
{
    fsaccess.escapefsincompatible(&path, fsType);
    return fromPath(path, fsaccess);
}

LocalPath LocalPath::fromPlatformEncoded(string path)
{
#if defined(_WIN32)
#ifdef DEBUG
    if (path.size() % 2)
    {
        assert(!(path.size() % 2));
    }
#endif
    LocalPath p;
    p.localpath.resize(path.size() / sizeof(wchar_t));
    memcpy(const_cast<wchar_t*>(p.localpath.data()), path.data(), p.localpath.size() * sizeof(wchar_t));
    return p;
#else
    LocalPath p;
    p.localpath = std::move(path);
    return p;
#endif
}

#if defined(_WIN32)
LocalPath LocalPath::fromPlatformEncoded(wstring&& wpath)
{
    LocalPath p;
    p.localpath = std::move(wpath);
    return p;
}

wchar_t LocalPath::driveLetter()
{
    auto drivepos = localpath.find(L':');
    return drivepos == wstring::npos || drivepos < 1 ? 0 : localpath[drivepos-1];
}
#endif


LocalPath LocalPath::tmpNameLocal(const FileSystemAccess& fsaccess)
{
    LocalPath lp;
    fsaccess.tmpnamelocal(lp);
    return lp;
}

bool LocalPath::isContainingPathOf(const LocalPath& path, size_t* subpathIndex) const
{
    assert(!empty());
    assert(!path.empty());

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
        return true;
    }
    else
    {
        component.localpath = localpath.substr(start, localpath.size() - start);
        subpathIndex = localpath.size();
        return true;
    }
}

ScopedLengthRestore::ScopedLengthRestore(LocalPath& p)
    : path(p)
    , length(path.localpath.size())
{
}
ScopedLengthRestore::~ScopedLengthRestore()
{
    path.localpath.resize(length);
};

FilenameAnomalyType isFilenameAnomaly(const LocalPath& localPath, const string& remoteName, nodetype_t type)
{
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
bool Notification::fromDebris(const Sync& sync) const
{
    // Must have an associated local node.
    if (!localnode) return false;

    // Assume this filtering has been done at a higher level.
    assert(!invalidated());

    // Emitted from sync root?
    if (localnode->parent) return false;

    // Contained with debris?
    return sync.localdebrisname.isContainingPathOf(path);
}

bool Notification::invalidated() const
{
    return localnode == (LocalNode*)~0;
}
#endif

} // namespace

