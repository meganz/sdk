/**
 * @file mega/filesystem.h
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

#ifndef MEGA_FILESYSTEM_H
#define MEGA_FILESYSTEM_H 1

#include <atomic>
#include "types.h"
#include "utils.h"
#include "waiter.h"
#include "filefingerprint.h"

namespace mega {

// Enumeration for filesystem families
enum FileSystemType
{
    FS_UNKNOWN = -1,
    FS_APFS = 0,
    FS_HFS = 1,
    FS_EXT = 2,
    FS_FAT32 = 3,
    FS_EXFAT = 4,
    FS_NTFS = 5,
    FS_FUSE = 6,
    FS_SDCARDFS = 7,
    FS_F2FS = 8,
    FS_XFS = 9,
    FS_CIFS = 10,
    FS_NFS = 11,
    FS_SMB = 12,
    FS_SMB2 = 13,
    FS_LIFS = 14,
};

typedef void (*asyncfscallback)(void *);

struct MEGA_API AsyncIOContext;

// Opaque filesystem fingerprint.
class fsfp_t
{
    // Legacy filesystem fingerprint.
    //
    // Not necessarily unique or persistent.
    //
    // ntfs-3g or any FUSE filesystem would be an example of where
    // this value is not very meaningful.
    //
    // Legacy XFS would be an example of a filesystem where this
    // value while unique at runtime is not persistent.
    //
    // Maintained for backwards compatibility.
    std::uint64_t mFingerprint = 0;

    // Filesystem UUID.
    //
    // Should be unique and persistent across all systems.
    std::string mUUID;

public:
    fsfp_t() = default;

    fsfp_t(std::uint64_t fingerprint,
           std::string uuid);

    fsfp_t(const fsfp_t& other) = default;

    fsfp_t(fsfp_t&& other) = default;

    operator bool() const;

    bool operator!() const;

    fsfp_t& operator=(const fsfp_t& rhs) = default;

    fsfp_t& operator=(fsfp_t&& rhs) = default;

    bool operator==(const fsfp_t& rhs) const;

    bool operator<(const fsfp_t& rhs) const;

    bool operator!=(const fsfp_t& rhs) const;

    bool equivalent(const fsfp_t& rhs) const;

    std::uint64_t fingerprint() const;

    void reset();

    const std::string& uuid() const;

    std::string toString() const;
}; // fsfp_t

// Keeps track of known filesystem fingerprints.
class fsfp_tracker_t
{
    // Compare IDs by reference.
    struct Less
    {
        bool operator()(const fsfp_t* lhs,
                        const fsfp_t* rhs) const;
    }; // Less

    // Fingerprint and reference count.
    using Entry = std::pair<fsfp_ptr_t, std::size_t>;

    // Maps fingerprints to fingerprint entry.
    using Map = std::map<const fsfp_t*, Entry, Less>;

    // Tracks which fingerprints we're aware of.
    Map mFingerprints;

public:
    fsfp_tracker_t() = default;

    fsfp_tracker_t(const fsfp_tracker_t&) = delete;

    fsfp_tracker_t& operator=(const fsfp_tracker_t&) = delete;

    // Add an ID to the tracker.
    fsfp_ptr_t add(const fsfp_t& id);

    // Retrieve an ID from the tracker.
    fsfp_ptr_t get(const fsfp_t& id) const;

    // Remove an ID from the tracker.
    bool remove(const fsfp_t& id);
}; // fsfp_tracker_t

// LocalPath represents a path in the local filesystem, and wraps up common operations in a convenient fashion.
// On mac/linux, local paths are in utf8 but in windows local paths are utf16, that is wrapped up here.

struct MEGA_API FileSystemAccess;
class MEGA_API LocalPath;
class MEGA_API Sync;
struct MEGA_API FSNode;

class ScopedLengthRestore {
    LocalPath& path;
    size_t length;
public:
    // On destruction, puts the LocalPath length back to what it was on construction of this class
    ScopedLengthRestore(LocalPath&);
    ~ScopedLengthRestore();
};

extern CodeCounter::ScopeStats g_compareUtfTimings;

class MEGA_API LocalPath
{
#ifdef WIN32
    using string_type = wstring;
#else // _WIN32
    using string_type = string;
#endif // ! _WIN32

    // The actual path.  For windows, this is UTF16
    string_type localpath;

    // Track whether this LocalPath is from the root of a filesystem (ie, an absolute path)
    // It makes a big difference for windows, where we must prepend \\?\ prefix
    // to be able to access long paths, paths ending with space or `.`, etc
    bool isFromRoot = false;

    // only functions that need to call the OS or 3rdParty libraries - normal code should have no access (or accessor) to localpath
    friend class ScopedLengthRestore;
    friend class ScopedSyncPathRestore;
    friend class WinFileSystemAccess;
    friend class PosixFileSystemAccess;
    friend struct WinDirAccess;
    friend struct WinDirNotify;
    friend class LinuxDirNotify;
    friend class MacDirNotify;
    friend class PosixDirNotify;
    friend class WinFileAccess;
    friend class PosixFileAccess;
    friend void RemoveHiddenFileAttribute(LocalPath& path);
    friend void AddHiddenFileAttribute(LocalPath& path);
    friend class GfxProviderFreeImage;
    friend struct FileSystemAccess;

    friend int compareUtf(const string&, bool unescaping1, const string&, bool unescaping2, bool caseInsensitive);
    friend int compareUtf(const string&, bool unescaping1, const LocalPath&, bool unescaping2, bool caseInsensitive);
    friend int compareUtf(const LocalPath&, bool unescaping1, const string&, bool unescaping2, bool caseInsensitive);
    friend int compareUtf(const LocalPath&, bool unescaping1, const LocalPath&, bool unescaping2, bool caseInsensitive);

#ifdef _WIN32
    friend bool isPotentiallyInaccessibleName(const FileSystemAccess&, const LocalPath&, nodetype_t);
    friend bool isPotentiallyInaccessiblePath(const FileSystemAccess&, const LocalPath&, nodetype_t);
#endif // ! _WIN32

    // helper functions to ensure proper format especially on windows
    void normalizeAbsolute();
    void removeTrailingSeparators();
    bool invariant() const;

    // path2local / local2path are much more natural here than in FileSystemAccess
    // convert MEGA path (UTF-8) to local format
    // there is still at least one use from outside this class
public:
    static void path2local(const string*, string*);
    static void local2path(const string*, string*, bool normalize);
#if defined(_WIN32)
    static void local2path(const std::wstring*, string*, bool normalize);
    static void path2local(const string*, std::wstring*);
#endif

public:
    LocalPath() {}

#ifdef _WIN32
    typedef wchar_t separator_t;
    const static separator_t localPathSeparator = L'\\';
    const static char localPathSeparator_utf8 = '\\';
#else
    typedef char separator_t;
    const static separator_t localPathSeparator = '/';
    const static char localPathSeparator_utf8 = '/';
#endif

    bool isAbsolute() const { return isFromRoot; };

    // UTF-8 normalization
    static void utf8_normalize(string *);

    // returns the internal representation copied into a string buffer, for backward compatibility
    string platformEncoded() const;

    bool empty() const;
    void clear();
    void truncate(size_t bytePos);
    LocalPath leafName() const;

    /*
    * Return the last component of the path (internally uses absolute path, no matter how the instance was initialized)
    * that could be used as an actual name.
    *
    * Examples:
    *   "D:\\foo\\bar.txt"  "bar.txt"
    *   "D:\\foo\\"         "foo"
    *   "D:\\foo"           "foo"
    *   "D:\\"              "D"
    *   "D:"                "D"
    *   "D"                 "D"
    *   "D:\\.\\"           "D"
    *   "D:\\."             "D"
    *   ".\\foo\\"          "foo"
    *   ".\\foo"            "foo"
    *   ".\\"               (as in "C:\\foo\\bar\\.\\")                             "bar"
    *   "."                 (as in "C:\\foo\\bar\\.")                               "bar"
    *   "..\\..\\"          (as in "C:\\foo\\bar\\..\\..\\")                        "C"
    *   "..\\.."            (as in "C:\\foo\\bar\\..\\..")                          "C"
    *   "..\\..\\.."        (as in "C:\\foo\\bar\\..\\..\\..", thus too far back)   "C"
    *   "/" (*nix)          ""
    */
    string leafOrParentName() const;

    void append(const LocalPath& additionalPath);
    void appendWithSeparator(const LocalPath& additionalPath, bool separatorAlways);
    void prependWithSeparator(const LocalPath& additionalPath);
    LocalPath prependNewWithSeparator(const LocalPath& additionalPath) const;
    void trimNonDriveTrailingSeparator();
    bool findNextSeparator(size_t& separatorBytePos) const;
    bool findPrevSeparator(size_t& separatorBytePos, const FileSystemAccess& fsaccess) const;
    bool endsInSeparator() const;
    bool beginsWithSeparator() const;
    size_t reportSize() const { return localpath.size() * sizeof(separator_t); } // only for reporting, not logic

    // get the index of the leaf name.  A trailing separator is considered part of the leaf.
    size_t getLeafnameByteIndex() const;
    LocalPath subpathFrom(size_t bytePos) const;
    LocalPath subpathTo(size_t bytePos) const;

    // Return a path denoting this path's parent.
    //
    // Result is undefined if this path is a "root."
    LocalPath parentPath() const;

    LocalPath insertFilenameSuffix(const std::string& suffix) const;

    bool isContainingPathOf(const LocalPath& path, size_t* subpathIndex = nullptr) const;
    bool nextPathComponent(size_t& subpathIndex, LocalPath& component) const;
    bool hasNextPathComponent(size_t index) const;

    // Return a utf8 representation of the LocalPath
    // No escaping or unescaping is done.
    string toPath(bool normalize) const;

    // Return a utf8 representation of the LocalPath, taking into account that the LocalPath
    // may contain escaped characters that are disallowed for the filesystem.
    // Those characters are converted back (unescaped).  fsaccess is used to do the conversion.
    std::string toName(const FileSystemAccess& fsaccess) const;

    // Create a Localpath from a utf8 string where no character conversions or escaping is necessary.
    static LocalPath fromAbsolutePath(const string& path);
    static LocalPath fromRelativePath(const string& path);

    // Create a LocalPath from a utf8 string, making any character conversions (escaping) necessary
    // for characters that are disallowed on that filesystem.  fsaccess is used to do the conversion.
    static LocalPath fromRelativeName(string path, const FileSystemAccess& fsaccess, FileSystemType fsType);

    // Create a LocalPath from a string that was already converted to be appropriate for a local file path.
    static LocalPath fromPlatformEncodedAbsolute(string localname);
    static LocalPath fromPlatformEncodedRelative(string localname);
