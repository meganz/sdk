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
    FS_XFS = 9
};

typedef void (*asyncfscallback)(void *);

struct MEGA_API AsyncIOContext;


// LocalPath represents a path in the local filesystem, and wraps up common operations in a convenient fashion.
// On mac/linux, local paths are in utf8 but in windows local paths are utf16, that is wrapped up here.

struct MEGA_API FileSystemAccess;
class MEGA_API LocalPath;
class MEGA_API Sync;

class ScopedLengthRestore {
    LocalPath& path;
    size_t length;
public:
    // On destruction, puts the LocalPath length back to what it was on construction of this class
    ScopedLengthRestore(LocalPath&);
    ~ScopedLengthRestore();
};

class MEGA_API LocalPath
{
#if defined(_WIN32)
    wstring localpath;
#else
    string localpath;
#endif

    // only functions that need to call the OS or 3rdParty libraries - normal code should have no access (or accessor) to localpath
    friend class ScopedLengthRestore;
    friend class WinFileSystemAccess;
    friend class PosixFileSystemAccess;
    friend struct WinDirAccess;
    friend struct WinDirNotify;
    friend class PosixDirNotify;
    friend class WinFileAccess;
    friend class PosixFileAccess;
    friend LocalPath NormalizeAbsolute(const LocalPath& path);
    friend LocalPath NormalizeRelative(const LocalPath& path);
    friend void RemoveHiddenFileAttribute(LocalPath& path);
    friend void AddHiddenFileAttribute(LocalPath& path);
    friend class GfxProcFreeImage;
    friend struct FileSystemAccess;
    friend int computeReversePathMatchScore(const LocalPath& path1, const LocalPath& path2, const FileSystemAccess& fsaccess);
#ifdef USE_ROTATIVEPERFORMANCELOGGER
    friend class RotativePerformanceLoggerLoggingThread;
#endif
#ifdef USE_IOS
    friend const string adjustBasePath(const LocalPath& name);
#else
    friend const string& adjustBasePath(const LocalPath& name);
#endif
    friend int compareUtf(const string&, bool unescaping1, const string&, bool unescaping2, bool caseInsensitive);
    friend int compareUtf(const string&, bool unescaping1, const LocalPath&, bool unescaping2, bool caseInsensitive);
    friend int compareUtf(const LocalPath&, bool unescaping1, const string&, bool unescaping2, bool caseInsensitive);
    friend int compareUtf(const LocalPath&, bool unescaping1, const LocalPath&, bool unescaping2, bool caseInsensitive);

#ifdef _WIN32
    friend bool isPotentiallyInaccessibleName(const FileSystemAccess&, const LocalPath&, nodetype_t);
    friend bool isPotentiallyInaccessiblePath(const FileSystemAccess&, const LocalPath&, nodetype_t);
#endif // ! _WIN32

public:
    LocalPath() {}

#ifdef _WIN32
    typedef wchar_t separator_t;
    const static separator_t localPathSeparator = L'\\';
#else
    typedef char separator_t;
    const static separator_t localPathSeparator = '/';
#endif

    // returns the internal representation copied into a string buffer, for backward compatibility
    string platformEncoded() const;

    bool empty() const;
    void clear();
    void erase(size_t pos = 0, size_t count = string::npos);
    void truncate(size_t bytePos);
    LocalPath leafName() const;
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
    size_t getLeafnameByteIndex(const FileSystemAccess& fsaccess) const;
    bool backEqual(size_t bytePos, const LocalPath& compareTo) const;
    LocalPath subpathFrom(size_t bytePos) const;
    LocalPath subpathTo(size_t bytePos) const;

    LocalPath insertFilenameCounter(unsigned counter, const FileSystemAccess& fsaccess);

    void ensureWinExtendedPathLenPrefix();

    bool isContainingPathOf(const LocalPath& path, size_t* subpathIndex = nullptr) const;
    bool nextPathComponent(size_t& subpathIndex, LocalPath& component) const;

    // Return a utf8 representation of the LocalPath (fsaccess is used to do the conversion)
    // No escaping or unescaping is done.
    string toPath(const FileSystemAccess& fsaccess) const;
    string toPath() const;

    // Return a utf8 representation of the LocalPath, taking into account that the LocalPath
    // may contain escaped characters that are disallowed for the filesystem.
    // Those characters are converted back (unescaped).  fsaccess is used to do the conversion.
    std::string toName(const FileSystemAccess& fsaccess, FileSystemType fsType) const;

