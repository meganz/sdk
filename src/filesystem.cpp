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
#include "mega/filesystem.h"

#include "mega.h"
#include "mega/base64.h"
#include "mega/localpath.h"
#include "mega/logging.h"
#include "mega/mega_utf8proc.h"
#include "mega/megaclient.h"
#include "mega/node.h"
#include "mega/sync.h"
#include "megafs.h"

#include <cassert>
#include <cctype>
#include <regex>
#include <tuple>

#ifdef TARGET_OS_MAC
#include "mega/osx/osxutils.h"
#endif

#ifdef __ANDROID__
#include "mega/android/androidFileSystem.h"
#endif

namespace mega
{
std::atomic<int> FileSystemAccess::mMinimumDirectoryPermissions{0700};
std::atomic<int> FileSystemAccess::mMinimumFilePermissions{0600};

CodeCounter::ScopeStats g_compareUtfTimings("compareUtfTimings");

FSLogging FSLogging::noLogging(eNoLogging);
FSLogging FSLogging::logOnError(eLogOnError);
FSLogging FSLogging::logExceptFileNotFound(eLogExceptFileNotFound);

bool FSLogging::doLog(int os_errorcode)
{
    return setting == eLogOnError ||
          (setting == eLogExceptFileNotFound && !isFileNotFound(os_errorcode));
}

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
        i.get();
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

fsfp_t::fsfp_t(std::uint64_t fingerprint,
               std::string uuid)
  : mFingerprint(fingerprint)
  , mUUID(std::move(uuid))
{
}

fsfp_t::operator bool() const
{
    return mFingerprint != 0 || !mUUID.empty();
}

bool fsfp_t::operator==(const fsfp_t& rhs) const
{
    return mFingerprint == rhs.mFingerprint
           && mUUID == rhs.mUUID;
}

bool fsfp_t::operator<(const fsfp_t& rhs) const
{
    return std::tie(mFingerprint, mUUID)
           < std::tie(rhs.mFingerprint, rhs.mUUID);
}

bool fsfp_t::equivalent(const fsfp_t& rhs) const
{
    // Only compare legacy fingerprints if UUIDs are unavailable.
    if (mUUID.empty() || rhs.mUUID.empty())
    {
        return mFingerprint == rhs.mFingerprint;
    }

    return mUUID == rhs.mUUID;
}

std::uint64_t fsfp_t::fingerprint() const
{
    return mFingerprint;
}

void fsfp_t::reset()
{
    operator=(fsfp_t());
}

const std::string& fsfp_t::uuid() const
{
    return mUUID;
}

std::string fsfp_t::toString() const
{
    std::ostringstream ostream;

    ostream << "(fingerprint: "
            << mFingerprint
            << ", uuid: "
            << (mUUID.empty() ? "undefined" : mUUID.c_str())
            << ")";

    return ostream.str();
}

bool fsfp_tracker_t::Less::operator()(const fsfp_t* lhs,
                                      const fsfp_t* rhs) const
{
    return lhs != rhs && *lhs < *rhs;
}

fsfp_ptr_t fsfp_tracker_t::add(const fsfp_t& id)
{
    // Do we already know about this ID?
    auto i = mFingerprints.find(&id);

    // IDs already tracked.
    if (i != mFingerprints.end())
    {
        // Increment reference count.
        ++i->second.second;

        // Return reference to caller.
        return i->second.first;
    }

    // Instantiate ID.
    auto ptr = std::make_shared<fsfp_t>(id);

    // Add ID to map.
    mFingerprints.emplace(std::piecewise_construct,
                        std::forward_as_tuple(ptr.get()),
                        std::forward_as_tuple(ptr, 1));

    // Return reference to caller.
    return ptr;
}

fsfp_ptr_t fsfp_tracker_t::get(const fsfp_t& id) const
{
    // Do we know about this ID?
    auto i = mFingerprints.find(&id);

    // Don't know about this ID.
    if (i == mFingerprints.end())
        return nullptr;

    // Return reference to caller.
    return i->second.first;
}

bool fsfp_tracker_t::remove(const fsfp_t& id)
{
    // Do we know about this ID?
    auto i = mFingerprints.find(&id);

    // Don't know about this ID.
    if (i == mFingerprints.end())
        return false;

    // Remove ID if reference count drops to zero.
    if (!--i->second.second)
        mFingerprints.erase(i);

    // Let caller know we removed an ID reference.
    return true;
}

int compareUtf(const string& s1, bool unescaping1, const string& s2, bool unescaping2, bool caseInsensitive)
{
    return detail::compareUtf(
                unicodeCodepointIterator(s1), unescaping1,
                unicodeCodepointIterator(s2), unescaping2,
                caseInsensitive ? Utils::toUpper: detail::identity);
}

int compareUtf(const string& s1, bool unescaping1, const LocalPath& s2, bool unescaping2, bool caseInsensitive)
{
    return detail::compareUtf(unicodeCodepointIterator(s1),
                              unescaping1,
                              unicodeCodepointIterator(s2.toPath(false)),
                              unescaping2,
                              caseInsensitive ? Utils::toUpper : detail::identity);
}

int compareUtf(const LocalPath& s1, bool unescaping1, const string& s2, bool unescaping2, bool caseInsensitive)
{
    return detail::compareUtf(unicodeCodepointIterator(s1.toPath(false)),
                              unescaping1,
                              unicodeCodepointIterator(s2),
                              unescaping2,
                              caseInsensitive ? Utils::toUpper : detail::identity);
}

int compareUtf(const LocalPath& s1, bool unescaping1, const LocalPath& s2, bool unescaping2, bool caseInsensitive)
{
    return detail::compareUtf(unicodeCodepointIterator(s1.toPath(false)),
                              unescaping1,
                              unicodeCodepointIterator(s2.toPath(false)),
                              unescaping2,
                              caseInsensitive ? Utils::toUpper : detail::identity);
}

RemotePath::RemotePath(const string& path)
  : mPath(path)
{
}

RemotePath& RemotePath::operator=(const string& rhs)
{
    return mPath = rhs, *this;
}

bool RemotePath::operator==(const RemotePath& rhs) const
{
    return mPath == rhs.mPath;
}

bool RemotePath::operator==(const string& rhs) const
{
    return mPath == rhs;
}

RemotePath::operator const string&() const
{
    return mPath;
}

void RemotePath::appendWithSeparator(const RemotePath& component, bool always)
{
    appendWithSeparator(component.mPath, always);
}

void RemotePath::appendWithSeparator(const string& component, bool always)
{
    // Only add a separator if necessary.
    while (always || !mPath.empty())
    {
        // Does the path already end with a separator?
        if (endsInSeparator())
            break;

        // Does the component begin with a separator?
        if (component.empty() || component.front() == '/')
            break;

        // Add the separator.
        mPath.append(1, '/');
        break;
    }

    // Add the component.
    mPath.append(component);
}

bool RemotePath::beginsWithSeparator() const
{
    return !mPath.empty() && mPath.front() == '/';
}

void RemotePath::clear()
{
    mPath.clear();
}

bool RemotePath::empty() const
{
    return mPath.empty();
}

bool RemotePath::endsInSeparator() const
{
    return !mPath.empty() && mPath.back() == '/';
}

bool RemotePath::findNextSeparator(size_t& index) const
{
    index = std::min(mPath.find('/', index), mPath.size());

    return index < mPath.size();
}

bool RemotePath::hasNextPathComponent(size_t index) const
{
    return index < mPath.size();
}

bool RemotePath::nextPathComponent(size_t& index, RemotePath& component) const
{
    // Skip leading separators.
    while (index < mPath.size() && mPath[index] == '/')
        ++index;

    // Have we hit the end of the string?
    if (index >= mPath.size())
        return component.clear(), false;

    // Start of component.
    auto i = index;

    // Locate next separator.
    findNextSeparator(index);

    // Extract component.
    component.mPath.assign(mPath, i, index - i);

    return true;
}

void RemotePath::prependWithSeparator(const RemotePath& component)
{
    // Add a separator only if necessary.
    if (!beginsWithSeparator() && !component.endsInSeparator())
        mPath.insert(0, 1, '/');

    // Prepend the component.
    mPath.insert(0, component.mPath);
}

const string& RemotePath::str() const
{
    return mPath;
}

RemotePath RemotePath::subpathFrom(size_t index) const
{
    RemotePath path;

    path.mPath = mPath.substr(index, string::npos);

    return path;
}

RemotePath RemotePath::subpathTo(size_t index) const
{
    RemotePath path;

    path.mPath.assign(mPath, 0, index);

    return path;
}

const string& RemotePath::toName(const FileSystemAccess&) const
{
    return mPath;
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

FileSystemAccess::FileSystemAccess()
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


const char *FileSystemAccess::fstypetostring(FileSystemType type)
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
        case FS_LIFS:
            return "LIFS";
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

    parentPath = parentPath.parentPath();
    if (getlocalfstype(parentPath, type))
    {
        return type;
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

    // it turns out that escaping the escape % character doesn't interact well with the
    // existing sync code, should an older megasync etc be running in the same account
    // so let's leave this aspect the same as the old system, for now at least.

    // Filesystem-specific policies.
    switch (type)
    {
    case FS_HFS:
        return character != ':' && character != '/';
    case FS_APFS:
    case FS_EXT:
    case FS_F2FS:
    case FS_XFS:
        return character != '/';
    case FS_EXFAT:
    case FS_FAT32:
    case FS_FUSE:
    case FS_NTFS:
    case FS_SDCARDFS:
    case FS_LIFS:
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
            snprintf(buf, sizeof(buf), "%%%02x", c);
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
        if (decodeEscape(name->c_str() + i, c)) // it must be a null terminated c-style string passed here
        {
            // Substitute in the decoded character.
            name->replace(i, 3, 1, c);
        }
    }
}

std::unique_ptr<LocalPath> FileSystemAccess::fsShortname(const LocalPath& localname)
{
    LocalPath s;
    if (getsname(localname, s))
    {
        return std::make_unique<LocalPath>(std::move(s));
    }
    return nullptr;
}

handle FileSystemAccess::fsidOf(const LocalPath& path, bool follow, bool skipcasecheck, FSLogging fsl)
{
    auto fileAccess = newfileaccess(follow);

    if (fileAccess->fopen(path, true, false, fsl, nullptr, false, skipcasecheck))
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
DirNotify::DirNotify(const LocalPath& rootPath)
{
    assert(!rootPath.empty());
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
    return fsEventq.empty();
}

// notify base LocalNode + relative path/filename
void DirNotify::notify(NotificationDeque& q, LocalNode* l, Notification::ScanRequirement sr, LocalPath&& path, bool immediate)
{
    // We may be executing on a thread here so we can't access the LocalNode data structures.  Queue everything, and
    // filter when the notifications are processed.  Also, queueing it here is faster than logging the decision anyway.
    Notification n(immediate ? 0 : Waiter::ds.load(), sr, std::move(path), l);
    q.pushBack(std::move(n));
}

DirNotify* FileSystemAccess::newdirnotify(LocalNode&, const LocalPath& rootPath, Waiter*)
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
bool FileAccess::fopen(const LocalPath& name, FSLogging fsl)
{
    updatelocalname(name, true);

    fopenSucceeded = sysstat(&mtime, &size, FSLogging::noLogging);
    if (!fopenSucceeded && fsl.doLog(errorcode))
    {
        LOG_err << "Unable to FileAccess::fopen('" << name << "'): sysstat() failed: error code: " << errorcode << ": " << FileSystemAccess::getErrorMessage(errorcode);
    }
    return fopenSucceeded;
}

bool FileAccess::isfile(const LocalPath& path)
{
    auto name = std::move(nonblocking_localname);

    updatelocalname(path, true);
    sysstat(&mtime, &size, FSLogging::noLogging);
    updatelocalname(name, true);

    return type == FILENODE;
}

bool FileAccess::isfolder(const LocalPath& path)
{
    auto name = std::move(nonblocking_localname);

    updatelocalname(path, true);
    sysstat(&mtime, &size, FSLogging::noLogging);
    updatelocalname(name, true);

    return type == FOLDERNODE;
}

// check if size and mtime are unchanged, then open for reading
bool FileAccess::openf(FSLogging fsl)
{
    if (nonblocking_localname.empty())
    {
        // file was not opened in nonblocking mode
        return true;
    }

    m_time_t curr_mtime;
    m_off_t curr_size;
    if (!sysstat(&curr_mtime, &curr_size, FSLogging::noLogging))
    {
        if (fsl.doLog(errorcode))
        {
            LOG_err << "Error opening file handle (sysstat) '"
                << nonblocking_localname << "': errorcode " << errorcode << ": " << FileSystemAccess::getErrorMessage(errorcode);
        }
        return false;
    }

    if (curr_mtime != mtime || curr_size != size)
    {
        mtime = curr_mtime;
        size = curr_size;
        retry = false;
        return false;
    }

    bool r = sysopen(false, FSLogging::noLogging);
    if (!r && fsl.doLog(errorcode)) {
        // file may have been deleted just now
        LOG_err << "Error opening file handle (sysopen) '"
                << nonblocking_localname << "': errorcode " << errorcode << ": " << FileSystemAccess::getErrorMessage(errorcode);
    }
    return r;
}

void FileAccess::closef()
{
    if (!nonblocking_localname.empty())
    {
        sysclose();
    }
}

bool FileAccess::fstat()
{
    return fstat(mtime, size);
}

void FileAccess::asyncopfinished(void *param)
{
    Waiter *waiter = (Waiter *)param;
    if (waiter)
    {
        waiter->notify();
    }
}

AsyncIOContext *FileAccess::asyncfopen(const LocalPath& f, FSLogging fsl)
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

    context->failed = !sysstat(&mtime, &size, fsl);
    context->retry = this->retry;
    context->finished = true;
    context->userCallback(context->userData);
    return context;
}

bool FileAccess::asyncopenf(FSLogging fsl)
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
    if (!sysstat(&curr_mtime, &curr_size, FSLogging::noLogging))
    {
        if (fsl.doLog(errorcode))
        {
            LOG_err << "Error opening async file handle (sysstat): '" << nonblocking_localname << "': " << errorcode << ": " << FileSystemAccess::getErrorMessage(errorcode);
        }
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
    bool result = sysopen(true, FSLogging::noLogging);
    if (result)
    {
        isAsyncOpened = true;
    }
    else if (fsl.doLog(errorcode))
    {
        LOG_err << "Error opening async file handle (sysopen): '" << nonblocking_localname << "': " << errorcode << ": " << FileSystemAccess::getErrorMessage(errorcode);
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

AsyncIOContext *FileAccess::asyncfread(string *dst, unsigned len, unsigned pad, m_off_t pos, FSLogging fsl)
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

    if (!asyncopenf(fsl))
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

bool FileAccess::fread(string* buffer,
                       unsigned long length,
                       unsigned long padding,
                       m_off_t offset,
                       FSLogging logging,
                       bool* cretry)
{
    // Make sure the file's been opened.
    if (!openf(logging))
        return false;

    // Make sure our buffer is large enough.
    buffer->resize(length + padding);

    // Try and perform the read.
    auto result = sysread(buffer->data(), length, offset, cretry);

    // Read was successful so zero pad bytes.
    if (result && padding)
        std::memset(buffer->data() + length, 0, padding);

    // Close the file if necessary.
    closef();

    // Let the caller know if the read was successful.
    return result;
}

bool FileAccess::frawread(void* buffer,
                          unsigned long length,
                          m_off_t offset,
                          bool alreadyOpened,
                          FSLogging logging,
                          bool* cretry)
{
    // Couldn't open the file.
    if (!alreadyOpened && !openf(logging))
        return false;

    // Try and perform the read.
    auto result = sysread(buffer, length, offset, cretry);

    // Close the file if necessary.
    if (!alreadyOpened)
        closef();

    // Let the caller know if the read was successful.
    return result;
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

    if (fileAccess->frawread(buffer, size, offset, true, FSLogging::logOnError))
    {
        offset += size;
        return true;
    }

    LOG_warn << "Invalid read on FileInputStream";
    return false;
}

#ifdef ENABLE_SYNC
bool Notification::fromDebris(const Sync& sync) const
{
    // Must not be the root.
    if (path.empty()) return false;

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

LocalPath FileNameGenerator::suffixWithN(FileAccess* fa, const LocalPath& localname)
{
    return suffix(fa, localname, [ ](unsigned num) { return " (" + std::to_string(num) + ")"; });
}

LocalPath FileNameGenerator::suffixWithOldN(FileAccess* fa, const LocalPath& localname)
{
    LocalPath currentLeafName;
    // We need to get current name, in case of unsensitive case systems, suffix is added to current
    // name without any change case
    fa->fopen(localname,
              true,
              false,
              FSLogging::logExceptFileNotFound,
              nullptr,
              false,
              false,
              &currentLeafName);

    LocalPath path = localname;
    if (!currentLeafName.empty() && !currentLeafName.isAbsolute() && !currentLeafName.isURI())
    {
        path.changeLeaf(currentLeafName);
    }

    return suffix(fa,
                  path,
                  [](unsigned num)
                  {
                      return ".old" + std::to_string(num);
                  });
}

LocalPath FileNameGenerator::suffix(FileAccess* fa, const LocalPath& localname, std::function<std::string(unsigned)> suffixF)
{
    LocalPath localnewname;
    unsigned num = 0;
    do
    {
        num++;
        localnewname = localname.insertFilenameSuffix(suffixF(num));
    } while (fa->fopen(localnewname, FSLogging::logExceptFileNotFound) || fa->type == FOLDERNODE);

    return localnewname;
}

FileDistributor::FileDistributor(const LocalPath& lp, size_t ntargets, m_time_t mtime, const FileFingerprint& confirm)
    : theFile(lp)
    , numTargets(ntargets)
    , mMtime(mtime)
    , confirmFingerprint(confirm)
{

}

FileDistributor::~FileDistributor()
{
    // the last operation clears the name
    lock_guard<recursive_mutex> g(mMutex);
    assert(theFile.empty()); // todo: if we haven't cleared the name, delete the file.  But we need an fsaccess for this thread which could be sync or client... maybe queue to client thread?
    assert(numTargets == 0);
}

bool FileDistributor::moveTo(const LocalPath& source,
                             LocalPath& target,
                             TargetNameExistsResolution method,
                             FileSystemAccess& fsAccess,
                             bool& transient_error,
                             bool& name_too_long,
                             [[maybe_unused]] Sync* syncForDebris,
                             [[maybe_unused]] const FileFingerprint& confirmFingerprint)
{
    assert (!!syncForDebris == (method == MoveReplacedFileToSyncDebris));

    // Try and move the source to the target.
    assert(FSNode::debugConfirmOnDiskFingerprintOrLogWhy(fsAccess, source, confirmFingerprint));
    if (fsAccess.renamelocal(source, target, method == OverwriteTarget))
    {
        assert(FSNode::debugConfirmOnDiskFingerprintOrLogWhy(fsAccess, target, confirmFingerprint));
        return true;
    }

    transient_error = fsAccess.transient_error;
    name_too_long = fsAccess.target_name_too_long;

    // the destination path already exists if method is not OverwriteTarget
    switch (method)
    {
#ifdef ENABLE_SYNC
        case MoveReplacedFileToSyncDebris:
            return moveToForMethod_MoveReplacedFileToSyncDebris(source, target, fsAccess, transient_error, name_too_long, syncForDebris, confirmFingerprint);
#endif
        case RenameWithBracketedNumber:
            return moveToForMethod_RenameWithBracketedNumber(source, target, fsAccess, transient_error, name_too_long);
        case RenameExistingToOldN:
            return moveToForMethod_RenameExistingToOldN(source, target, fsAccess, transient_error, name_too_long);
        default:
        {
            LOG_debug << "File move failed even with overwrite set. Target name: " << target;
            return false;
        }

    }
}

bool FileDistributor::moveToForMethod_RenameWithBracketedNumber(const LocalPath& source, LocalPath& target,
                        FileSystemAccess& fsAccess, bool& transient_error, bool& name_too_long)
{
        // add an (x) suffix until there's no clash
        auto fa = fsAccess.newfileaccess();
        auto changedName = FileNameGenerator::suffixWithN(fa.get(), target);
        LOG_debug << "The move destination file path exists already. Updated name: " << changedName;

        // Try and move the source to the changed name.
        if (fsAccess.renamelocal(source, changedName, false))
        {
            target = changedName;
            return true;
        }
        else
        {
            LOG_debug << "File move failed even after renaming with (N) to avoid a clash. Updated name: " << changedName;
            transient_error = fsAccess.transient_error;
            name_too_long = fsAccess.target_name_too_long;
            return false;
        }
}

bool FileDistributor::moveToForMethod_RenameExistingToOldN(const LocalPath& source, LocalPath& target,
                        FileSystemAccess& fsAccess, bool& transient_error, bool& name_too_long)
{
        // rename the existing with an .oldN suffix until there's no clash
        auto fa = fsAccess.newfileaccess();
        auto newName = FileNameGenerator::suffixWithOldN(fa.get(), target);
        LOG_debug << "The move destination file path exists already. renamed it to: " << newName;

        // Try rename the target to the new name.
        if (!fsAccess.renamelocal(target, newName, false))
        {
            LOG_debug << "Existing File renamed failed even after renaming with .oldN to avoid a clash. renamed name: " << newName;
            transient_error = fsAccess.transient_error;
            name_too_long = fsAccess.target_name_too_long;
            return false;
        }

        // Try and move the source to the target.
        if (fsAccess.renamelocal(source, target, false))
        {
            return true;
        }
        else
        {
            LOG_debug << "File move failed even after renaming the existing with .oldN to avoid a clash. renamed name: " << newName;
            transient_error = fsAccess.transient_error;
            name_too_long = fsAccess.target_name_too_long;
            return false;
        }
}

#ifdef ENABLE_SYNC
bool FileDistributor::moveToForMethod_MoveReplacedFileToSyncDebris(
    const LocalPath& source,
    LocalPath& target,
    FileSystemAccess& fsAccess,
    bool& transient_error,
    bool& name_too_long,
    Sync* syncForDebris,
    [[maybe_unused]] const FileFingerprint& confirmFingerprint)
{
        // Move the obstruction to the local debris.
        if (!syncForDebris->movetolocaldebris(target))
        {
            return false;
        }

        auto result = fsAccess.renamelocal(source, target, false);
        if (!result)
        {
            transient_error = fsAccess.transient_error;
            name_too_long = fsAccess.target_name_too_long;
            LOG_warn << "File move failed even after moving the obstruction to local debris. Target name: " << target;
        }
        else
        {
            assert(FSNode::debugConfirmOnDiskFingerprintOrLogWhy(fsAccess, target, confirmFingerprint));
        }
        return result;
}

bool FileDistributor::copyToForMethod_MoveReplacedFileToSyncDebris(
    const LocalPath& source,
    LocalPath& target,
    m_time_t mtime,
    FileSystemAccess& fsAccess,
    bool& transient_error,
    bool& name_too_long,
    Sync* syncForDebris,
    [[maybe_unused]] const FileFingerprint& confirmFingerprint)
{
        // Move the obstruction to the local debris.
        if (!syncForDebris->movetolocaldebris(target))
        {
            return false;
        }

        auto result = fsAccess.copylocal(source, target, mtime);
        if (!result)
        {
            transient_error = fsAccess.transient_error;
            name_too_long = fsAccess.target_name_too_long;
            LOG_debug << "File copy failed even after moving the obstruction to local debris. Target name: " << target;
        }
        else
        {
            assert(FSNode::debugConfirmOnDiskFingerprintOrLogWhy(fsAccess, target, confirmFingerprint));
        }
        return result;
}
#endif //ENABLE_SYNC

bool FileDistributor::copyToForMethod_RenameWithBracketedNumber(const LocalPath& source, LocalPath& target, m_time_t mtime,
                        FileSystemAccess& fsAccess, bool& transient_error, bool& name_too_long)
{
        // add an (x) suffix until there's no clash
        auto fa = fsAccess.newfileaccess();
        auto changedName = FileNameGenerator::suffixWithN(fa.get(), target);
        LOG_debug << "The copy destination file path exists already. Updated name: " << changedName;

        // copy the source to the changed name.
        if (fsAccess.copylocal(source, changedName, mtime))
        {
            target = changedName;
            return true;
        }
        else
        {
            LOG_debug << "File copy failed even after renaming with (N) to avoid a clash. Updated name: " << changedName;
            transient_error = fsAccess.transient_error;
            name_too_long = fsAccess.target_name_too_long;
            return false;
        }
}

bool FileDistributor::copyToForMethod_RenameExistingToOldN(const LocalPath& source, LocalPath& target, m_time_t mtime,
                        FileSystemAccess& fsAccess, bool& transient_error, bool& name_too_long)
{
        // rename the existing with an .oldN suffix until there's no clash
        auto fa = fsAccess.newfileaccess();
        auto newName = FileNameGenerator::suffixWithOldN(fa.get(), target);
        LOG_debug << "The copy destination file path exists already. renamed it to: " << newName;

        // Try rename the target to the new name.
        if (!fsAccess.renamelocal(target, newName, false))
        {
            LOG_debug << "Existing File renamed failed even after renaming with .oldN to avoid a clash. renamed name: " << newName;
            transient_error = fsAccess.transient_error;
            name_too_long = fsAccess.target_name_too_long;
            return false;
        }

        // Try and copy the source to the target.
        if (fsAccess.copylocal(source, target, mtime))
        {
            return true;
        }
        else
        {
            LOG_debug << "File copy failed even after renaming the existing with .oldN to avoid a clash. Updated name: " << newName;
            transient_error = fsAccess.transient_error;
            name_too_long = fsAccess.target_name_too_long;
            return false;
        }
}

bool FileDistributor::copyToForMethod_OverwriteTarget(
    const LocalPath& source,
    LocalPath& target,
    m_time_t mtime,
    FileSystemAccess& fsAccess,
    bool& transient_error,
    bool& name_too_long,
    [[maybe_unused]] const FileFingerprint& confirmFingerprint)
{
    if (fsAccess.copylocal(source, target, mtime))//copylocal is implemented as always overwrite
    {
        assert(FSNode::debugConfirmOnDiskFingerprintOrLogWhy(fsAccess, target, confirmFingerprint));
        return true;
    }
    else
    {
        transient_error = fsAccess.transient_error;
        name_too_long = fsAccess.target_name_too_long;
        return false;
    }
}

bool FileDistributor::copyTo(const LocalPath& source,
                             LocalPath& target,
                             m_time_t mtime,
                             TargetNameExistsResolution method,
                             FileSystemAccess& fsAccess,
                             bool& transient_error,
                             bool& name_too_long,
                             [[maybe_unused]] Sync* syncForDebris,
                             const FileFingerprint& confirmFingerprint)
{
    assert (!!syncForDebris == (method == MoveReplacedFileToSyncDebris));
    assert(FSNode::debugConfirmOnDiskFingerprintOrLogWhy(fsAccess, source, confirmFingerprint));

    // copy the source to the target if target is not there.
    if (!fsAccess.fileExistsAt(target))
    {
        return copyToForMethod_OverwriteTarget(source, target, mtime, fsAccess, transient_error, name_too_long, confirmFingerprint);
    }

    // the destination path already exists if method is not OverwriteTarget
    switch (method)
    {
#ifdef ENABLE_SYNC
        case MoveReplacedFileToSyncDebris:
            return copyToForMethod_MoveReplacedFileToSyncDebris(source, target, mtime, fsAccess, transient_error, name_too_long, syncForDebris, confirmFingerprint);
#endif
        case OverwriteTarget:
            return copyToForMethod_OverwriteTarget(source, target, mtime, fsAccess, transient_error, name_too_long, confirmFingerprint);
        case RenameWithBracketedNumber:
            return copyToForMethod_RenameWithBracketedNumber(source, target, mtime, fsAccess, transient_error, name_too_long);
        case RenameExistingToOldN:
            return copyToForMethod_RenameExistingToOldN(source, target, mtime, fsAccess, transient_error, name_too_long);
        default:
        {
            LOG_debug << "File copy failed as invalid method: " << method;
            return false;
        }
    }
}

bool FileDistributor::distributeTo(LocalPath& lp, FileSystemAccess& fsaccess, TargetNameExistsResolution method, bool& transient_error, bool& name_too_long, Sync* syncForDebris)
{
    transient_error = false;
    name_too_long = false;
    lock_guard<recursive_mutex> g(mMutex);

    if (lp == theFile)
    {
        actualPathUsed = true;
        removeTarget();
        return true;
    }
    else
    {
        if (numTargets == 1 && !actualPathUsed)
        {
            // the last one can be a rename (if we haven't already renamed to a final location)
            LOG_debug << "Renaming temporary file to target path";
            if (moveTo(theFile, lp, method, fsaccess, transient_error, name_too_long, syncForDebris, confirmFingerprint))
            {
                actualPathUsed = true;
                removeTarget();
                return true;
            }
            else
            {
                // maybe multiple Files were part of a single Transfer, and this last one is on a different disk
                LOG_debug << "Moving instead of renaming temporary file to target path";
                if (copyTo(theFile, lp, mMtime, method, fsaccess, transient_error, name_too_long, syncForDebris, confirmFingerprint))
                {
                    if (!fsaccess.unlinklocal(theFile))
                    {
                        LOG_debug << "Could not remove temp file after final destination copy: " << theFile;
                    }
                    removeTarget();
                    return true;
                }

            }
        }
        else
        {
            // otherwise copy
            if (copyTo(theFile, lp, mMtime, method, fsaccess, transient_error, name_too_long, syncForDebris, confirmFingerprint))
            {
                removeTarget();
                return true;
            }
        }
        return false;
    }
}

void FileDistributor::removeTarget()
{
    lock_guard<recursive_mutex> guard(mMutex);

    // Call isn't meaningful if the distributor has no targets.
    assert(numTargets && !theFile.empty());

    // Decrement the count and clear theFile if there are no more targets.
    if (!--numTargets)
        theFile.clear();
}

bool isNetworkFilesystem(FileSystemType type)
{
    return type == FS_CIFS
           || type == FS_NFS
           || type == FS_SMB
           || type == FS_SMB2;
}


std::atomic<size_t> ScanService::mNumServices(0);
std::unique_ptr<ScanService::Worker> ScanService::mWorker;
std::mutex ScanService::mWorkerLock;

ScanService::ScanService()
{
    // Locking here, rather than in the if statement, ensures that the
    // worker is fully constructed when control leaves the constructor.
    std::lock_guard<std::mutex> lock(mWorkerLock);

    if (++mNumServices == 1)
    {
        mWorker.reset(new Worker());
    }
}

ScanService::~ScanService()
{
    if (--mNumServices == 0)
    {
        std::lock_guard<std::mutex> lock(mWorkerLock);
        mWorker.reset();
    }
}

auto ScanService::queueScan(LocalPath targetPath, handle expectedFsid, bool followSymlinks, map<LocalPath, FSNode>&& priorScanChildren, shared_ptr<Waiter> waiter) -> RequestPtr
{
    // Create a request to represent the scan.
    auto request = std::make_shared<ScanRequest>(std::move(waiter), followSymlinks, targetPath, expectedFsid, std::move(priorScanChildren));

    // Queue request for processing.
    mWorker->queue(request);

    return request;
}

ScanService::ScanRequest::ScanRequest(shared_ptr<Waiter> waiter,
    bool followSymLinks,
    LocalPath targetPath,
    handle expectedFsid,
    map<LocalPath, FSNode>&& priorScanChildren)
    : mWaiter(waiter)
    , mScanResult(SCAN_INPROGRESS)
    , mFollowSymLinks(followSymLinks)
    , mKnown(std::move(priorScanChildren))
    , mResults()
    , mTargetPath(std::move(targetPath))
    , mExpectedFsid(expectedFsid)
{
}

ScanService::Worker::Worker(size_t numThreads)
    : mFsAccess(new FSACCESS_CLASS())
    , mPending()
    , mPendingLock()
    , mPendingNotifier()
    , mThreads()
{
    // Always at least one thread.
    assert(numThreads > 0);

    LOG_debug << "Starting ScanService worker...";

    // Start the threads.
    while (numThreads--)
    {
        try
        {
            mThreads.emplace_back([this]() { loop(); });
        }
        catch (std::system_error& e)
        {
            LOG_err << "Failed to start worker thread: " << e.what();
        }
    }

    LOG_debug << mThreads.size() << " worker thread(s) started.";
    LOG_debug << "ScanService worker started.";
}

ScanService::Worker::~Worker()
{
    LOG_debug << "Stopping ScanService worker...";

    // Queue the 'terminate' sentinel.
    {
        std::unique_lock<std::mutex> lock(mPendingLock);
        mPending.emplace_back();
    }

    // Wake any sleeping threads.
    mPendingNotifier.notify_all();

    LOG_debug << "Waiting for worker thread(s) to terminate...";

    // Wait for the threads to terminate.
    for (auto& thread : mThreads)
    {
        thread.join();
    }

    LOG_debug << "ScanService worker stopped.";
}

void ScanService::Worker::queue(ScanRequestPtr request)
{
    // Queue the request.
    {
        std::unique_lock<std::mutex> lock(mPendingLock);
        mPending.emplace_back(std::move(request));
    }

    // Tell the lucky thread it has something to do.
    mPendingNotifier.notify_one();
}

void ScanService::Worker::loop()
{
    // We're ready when we have some work to do.
    auto ready = [this]() { return !mPending.empty(); };

    for ( ; ; )
    {
        ScanRequestPtr request;

        {
            // Wait for something to do.
            std::unique_lock<std::mutex> lock(mPendingLock);
            mPendingNotifier.wait(lock, ready);

            assert(ready()); // condition variable should have taken care of this

            // Are we being told to terminate?
            if (!mPending.front())
            {
                // Bail, don't deque the sentinel.
                return;
            }

            request = std::move(mPending.front());
            mPending.pop_front();
        }

        LOG_verbose << "Directory scan begins: " << request->mTargetPath;
        using namespace std::chrono;
        auto scanStart = high_resolution_clock::now();

        // Process the request.
        unsigned nFingerprinted = 0;
        auto result = scan(request, nFingerprinted);
        auto scanEnd = high_resolution_clock::now();

        if (result == SCAN_SUCCESS)
        {
            LOG_verbose << "Directory scan complete for: " << request->mTargetPath
                << " entries: " << request->mResults.size()
                << " taking " << duration_cast<milliseconds>(scanEnd - scanStart).count() << "ms"
                << " fingerprinted: " << nFingerprinted;
        }
        else
        {
            LOG_verbose << "Directory scan FAILED (" << result << "): " << request->mTargetPath;
        }

        request->mScanResult = result;
        request->mWaiter->notify();
    }
}

// Really we only have one worker despite the vector of threads - maybe we should just have one
// regardless of multiple clients too - there is only one filesystem after all (but not singleton!!)
CodeCounter::ScopeStats ScanService::syncScanTime = { "folderScan" };

auto ScanService::Worker::scan(ScanRequestPtr request, unsigned& nFingerprinted) -> ScanResult
{
    CodeCounter::ScopeTimer rst(syncScanTime);

    auto result = mFsAccess->directoryScan(request->mTargetPath,
        request->mExpectedFsid,
        request->mKnown,
        request->mResults,
        request->mFollowSymLinks,
        nFingerprinted);

    // No need to keep this data around anymore.
    request->mKnown.clear();

    return result;
}

unique_ptr<FSNode> FSNode::fromFOpened(FileAccess& fa, const LocalPath& fullPath, FileSystemAccess& fsa)
{
    unique_ptr<FSNode> result(new FSNode);
    result->type = fa.type;
    result->fsid = fa.fsidvalid ? fa.fsid : UNDEF;
    result->isSymlink = fa.mIsSymLink;
    result->fingerprint.mtime = fa.mtime;
    result->fingerprint.size = fa.size;

    result->localname = fullPath.leafName();

    if (auto sn = fsa.fsShortname(fullPath))
    {
        if (*sn != result->localname)
        {
            result->shortname = std::move(sn);
        }
    }
    return result;
}

unique_ptr<FSNode> FSNode::fromPath(FileSystemAccess& fsAccess, const LocalPath& path, bool skipCaseCheck, FSLogging fsl)
{
    auto fileAccess = fsAccess.newfileaccess(false);

    LocalPath actualLeafNameIfDifferent;

    if (!fileAccess->fopen(path, true, false, fsl, nullptr, false, skipCaseCheck, &actualLeafNameIfDifferent))
        return nullptr;

    auto fsNode = fromFOpened(*fileAccess, path, fsAccess);

    if (!actualLeafNameIfDifferent.empty())
    {
        fsNode->localname = actualLeafNameIfDifferent;
    }

    if (fsNode->type != FILENODE)
        return fsNode;

    if (!fsNode->fingerprint.genfingerprint(fileAccess.get()))
        return nullptr;

    return fsNode;
}

bool FSNode::debugConfirmOnDiskFingerprintOrLogWhy(FileSystemAccess& fsAccess, const LocalPath& path, const FileFingerprint& ff)
{
    if (unique_ptr<FSNode> od = fromPath(fsAccess, path, false, FSLogging::logOnError))
    {
        if (od->fingerprint == ff) return true;
#ifdef __ANDROID__
        if (od->fingerprint.equalExceptMtime(ff))
        {
            return true;
        }
#endif

        LOG_debug << "fingerprint mismatch at path: " << path;
        LOG_debug << "size: " << od->fingerprint.size << " should have been " << ff.size;
        LOG_debug << "mtime: " << od->fingerprint.mtime << " should have been " << ff.mtime;
        LOG_debug << "crc: " << Base64Str<sizeof(FileFingerprint::crc)>((byte*)&od->fingerprint.crc)
                  << " should have been " << Base64Str<sizeof(FileFingerprint::crc)>((byte*)&ff.crc);
    }
    else
    {
        LOG_debug << "failed to get fingerprint for path " << path;
    }
    return false;
}

} // namespace