#ifdef WIN32
    static LocalPath fromPlatformEncodedAbsolute(wstring&& localname);
    static LocalPath fromPlatformEncodedRelative(wstring&& localname);
    wchar_t driveLetter();
#endif

    // Generates a name for a temporary file
    static LocalPath tmpNameLocal();

    bool operator==(const LocalPath& p) const { return localpath == p.localpath; }
    bool operator!=(const LocalPath& p) const { return localpath != p.localpath; }
    bool operator<(const LocalPath& p) const { return localpath < p.localpath; }

    // Try to avoid using this function as much as you can.
    //
    // It's present for efficiency reasons and is really only meant for
    // specific cases when we are using a LocalPath instance in a system
    // call.
    const string_type& rawValue() const
    {
        return localpath;
    }
};

inline std::ostream& operator<<(std::ostream& os, const LocalPath& p)
{
    return os << p.toPath(false);
}

class RemotePath
{
public:
    // Create an empty path.
    RemotePath() = default;

    // Create a remote path from a string.
    RemotePath(const string& path);

    MEGA_DEFAULT_COPY_MOVE(RemotePath);

    // For convenience.
    RemotePath& operator=(const string& rhs);

    // Compare this path against another.
    bool operator==(const RemotePath& rhs) const;
    bool operator==(const string& rhs) const;