    // Create a Localpath from a utf8 string where no character conversions or escaping is necessary.
    static LocalPath fromPath(const string& path, const FileSystemAccess& fsaccess);

    // Create a LocalPath from a utf8 string, making any character conversions (escaping) necessary
    // for characters that are disallowed on that filesystem.  fsaccess is used to do the conversion.
    static LocalPath fromName(string path, const FileSystemAccess& fsaccess, FileSystemType fsType);

    // Create a LocalPath from a string that was already converted to be appropriate for a local file path.
    static LocalPath fromPlatformEncoded(string localname);
#ifdef WIN32
    static LocalPath fromPlatformEncoded(wstring&& localname);
    wchar_t driveLetter();
#endif

    // Generates a name for a temporary file
    static LocalPath tmpNameLocal(const FileSystemAccess& fsaccess);

    bool operator==(const LocalPath& p) const { return localpath == p.localpath; }
    bool operator!=(const LocalPath& p) const { return localpath != p.localpath; }
    bool operator<(const LocalPath& p) const { return localpath < p.localpath; }
};

struct NameConflict {
    string cloudPath;
    vector<string> clashingCloudNames;
    LocalPath localPath;
    vector<LocalPath> clashingLocalNames;
};

void AddHiddenFileAttribute(mega::LocalPath& path);
void RemoveHiddenFileAttribute(mega::LocalPath& path);

/**
 * @brief
 * Ensures that a path does not end with a separator.
 *
 * @param path
 * An absolute path to normalize.
 *
 * @return
 * A normalized path.
 */
LocalPath NormalizeAbsolute(const LocalPath& path);

/**
 * @brief
 * Ensures that a path does not begin or end with a separator.
 *
 * @param path
 * A relative path to normalize.
 *
 * @return
 * A normalized path.
 */
LocalPath NormalizeRelative(const LocalPath& path);

inline LocalPath operator+(LocalPath& a, LocalPath& b)
{
    LocalPath result = a;
    result.append(b);
    return result;
}

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

// generic host file/directory access interface
struct MEGA_API FileAccess
{
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

    //error code related to the last call to fopen() without parameters
    int errorcode = 0;

    // for files "opened" in nonblocking mode, the current local filename
    LocalPath nonblocking_localname;

    // waiter to notify on filesystem events
    Waiter *waiter;

    // blocking mode: open for reading, writing or reading and writing.
    // This one really does open the file, and openf(), closef() will have no effect
    // If iteratingDir is supplied, this fopen() call must be for the directory entry being iterated by dopen()/dnext()
    virtual bool fopen(const LocalPath&, bool read, bool write, DirAccess* iteratingDir = nullptr, bool ignoreAttributes = false) = 0;

    // nonblocking open: Only prepares for opening.  Actually stats the file/folder, getting mtime, size, type.
    // Call openf() afterwards to actually open it if required.  For folders, returns false with type==FOLDERNODE.
    bool fopen(const LocalPath&);

    // check if a local path is a folder
    bool isfolder(const LocalPath& path);

    // check if local path is a file.
    bool isfile(const LocalPath& path);

    // update localname (only has an effect if operating in by-name mode)
    virtual void updatelocalname(const LocalPath&, bool force) = 0;

    // absolute position read, with NUL padding
    bool fread(string*, unsigned, unsigned, m_off_t);

    // absolute position read to byte buffer
    bool frawread(byte *, unsigned, m_off_t, bool caller_opened = false);

    // After a successful nonblocking fopen(), call openf() to really open the file (by localname)
    // (this is a lazy-type approach in case we don't actually need to open the file after finding out type/size/mtime).
    // If the size or mtime changed, it will fail.
    bool openf();

    // After calling openf(), make sure to close the file again quickly with closef().
    void closef();

    // absolute position write
    virtual bool fwrite(const byte *, unsigned, m_off_t) = 0;

    // Truncate a file.
    virtual bool ftruncate() = 0;

    FileAccess(Waiter *waiter);
    virtual ~FileAccess();

    virtual bool asyncavailable() { return false; }

    AsyncIOContext *asyncfopen(const LocalPath&);

    // non-locking ops: open/close temporary hFile
    bool asyncopenf();
    void asyncclosef();

    AsyncIOContext *asyncfopen(const LocalPath&, bool, bool, m_off_t = 0);
    AsyncIOContext* asyncfread(string*, unsigned, unsigned, m_off_t);
    AsyncIOContext* asyncfwrite(const byte *, unsigned, m_off_t);


protected:
    virtual AsyncIOContext* newasynccontext();
    static void asyncopfinished(void *param);
    bool isAsyncOpened;
    int numAsyncReads;

    // system-specific raw read/open/close to be provided by platform implementation.   fopen / openf / fread etc are implemented by calling these.
    virtual bool sysread(byte *, unsigned, m_off_t) = 0;
    virtual bool sysstat(m_time_t*, m_off_t*) = 0;
    virtual bool sysopen(bool async = false) = 0;
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

struct Notification
{
    dstime timestamp;
    LocalPath path;
    LocalNode* localnode = nullptr;

    Notification() {}
    Notification(dstime ts, const LocalPath& p, LocalNode* ln)
        : timestamp(ts), path(p), localnode(ln)
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

#ifdef ENABLE_SYNC
// filesystem change notification, highly coupled to Syncs and LocalNodes.
struct MEGA_API DirNotify
{
    typedef enum { EXTRA, DIREVENTS, RETRY, NUMQUEUES } notifyqueue;

    // notifyq[EXTRA] is like DIREVENTS, but delays its processing (for network filesystems)
    // notifyq[DIREVENTS] is fed with filesystem changes
    // notifyq[RETRY] receives transient errors that need to be retried
    // Thread safe so that a separate thread can listen for filesystem notifications (for windows for now, maybe more platforms later)
    NotificationDeque notifyq[NUMQUEUES];

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

    virtual void addnotify(LocalNode*, const LocalPath&) { }
    virtual void delnotify(LocalNode*) { }

    void notify(notifyqueue, LocalNode *, LocalPath&&, bool = false);

    // filesystem fingerprint
    virtual fsfp_t fsfingerprint() const;

    // Returns true if the filesystem's IDs are stable (e.g. never change between mounts).
    // This should return false for any FAT filesystem.
    virtual bool fsstableids() const;

    // ignore this (debris folder)
    LocalPath ignore;

    Sync *sync;

    DirNotify(const LocalPath&, const LocalPath&, Sync* s);
    virtual ~DirNotify() {}

    bool empty();
};
#endif

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
    virtual DirAccess* newdiraccess() = 0;

#ifdef ENABLE_SYNC
    // instantiate DirNotify object (default to periodic scanning handler if no
    // notification configured) with given root path
    virtual DirNotify* newdirnotify(const LocalPath&, const LocalPath&, Waiter*, LocalNode* syncroot);
#endif

    // check if character is lowercase hex ASCII
    bool isControlChar(unsigned char c) const;
    bool islocalfscompatible(unsigned char, bool isEscape, FileSystemType = FS_UNKNOWN) const;
    void escapefsincompatible(string*, FileSystemType fileSystemType) const;

    const char *fstypetostring(FileSystemType type) const;
    virtual bool getlocalfstype(const LocalPath& path, FileSystemType& type) const = 0;
    FileSystemType getlocalfstype(const LocalPath& path) const;
    void unescapefsincompatible(string*,FileSystemType) const;

    // convert MEGA path (UTF-8) to local format
    virtual void path2local(const string*, string*) const = 0;
    virtual void local2path(const string*, string*) const = 0;

#if defined(_WIN32)
    // convert MEGA-formatted filename (UTF-8) to local filesystem name
    virtual void local2path(const std::wstring*, string*) const = 0;
    virtual void path2local(const string*, std::wstring*) const = 0;
#endif

    // returns a const char pointer that contains the separator character for the target system
    static const char *getPathSeparator();

    //Normalize UTF-8 string
    static void normalize(string *);

    // generate local temporary file name
    virtual void tmpnamelocal(LocalPath&) const = 0;

    // obtain local secondary name
    virtual bool getsname(const LocalPath&, LocalPath&) const = 0;

    // rename file, overwrite target
    virtual bool renamelocal(const LocalPath&, const LocalPath&, bool = true) = 0;

    // copy file, overwrite target, set mtime
    virtual bool copylocal(LocalPath&, LocalPath&, m_time_t) = 0;

    // delete file
    virtual bool unlinklocal(const LocalPath&) = 0;

    // delete empty directory
    virtual bool rmdirlocal(const LocalPath&) = 0;

    // create directory, optionally hidden
    virtual bool mkdirlocal(const LocalPath&, bool hidden, bool logAlreadyExistsError) = 0;

    // make sure that we stay within the range of timestamps supported by the server data structures (unsigned 32-bit)
    static void captimestamp(m_time_t*);