    // Return a string representing this path.
    operator const string&() const;

    // Add a component to the end of this path.
    void appendWithSeparator(const RemotePath& component, bool always);
    void appendWithSeparator(const string& component, bool always);

    // Query whether the path begins with a separator.
    bool beginsWithSeparator() const;

    // Clear the path.
    void clear();

    // Query whether the path is empty.
    bool empty() const;

    // Query whether the path ends with a separator.
    bool endsInSeparator() const;

    // Locate the next path separator.
    bool findNextSeparator(size_t& index) const;

    // Query whether the path has any further components.
    bool hasNextPathComponent(size_t index) const;

    // Retrieve the next path component.
    bool nextPathComponent(size_t& index, RemotePath& component) const;

    // Add a path component to the start of this path.
    void prependWithSeparator(const RemotePath& component);

    // Return a string representing this path.
    const string& str() const;

    // Create a new path based on a portion of another.
    RemotePath subpathFrom(size_t index) const;
    RemotePath subpathTo(size_t index) const;

    // For compatibility with LocalPath.
    //
    // Useful when we're metaprogramming and don't knwo whether the type
    // provided by the caller is a local or remote path.
    const string &toName(const FileSystemAccess&) const;

private:
    string mPath;
}; // RemotePath

// For convenience.  first = leaf name only   second = relative path
using RemotePathPair = pair<RemotePath, RemotePath>;

// For metaprogramming.
template<typename T>
struct IsPath
  : public std::false_type
{
}; // IsPath<T>

template<>
struct IsPath<LocalPath>
    : public std::true_type
{
}; // IsPath<LocalPath>

template<>
struct IsPath<RemotePath>
  : public std::true_type
{
}; // IsPath<RemotePath>

struct NameConflict {
    struct NameHandle {
        string name;
        NodeHandle handle;
        NameHandle(const string& s, NodeHandle h) : name(s), handle(h) {}
    };
    string cloudPath;
    vector<NameHandle> clashingCloud;
    LocalPath localPath;
    vector<LocalPath> clashingLocalNames;
};

void AddHiddenFileAttribute(mega::LocalPath& path);
void RemoveHiddenFileAttribute(mega::LocalPath& path);

/**
 * @brief
 * Checks whether a contains b.
 *
 * @return
 * True if a contains b.
 */
bool IsContainingLocalPathOf(const string& a, const string& b);
bool IsContainingLocalPathOf(const string& a, const char* b, size_t bLength);

bool IsContainingCloudPathOf(const string& a, const string& b);
bool IsContainingCloudPathOf(const string& a, const char* b, size_t bLength);


inline LocalPath operator+(LocalPath& a, LocalPath& b)
{
    LocalPath result = a;
    result.append(b);
    return result;
}

class FileNameGenerator
{
public:
    // It generates a new name suffixed with (n). It is not conflicted in the file system
    // always a new name is returned
    static LocalPath suffixWithN(FileAccess* fa, const LocalPath& localname);

    // It generates a new name suffixed with .oldn. It is not conflicted in the file system
    // always a new name is returned
    static LocalPath suffixWithOldN(FileAccess* fa, const LocalPath& localname);

private:
    static LocalPath suffix(FileAccess* fa, const LocalPath& localname, std::function<std::string(unsigned)> suffixF);
};

class FileDistributor
{
    // This class is to manage copying/moving a Transfer's downloaded file
    // To all the correct locations (one per File that the transfer owns)
    // Taking into account that the sync thread will do some of the moving/copying
    // So it has to operate properly across threads.
    // Note that the file is not always "tmp", one of the target locations
    // may actually be the place it was downloaded to, rather than a tmp location.

    recursive_mutex mMutex;
    LocalPath theFile;
    size_t  numTargets;
    bool actualPathUsed = false;
    m_time_t mMtime;
    FileFingerprint confirmFingerprint;

public:
    FileDistributor(const LocalPath& lp, size_t ntargets, m_time_t mtime, const FileFingerprint& confirm);
    ~FileDistributor();

    enum TargetNameExistsResolution {
        OverwriteTarget,
        RenameWithBracketedNumber,
        MoveReplacedFileToSyncDebris,
        RenameExistingToOldN,
    };


    bool distributeTo(LocalPath& lp, FileSystemAccess& fsaccess, TargetNameExistsResolution, bool& transient_error, bool& name_too_long, Sync* syncForDebris);

    // Remove a pending distribution target.
    void removeTarget();

    // static functions used by distributeTo
    // these will be useful for other cases also

    LocalPath distributeFromPath()
    {
        lock_guard<recursive_mutex> g(mMutex);
        return theFile;
    }

private:
    static bool moveTo(const LocalPath& source, LocalPath& target,
                        TargetNameExistsResolution, FileSystemAccess& fsaccess,
                        bool& transient_error, bool& name_too_long, Sync* syncForDebris,
                        const FileFingerprint& confirmFingerprint);

    static bool moveToForMethod_RenameWithBracketedNumber(const LocalPath& source, LocalPath& target,
                        FileSystemAccess& fsaccess, bool& transient_error, bool& name_too_long);

    static bool moveToForMethod_RenameExistingToOldN(const LocalPath& source, LocalPath& target,
                        FileSystemAccess& fsaccess, bool& transient_error, bool& name_too_long);

    static bool copyTo(const LocalPath& source, LocalPath& target, m_time_t mtime,
                        TargetNameExistsResolution, FileSystemAccess& fsaccess, bool& transient_error,
                        bool& name_too_long, Sync* syncForDebris,
                        const FileFingerprint& confirmFingerprint);

    static bool copyToForMethod_RenameWithBracketedNumber(const LocalPath& source, LocalPath& target, m_time_t mtime,
                        FileSystemAccess& fsaccess, bool& transient_error, bool& name_too_long);

    static bool copyToForMethod_RenameExistingToOldN(const LocalPath& source, LocalPath& target, m_time_t mtime,
                        FileSystemAccess& fsaccess, bool& transient_error, bool& name_too_long);

    static bool copyToForMethod_OverwriteTarget(const LocalPath& source, LocalPath& target, m_time_t mtime,
                        FileSystemAccess& fsaccess, bool& transient_error, bool& name_too_long, const FileFingerprint& confirmFingerprint);


#ifdef ENABLE_SYNC
    static bool moveToForMethod_MoveReplacedFileToSyncDebris(const LocalPath& source, LocalPath& target, FileSystemAccess& fsaccess,
                        bool& transient_error, bool& name_too_long, Sync* syncForDebris,
                        const FileFingerprint& confirmFingerprint);

    static bool copyToForMethod_MoveReplacedFileToSyncDebris(const LocalPath& source, LocalPath& target, m_time_t mtime, FileSystemAccess& fsaccess,
                        bool& transient_error, bool& name_too_long, Sync* syncForDebris,
                        const FileFingerprint& confirmFingerprint);
#endif
};


struct MEGA_API AsyncIOContext
{
    enum {
        NONE, READ, WRITE, OPEN
    };

    enum {
        ACCESS_NONE     = 0x00,
        ACCESS_READ     = 0x01,
        ACCESS_WRITE    = 0x02
    };

    virtual ~AsyncIOContext();
    virtual void finish();

    // results
    asyncfscallback userCallback = nullptr;
    void *userData = nullptr;
    bool finished = false;
    bool failed = false;
    bool retry = false;

    // parameters
    int op = NONE;
    int access = ACCESS_NONE;
    m_off_t posOfBuffer = 0;
    unsigned pad = 0;
    LocalPath openPath;
    byte* dataBuffer = nullptr;
    unsigned dataBufferLen = 0;
    Waiter *waiter = nullptr;
    FileAccess *fa = nullptr;
};

// map a request tag with pending paths of temporary files
typedef map<int, vector<LocalPath> > pendingfiles_map;

struct MEGA_API DirAccess;

struct FSLogging
{
    enum Setting
    {
        eNoLogging,
        eLogOnError,
        eLogExceptFileNotFound
    };

    Setting setting;

    FSLogging(Setting s) : setting(s) {}

    bool doLog(int os_errorcode);

    static FSLogging noLogging;
    static FSLogging logOnError;
    static FSLogging logExceptFileNotFound;

private:
    static bool isFileNotFound(int error);
};