    // set mtime
    virtual bool setmtimelocal(LocalPath&, m_time_t) = 0;

    // change working directory
    virtual bool chdirlocal(LocalPath&) const = 0;

    // obtain lowercased extension
    virtual bool getextension(const LocalPath&, std::string&) const = 0;

    // check if synchronization is supported for a specific path
    virtual bool issyncsupported(const LocalPath&, bool&, SyncError&, SyncWarning&) = 0;

    // get the absolute path corresponding to a path
    virtual bool expanselocalpath(LocalPath& path, LocalPath& absolutepath) = 0;

    // default permissions for new files
    int getdefaultfilepermissions() { return 0600; }
    void setdefaultfilepermissions(int) { }

    // default permissions for new folder
    int getdefaultfolderpermissions() { return 0700; }
    void setdefaultfolderpermissions(int) { }

    // convenience function for getting filesystem shortnames
    std::unique_ptr<LocalPath> fsShortname(const LocalPath& localpath);

    // set whenever an operation fails due to a transient condition (e.g. locking violation)
    bool transient_error = false;

#ifdef ENABLE_SYNC
    // set whenever there was a global file notification error or permanent failure
    // (this is in addition to the DirNotify-local error)
    bool notifyerr;
    bool notifyfailed;
#endif

    // set whenever an operation fails because the target already exists
    bool target_exists = false;

    // append local operating system version information to string.
    // Set includeArchExtraInfo to know if the app is 32 bit running on 64 bit (on windows, that is via the WOW subsystem)
    virtual void osversion(string*, bool includeArchExtraInfo) const { }

    // append id for stats
    virtual void statsid(string*) const { }

    MegaClient* client = nullptr;

    FileSystemAccess();
    virtual ~FileSystemAccess() { }

    // Get the current working directory.
    virtual bool cwd(LocalPath& path) const = 0;
};

enum FilenameAnomalyType
{
    FILENAME_ANOMALY_NAME_MISMATCH = 0,
    FILENAME_ANOMALY_NAME_RESERVED = 1,
    // This should always be last.
    FILENAME_ANOMALY_NONE
}; // FilenameAnomalyType

class FilenameAnomalyReporter
{
public:
    virtual ~FilenameAnomalyReporter() { };

    virtual void anomalyDetected(FilenameAnomalyType type, const string& localPath, const string& remotePath) = 0;
}; // FilenameAnomalyReporter

bool isCaseInsensitive(const FileSystemType type);

int compareUtf(const string&, bool unescaping1, const string&, bool unescaping2, bool caseInsensitive);
int compareUtf(const string&, bool unescaping1, const LocalPath&, bool unescaping2, bool caseInsensitive);
int compareUtf(const LocalPath&, bool unescaping1, const string&, bool unescaping2, bool caseInsensitive);
int compareUtf(const LocalPath&, bool unescaping1, const LocalPath&, bool unescaping2, bool caseInsensitive);

// Same as above except case insensitivity is determined by build platform.
int platformCompareUtf(const string&, bool unescape1, const string&, bool unescape2);
int platformCompareUtf(const string&, bool unescape1, const LocalPath&, bool unescape2);
int platformCompareUtf(const LocalPath&, bool unescape1, const string&, bool unescape2);
int platformCompareUtf(const LocalPath&, bool unescape1, const LocalPath&, bool unescape2);

// Returns true if name is a reserved file name.
//
// On Windows, a reserved file name is:
//   - AUX, COM[0-9], CON, LPT[0-9], NUL or PRN.
bool isReservedName(const string& name, nodetype_t type = FILENODE);

// Checks if there is a filename anomaly.
//
// @param localPath
// The local path of the file in question.
//
// @param node
// The remote node representing the file in question.
//
// @return
// FILENAME_ANOMALY_NAME_MISMATCH
// - If the local and remote file name differs.
// FILENAME_ANOMALY_NAME_RESERVED
// - If the remote file name is reserved.
// FILENAME_ANOMALY_NONE
// - If no anomalies were detected.
FilenameAnomalyType isFilenameAnomaly(const LocalPath& localPath, const string& remoteName, nodetype_t type = FILENODE);
FilenameAnomalyType isFilenameAnomaly(const LocalPath& localPath, const Node* node);
#ifdef ENABLE_SYNC
FilenameAnomalyType isFilenameAnomaly(const LocalNode& node);
#endif

} // namespace

#endif