// generic host file/directory access interface
struct MEGA_API FileAccess
{
    // if true, then size and mtime are valid
    bool fopenSucceeded = false;

    // file size
    m_off_t size = 0;

    // mtime of a file opened for reading
    m_time_t mtime = 0;

    // local filesystem record id (survives renames & moves)
    handle fsid = 0;
    bool fsidvalid = false;

    // type of opened path
    nodetype_t type = TYPE_UNKNOWN;

    // if opened path is a symlink
    bool mIsSymLink = false;

    // if the open failed, retry indicates a potentially transient reason
    bool retry = false;

    // OS error code related to the last call to fopen() without parameters
    int errorcode = 0;

    // for files "opened" in nonblocking mode, the current local filename
    LocalPath nonblocking_localname;

    // waiter to notify on filesystem events
    Waiter *waiter;

    // blocking mode: open for reading, writing or reading and writing.
    // This one really does open the file, and openf(), closef() will have no effect
    // If iteratingDir is supplied, this fopen() call must be for the directory entry being iterated by dopen()/dnext()
    virtual bool fopen(const LocalPath&, bool read, bool write, FSLogging,
                DirAccess* iteratingDir = nullptr,
                bool ignoreAttributes = false,
                bool skipcasecheck = false,
                LocalPath* actualLeafNameIfDifferent = nullptr) = 0;

    // nonblocking open: Only prepares for opening.  Actually stats the file/folder, getting mtime, size, type.
    // Call openf() afterwards to actually open it if required.  For folders, returns false with type==FOLDERNODE.
    bool fopen(const LocalPath&, FSLogging);

    // check if a local path is a folder
    bool isfolder(const LocalPath& path);

    // check if local path is a file.
    bool isfile(const LocalPath& path);

    // update localname (only has an effect if operating in by-name mode)
    virtual void updatelocalname(const LocalPath&, bool force) = 0;

    // absolute position read, with NUL padding
    bool fread(string*, unsigned, unsigned, m_off_t, FSLogging);

    // absolute position read to byte buffer
    bool frawread(byte *, unsigned, m_off_t, bool caller_opened, FSLogging);

    // After a successful nonblocking fopen(), call openf() to really open the file (by localname)
    // (this is a lazy-type approach in case we don't actually need to open the file after finding out type/size/mtime).
    // If the size or mtime changed, it will fail.
    bool openf(FSLogging);

    // After calling openf(), make sure to close the file again quickly with closef().
    void closef();

    // absolute position write
    virtual bool fwrite(const byte *, unsigned, m_off_t) = 0;

    // Truncate a file.
    virtual bool ftruncate() = 0;

    FileAccess(Waiter *waiter);
    virtual ~FileAccess();

    virtual bool asyncavailable() { return false; }

    AsyncIOContext *asyncfopen(const LocalPath&, FSLogging);

    // non-locking ops: open/close temporary hFile
    bool asyncopenf(FSLogging fsl);
    void asyncclosef();

    AsyncIOContext *asyncfopen(const LocalPath&, bool, bool, m_off_t = 0);
    AsyncIOContext* asyncfread(string*, unsigned, unsigned, m_off_t, FSLogging fsl);
    AsyncIOContext* asyncfwrite(const byte *, unsigned, m_off_t);

protected:
    virtual AsyncIOContext* newasynccontext();
    static void asyncopfinished(void *param);
    bool isAsyncOpened;
    int numAsyncReads;

    // system-specific raw read/open/close to be provided by platform implementation.   fopen / openf / fread etc are implemented by calling these.
    virtual bool sysread(byte *, unsigned, m_off_t) = 0;
    virtual bool sysstat(m_time_t*, m_off_t*, FSLogging) = 0;
    virtual bool sysopen(bool async, FSLogging) = 0;
    virtual void sysclose() = 0;
    virtual void asyncsysopen(AsyncIOContext*);
    virtual void asyncsysread(AsyncIOContext*);
    virtual void asyncsyswrite(AsyncIOContext*);
};

class MEGA_API FileInputStream : public InputStreamAccess
{
    FileAccess *fileAccess;
    m_off_t offset;

public:
    FileInputStream(FileAccess *fileAccess);

    m_off_t size() override;
    bool read(byte *buffer, unsigned size) override;
};

// generic host directory enumeration
struct MEGA_API DirAccess
{
    // open for scanning
    virtual bool dopen(LocalPath*, FileAccess*, bool) = 0;

    // get next record
    virtual bool dnext(LocalPath&, LocalPath&, bool = true, nodetype_t* = NULL) = 0;

    virtual ~DirAccess() { }
};

#ifdef ENABLE_SYNC

struct Notification
{
    bool fromDebris(const Sync& sync) const;
    bool invalidated() const;

    enum ScanRequirement
    {
        NEEDS_PARENT_SCAN, // normal case.  For a plain path (eg. file), we would scan the parent to see if the item at this path chagned
        FOLDER_NEEDS_SELF_SCAN,  // But, if the notification means that we should scan the contents of the actual path rather than the parent, set this flag.
        FOLDER_NEEDS_SCAN_RECURSIVE  // And sometimes we need to scan recursively from that point (eg. glitch in stream of incoming notifications)
    };

    dstime timestamp;
    ScanRequirement scanRequirement = NEEDS_PARENT_SCAN;
    LocalPath path;
    LocalNode* localnode = nullptr;

    Notification() {}
    Notification(dstime ts, ScanRequirement sr, const LocalPath& p, LocalNode* ln)
        : timestamp(ts), scanRequirement(sr), path(p), localnode(ln)
        {}
};

struct NotificationDeque : ThreadSafeDeque<Notification>
{
    void replaceLocalNodePointers(LocalNode* check, LocalNode* newvalue)
    {
        std::lock_guard<std::mutex> g(m);
        for (auto& n : mNotifications)
        {
            if (n.localnode == check)
            {
                n.localnode = newvalue;
            }
        }
    }
};

// filesystem change notification, highly coupled to Syncs and LocalNodes.
struct MEGA_API DirNotify
{
    // Thread safe so that a separate thread can listen for filesystem notifications (for windows for now, maybe more platforms later)
    NotificationDeque fsEventq;

private:
    // these next few fields may be updated by notification-reading threads
    std::mutex mMutex;

    // set if no notification available on this platform or a permanent failure
    // occurred
    int mFailed;

    // reason of the permanent failure of filesystem notifications
    string mFailReason;

public:
    // set if a temporary error occurred.  May be set from a thread.
    std::atomic<int> mErrorCount;

    // thread safe setter/getters
    void setFailed(int errCode, const string& reason);
    int  getFailed(string& reason);

    // base path
    LocalPath localbasepath;

    void notify(NotificationDeque&, LocalNode *, Notification::ScanRequirement, LocalPath&&, bool = false);

    DirNotify(const LocalPath& rootPath);
    virtual ~DirNotify() {}

    bool empty();
};
#endif

// For directoryScan(...).
struct MEGA_API FSNode;

// generic host filesystem access interface
struct MEGA_API FileSystemAccess : public EventTrigger
{
    // waiter to notify on filesystem events
    Waiter *waiter = nullptr;

    // indicate target_exists error logging is not necessary on this call as we may try something else for overall operation success
    bool skip_targetexists_errorreport = false;

    /**
     * @brief instantiate FileAccess object
     * @param followSymLinks whether symlinks should be followed when opening a path (default: true)
     * @return
     */
    virtual std::unique_ptr<FileAccess> newfileaccess(bool followSymLinks = true) = 0;

    // instantiate DirAccess object
    virtual unique_ptr<DirAccess> newdiraccess() = 0;

#ifdef ENABLE_SYNC
    // instantiate DirNotify object (default to periodic scanning handler if no
    // notification configured) with given root path
    virtual DirNotify* newdirnotify(LocalNode& root, const LocalPath& rootPath, Waiter* waiter);
#endif

    // Extracts the character encoded by the escape sequence %ab at s,
    // if it is one,
    // which must be part of a null terminated c-style string
    bool decodeEscape(const char* s, char& escapedChar) const;

    bool islocalfscompatible(const int character, const FileSystemType type) const;
    void escapefsincompatible(string*, FileSystemType fileSystemType) const;

    static const char *fstypetostring(FileSystemType type);
    virtual bool getlocalfstype(const LocalPath& path, FileSystemType& type) const = 0;
    FileSystemType getlocalfstype(const LocalPath& path) const;
    void unescapefsincompatible(string*) const;

    // obtain local secondary name
    virtual bool getsname(const LocalPath&, LocalPath&) const = 0;

    // rename file, overwrite target
    virtual bool renamelocal(const LocalPath&, const LocalPath&, bool = true) = 0;

    // copy file, overwrite target, set mtime
    virtual bool copylocal(const LocalPath&, const LocalPath&, m_time_t) = 0;

    // delete file
    virtual bool unlinklocal(const LocalPath&) = 0;

    // delete empty directory
    virtual bool rmdirlocal(const LocalPath&) = 0;

    // create directory, optionally hidden
    virtual bool mkdirlocal(const LocalPath&, bool hidden, bool logAlreadyExistsError) = 0;

    // make sure that we stay within the range of timestamps supported by the server data structures (unsigned 32-bit)
    static void captimestamp(m_time_t*);

    // set mtime
    virtual bool setmtimelocal(const LocalPath&, m_time_t) = 0;

    // change working directory
    virtual bool chdirlocal(LocalPath&) const = 0;

    // obtain lowercased extension
    virtual bool getextension(const LocalPath&, std::string&) const = 0;

    // check if synchronization is supported for a specific path
    virtual bool issyncsupported(const LocalPath&, bool&, SyncError&, SyncWarning&) = 0;

    // get the absolute path corresponding to a path
    virtual bool expanselocalpath(const LocalPath& path, LocalPath& absolutepath) = 0;

    // default permissions for new files
    virtual int getdefaultfilepermissions() { return 0600; }
    virtual void setdefaultfilepermissions(int) { }

    // default permissions for new folder
    virtual int getdefaultfolderpermissions() { return 0700; }
    virtual void setdefaultfolderpermissions(int) { }

    // convenience function for getting filesystem shortnames
    std::unique_ptr<LocalPath> fsShortname(const LocalPath& localpath);

    // convenience function for testing file existence at a path
    bool fileExistsAt(const LocalPath&);

    // set whenever an operation fails due to a transient condition (e.g. locking violation)
    bool transient_error = false;

    // set whenever an operation fails because the target already exists
    bool target_exists = false;

    // Set when an operation fails because the target file name is too long.
    bool target_name_too_long = false;

    // append local operating system version information to string.
    // Set includeArchExtraInfo to know if the app is 32 bit running on 64 bit (on windows, that is via the WOW subsystem)
    virtual void osversion(string*, bool includeArchExtraInfo) const { }

    // append id for stats
    virtual void statsid(string*) const { }

    FileSystemAccess();

    MEGA_DISABLE_COPY_MOVE(FileSystemAccess);

    virtual ~FileSystemAccess() { }

    // Get the current working directory.
    static bool cwd_static(LocalPath& path);
    virtual bool cwd(LocalPath& path) const = 0;

    // Retrieve the fingerprint of the filesystem containing the specified path.
    fsfp_t fsFingerprint(const LocalPath& path) const;

#ifdef ENABLE_SYNC
    // True if the filesystem indicated by the specified path has stable FSIDs.
    virtual bool fsStableIDs(const LocalPath& path) const = 0;

    virtual bool initFilesystemNotificationSystem();
#endif // ENABLE_SYNC

    virtual ScanResult directoryScan(const LocalPath& path,
                                     handle expectedFsid,
                                     map<LocalPath, FSNode>& known,
                                     std::vector<FSNode>& results,
                                     bool followSymLinks,
                                     unsigned& nFingerprinted) = 0;

    // Retrieve the FSID of the item at the specified path.
    // UNDEF is returned if we cannot determine the item's FSID.
    handle fsidOf(const LocalPath& path, bool follow, bool skipcasecheck, FSLogging);

    // Create a hard link from source to target.
    // Returns false if the link could not be created.
    virtual bool hardLink(const LocalPath& source, const LocalPath& target) = 0;

    // @brief
    // Retrieves the number of bytes available on the specified filesystem.
    //
    // @param drivePath
    // The path to the filesystem you'd like to query.
    //
    // @return
    // On success, the number of free bytes available to the caller.
    // On failure, zero.
    virtual m_off_t availableDiskSpace(const LocalPath& drivePath) = 0;

    // Specify the minimum permissions for new created directories.
    static void setMinimumDirectoryPermissions(int permissions);

    // Specify the minimum permissions for newly created files.
    static void setMinimumFilePermissions(int permissions);

    // return a description of OS error,
    // errno on unix. Defaults to the number itself.
    static std::string getErrorMessage(int error);

    // Check if the specified file is "hidden."
    //
    // On UNIX systems, this function will only return true if the
    // file specified by path begins with the period character.
    //
    // That is, "a" would not be hidden but ".a" would be.
    //
    // On Windows systems, this function will only return true if the
    // file specified by the path has its "hidden" attribute set.
    //
    // Returns:
    // >0 if the file is hidden.
    // =0 if the file is not hidden.
    // <0 if the file cannot be accessed.
    static int isFileHidden(const LocalPath& path,
                            FSLogging logWhen = FSLogging::logOnError);

    // Mark the specified file as "hidden."
    //
    // On UNIX systems, this function is a no-op and always returns true.
    //
    // On Windows systems, this function will set the file's "hidden"
    // attribute.
    //
    // Returns:
    // True if the file's hidden attribute was set.
    static bool setFileHidden(const LocalPath& path,
                              FSLogging logWhen = FSLogging::logOnError);

protected:
    // Specifies the minimum permissions allowed for directories.
    static std::atomic<int> mMinimumDirectoryPermissions;

    // Specifies the minimum permissions allowed for files.
    static std::atomic<int> mMinimumFilePermissions;
};

int compareUtf(const string&, bool unescaping1, const string&, bool unescaping2, bool caseInsensitive);
int compareUtf(const string&, bool unescaping1, const LocalPath&, bool unescaping2, bool caseInsensitive);
int compareUtf(const LocalPath&, bool unescaping1, const string&, bool unescaping2, bool caseInsensitive);
int compareUtf(const LocalPath&, bool unescaping1, const LocalPath&, bool unescaping2, bool caseInsensitive);

// Same as above except case insensitivity is determined by build platform.
int platformCompareUtf(const string&, bool unescape1, const string&, bool unescape2);
int platformCompareUtf(const string&, bool unescape1, const LocalPath&, bool unescape2);
int platformCompareUtf(const LocalPath&, bool unescape1, const string&, bool unescape2);
int platformCompareUtf(const LocalPath&, bool unescape1, const LocalPath&, bool unescape2);

struct MEGA_API FSNode
{
    // A structure convenient for containing just the attributes of one item from the filesystem
    LocalPath localname;
    unique_ptr<LocalPath> shortname;
    nodetype_t type = TYPE_UNKNOWN;
    mega::handle fsid = mega::UNDEF;
    bool isSymlink = false;
    bool isBlocked = false;
    FileFingerprint fingerprint; // includes size, mtime

    bool equivalentTo(const FSNode& n) const
    {
        if (type != n.type) return false;

        if (fsid != n.fsid) return false;

        if (isSymlink != n.isSymlink) return false;

        if (type == FILENODE && !(fingerprint == n.fingerprint)) return false;

        if (localname != n.localname) return false;

        return (!shortname && (!n.shortname || localname == *n.shortname))
               || (shortname && n.shortname && *shortname == *n.shortname);
    }

    unique_ptr<LocalPath> cloneShortname() const
    {
        return unique_ptr<LocalPath>(
            shortname
            ? new LocalPath(*shortname)
            : nullptr);
    }

    FSNode clone() const
    {
        FSNode f;
        f.localname = localname;
        f.shortname = cloneShortname();
        f.type = type;
        f.fsid = fsid;
        f.isSymlink = isSymlink;
        f.isBlocked = isBlocked;
        f.fingerprint = fingerprint;
        return f;
    }

    static unique_ptr<FSNode> fromFOpened(FileAccess&, const LocalPath& fullName, FileSystemAccess& fsa);

    // Same as the above but useful in situations where we don't have an FA handy.
    static unique_ptr<FSNode> fromPath(FileSystemAccess& fsAccess, const LocalPath& path, bool skipCaseCheck, FSLogging);

    // Use this in asserts() to check if race conditions may be occurring
    static bool debugConfirmOnDiskFingerprintOrLogWhy(FileSystemAccess& fsAccess, const LocalPath& path, const FileFingerprint& ff);

    const string& toName_of_localname(const FileSystemAccess& fsaccess)
    {
        // Although FSNode wouldn't naturally have a utf8 and normalized version of localname,
        // we need to compare such a string during sorting operations.
        // Using a caching mechanism like this avoids execessive conversions, normalization, and computing that if it's not used.
        if (toName_of_localname_cached.empty())
        {
            toName_of_localname_cached = localname.toName(fsaccess);
        }
        return toName_of_localname_cached;
    }

private:
    string toName_of_localname_cached;
};

class MEGA_API ScanService
{
public:
    ScanService();
    ~ScanService();

    // Concrete representation of a scan request.
    class ScanRequest
    {
    public:
        ScanRequest(shared_ptr<Waiter> waiter,
            bool followSymlinks,
            LocalPath targetPath,
            handle expectedFsid,
            map<LocalPath, FSNode>&& priorScanChildren);

        MEGA_DISABLE_COPY_MOVE(ScanRequest);

        bool completed() const
        {
            return mScanResult != SCAN_INPROGRESS;
        };

        ScanResult completionResult() const
        {
            return mScanResult;
        };

        std::vector<FSNode>&& resultNodes()
        {
            return std::move(mResults);
        }

        handle fsidScanned()
        {
            return mExpectedFsid;
        }

    private:
        friend class ScanService;

        // Waiter to notify when done
        shared_ptr<Waiter> mWaiter;

        // Whether the scan request is complete.
        std::atomic<ScanResult> mScanResult; // SCAN_INPROGRESS;

        // Whether we should follow symbolic links.
        const bool mFollowSymLinks;

        // Details the known children of mTarget.
        map<LocalPath, FSNode> mKnown;

        // Results of the scan.
        vector<FSNode> mResults;

        // Path to the target.
        const LocalPath mTargetPath;

        // fsid that the target path should still referene
        handle mExpectedFsid;

    }; // ScanRequest

    // For convenience.
    using RequestPtr = std::shared_ptr<ScanRequest>;

    // Issue a scan for the given target.
    RequestPtr queueScan(LocalPath targetPath, handle expectedFsid, bool followSymlinks, map<LocalPath, FSNode>&& priorScanChildren, shared_ptr<Waiter> waiter);

    // Track performance (debug only)
    static CodeCounter::ScopeStats syncScanTime;

private:
       // Convenience.
    using ScanRequestPtr = std::shared_ptr<ScanRequest>;

    // Processes scan requests.
    class Worker
    {
    public:
        Worker(size_t numThreads = 1);

        ~Worker();

        MEGA_DISABLE_COPY_MOVE(Worker);

        // Queues a scan request for processing.
        void queue(ScanRequestPtr request);

    private:
        // Thread entry point.
        void loop();

        // Processes a scan request.
        ScanResult scan(ScanRequestPtr request, unsigned& nFingerprinted);

        // Filesystem access.
        std::unique_ptr<FileSystemAccess> mFsAccess;

        // Pending scan requests.
        std::deque<ScanRequestPtr> mPending;

        // Guards access to the above.
        std::mutex mPendingLock;
        std::condition_variable mPendingNotifier;

        // Worker threads.
        std::vector<std::thread> mThreads;
    }; // Worker

    // How many services are currently active.
    static std::atomic<size_t> mNumServices;

    // Worker shared by all services.
    static std::unique_ptr<Worker> mWorker;

    // Synchronizes access to the above.
    static std::mutex mWorkerLock;

}; // ScanService

// True if type denotes a network filesystem.
bool isNetworkFilesystem(FileSystemType type);

} // namespace

#endif
