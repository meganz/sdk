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

#if defined (__linux__) && !defined (__ANDROID__)
#include <linux/magic.h>
#endif

#if defined (__linux__) || defined (__ANDROID__) // __ANDROID__ is always included in __linux__
#include <sys/vfs.h>
#elif defined  (__APPLE__) || defined (USE_IOS)
#include <sys/mount.h>
#include <sys/param.h>
#elif defined(_WIN32) || defined(WINDOWS_PHONE)
#include <winsock2.h>
#include <Windows.h>
#endif

#include "types.h"
#include "utils.h"
#include "waiter.h"

#if defined (__linux__) && !defined (__ANDROID__)
// Define magic constants (for linux), in case they are not defined in headers
#ifndef HFS_SUPER_MAGIC
#define HFS_SUPER_MAGIC 0x4244
#endif

#ifndef NTFS_SB_MAGIC
#define NTFS_SB_MAGIC   0x5346544e
#endif

#elif defined (__ANDROID__)
// Define magic constants (for Android), in case they are not defined in headers
#ifndef SDCARDFS_SUPER_MAGIC
#define SDCARDFS_SUPER_MAGIC 0x5DCA2DF5
#endif

#ifndef FUSEBLK_SUPER_MAGIC
#define FUSEBLK_SUPER_MAGIC  0x65735546
#endif

#ifndef FUSECTL_SUPER_MAGIC
#define FUSECTL_SUPER_MAGIC  0x65735543
#endif

#ifndef F2FS_SUPER_MAGIC
#define F2FS_SUPER_MAGIC 0xF2F52010
#endif
#endif

namespace mega {

// Enumeration for filesystem families
enum FileSystemType {FS_UNKNOWN = -1, FS_APFS = 0, FS_HFS = 1, FS_EXT = 2, FS_FAT32 = 3,
                     FS_EXFAT = 4, FS_NTFS = 5, FS_FUSE = 6, FS_SDCARDFS = 7, FS_F2FS = 8};

// generic host filesystem node ID interface
struct MEGA_API FsNodeId
{
    virtual bool isequalto(FsNodeId*) = 0;
};

typedef void (*asyncfscallback)(void *);

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

    AsyncIOContext();
    virtual ~AsyncIOContext();
    virtual void finish();

    // results
    asyncfscallback userCallback;
    void *userData;
    bool finished;
    bool failed;
    bool retry;

    // parameters
    int op;
    int access;
    m_off_t pos;
    unsigned len;
    unsigned pad;
    byte *buffer;
    Waiter *waiter;
    FileAccess *fa;
};



// LocalPath represents a path in the local filesystem, and wraps up common operations in a convenient fashion.
// On mac/linux, local paths are in utf8 but in windows local paths are utf16, that is wrapped up here.

struct MEGA_API FileSystemAccess;
class MEGA_API LocalPath;

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
    std::string localpath;

    friend class ScopedLengthRestore;
    size_t getLength() { return localpath.size(); }
    void setLength(size_t length) { localpath.resize(length); }

public:

    LocalPath() {}
    explicit LocalPath(std::string&& s) : localpath(std::move(s)) {}

    std::string* editStringDirect();
    const std::string* editStringDirect() const;
    bool empty() const;
    void clear() { localpath.clear(); }
    void erase(size_t pos = 0, size_t count = std::string::npos) { localpath.erase(pos, count); }
    void truncate(size_t bytePos) { localpath.resize(bytePos); }
    size_t lastpartlocal(const FileSystemAccess& fsaccess) const;
    void append(const LocalPath& additionalPath);
    void appendWithSeparator(const LocalPath& additionalPath, bool separatorAlways, const std::string& localseparator);
    void prependWithSeparator(const LocalPath& additionalPath, const std::string& localseparator);
    void trimNonDriveTrailingSeparator(const FileSystemAccess& fsaccess);
    bool findNextSeparator(size_t& separatorBytePos, const FileSystemAccess& fsaccess) const;
    bool findPrevSeparator(size_t& separatorBytePos, const FileSystemAccess& fsaccess) const;
    bool endsInSeparator(const FileSystemAccess& fsaccess) const;

    // get the index of the leaf name.  A trailing separator is considered part of the leaf.
    size_t getLeafnameByteIndex(const FileSystemAccess& fsaccess) const;
    bool backEqual(size_t bytePos, const LocalPath& compareTo) const;
    LocalPath subpathFrom(size_t bytePos) const;
    std::string substrTo(size_t bytePos) const;

    void ensureWinExtendedPathLenPrefix();

    bool isContainingPathOf(const LocalPath& path, const FileSystemAccess& fsaccess);

    // Return the last part of the local path.
    LocalPath lastPart(const FileSystemAccess& fsAccess) const;

    // Return a utf8 representation of the LocalPath (fsaccess is used to do the conversion)
    // No escaping or unescaping is done.
    std::string toPath(const FileSystemAccess& fsaccess) const;
    
    // Return a utf8 representation of the LocalPath, taking into account that the LocalPath 
    // may contain escaped characters that are disallowed for the filesystem.
    // Those characters are converted back (unescaped).  fsaccess is used to do the conversion.
    std::string toName(const FileSystemAccess& fsaccess, FileSystemType fsType = FS_UNKNOWN) const;

    // Create a Localpath from a utf8 string where no character conversions or escaping is necessary.
    static LocalPath fromPath(const std::string& path, const FileSystemAccess& fsaccess);

    // Create a LocalPath from a utf8 string, making any character conversions (escaping) necessary
    // for characters that are disallowed on that filesystem.  fsaccess is used to do the conversion.
    static LocalPath fromName(std::string path, const FileSystemAccess& fsaccess, FileSystemType fsType);

    // Create a LocalPath from a string that was already converted to be appropriate for a local file path.
    static LocalPath fromLocalname(std::string localname);

    // Generates a name for a temporary file
    static LocalPath tmpNameLocal(const FileSystemAccess& fsaccess);

    bool operator==(const LocalPath& p) const { return localpath == p.localpath; }
    bool operator!=(const LocalPath& p) const { return localpath != p.localpath; }
    bool operator<(const LocalPath& p) const { return localpath < p.localpath; }
};

inline LocalPath operator+(LocalPath& a, LocalPath& b)
{
    LocalPath result = a;
    result.append(b);
    return result;
}

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
    virtual bool fopen(LocalPath&, bool read, bool write, DirAccess* iteratingDir = nullptr) = 0;

    // nonblocking open: Only prepares for opening.  Actually stats the file/folder, getting mtime, size, type.
    // Call openf() afterwards to actually open it if required.  For folders, returns false with type==FOLDERNODE.
    bool fopen(LocalPath&);

    // check if a local path is a folder
    bool isfolder(LocalPath&);

    // update localname (only has an effect if operating in by-name mode)
    virtual void updatelocalname(LocalPath&) = 0;

    // absolute position read, with NUL padding
    bool fread(string *, unsigned, unsigned, m_off_t);

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

    FileAccess(Waiter *waiter);
    virtual ~FileAccess();

    virtual bool asyncavailable() { return false; }

    AsyncIOContext *asyncfopen(LocalPath&);

    // non-locking ops: open/close temporary hFile
    bool asyncopenf();
    void asyncclosef();

    AsyncIOContext *asyncfopen(LocalPath&, bool, bool, m_off_t = 0);
    AsyncIOContext* asyncfread(string *, unsigned, unsigned, m_off_t);
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

struct MEGA_API InputStreamAccess
{
    virtual m_off_t size() = 0;
    virtual bool read(byte *, unsigned) = 0;
    virtual ~InputStreamAccess() { }
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
    LocalNode* localnode;
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

// generic filesystem change notification
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

    virtual void addnotify(LocalNode*, string*) { }
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

    DirNotify(const LocalPath&, const LocalPath&);
    virtual ~DirNotify() {}
};

// generic host filesystem access interface
struct MEGA_API FileSystemAccess : public EventTrigger
{
    // local path separator, e.g. "/"
    string localseparator;

    // waiter to notify on filesystem events
    Waiter *waiter;

    // indicate error reports are not necessary on this call as it'll be retried in a moment if there is a continuing problem
    bool skip_errorreport;

    /**
     * @brief instantiate FileAccess object
     * @param followSymLinks whether symlinks should be followed when opening a path (default: true)
     * @return
     */
    virtual std::unique_ptr<FileAccess> newfileaccess(bool followSymLinks = true) = 0;

    // instantiate DirAccess object
    virtual DirAccess* newdiraccess() = 0;

    // instantiate DirNotify object (default to periodic scanning handler if no
    // notification configured) with given root path
    virtual DirNotify* newdirnotify(LocalPath&, LocalPath&, Waiter*);

    // check if character is lowercase hex ASCII
    bool islchex(char) const;
    bool isControlChar(unsigned char c) const;
    bool islocalfscompatible(unsigned char, bool isEscape, FileSystemType = FS_UNKNOWN) const;
    void escapefsincompatible(string*, FileSystemType fileSystemType) const;

    FileSystemType getFilesystemType(const LocalPath& dstPath) const;
    const char *fstypetostring(FileSystemType type) const;
    FileSystemType getlocalfstype(const LocalPath& dstPath) const;
    void unescapefsincompatible(string*,FileSystemType) const;

    // convert MEGA path (UTF-8) to local format
    virtual void path2local(const string*, string*) const = 0;
    virtual void local2path(const string*, string*) const = 0;

    // convert MEGA-formatted filename (UTF-8) to local filesystem name; escape
    // forbidden characters using urlencode
    void local2name(string*, FileSystemType) const;

    // convert local path to MEGA format (UTF-8) with unescaping
    void name2local(string*, FileSystemType) const;

    // returns a const char pointer that contains the separator character for the target system
    static const char *getPathSeparator();

    //Normalize UTF-8 string
    void normalize(string *) const;

    // generate local temporary file name
    virtual void tmpnamelocal(LocalPath&) const = 0;

    // obtain local secondary name
    virtual bool getsname(LocalPath&, LocalPath&) const = 0;

    // rename file, overwrite target
    virtual bool renamelocal(LocalPath&, LocalPath&, bool = true) = 0;

    // copy file, overwrite target, set mtime
    virtual bool copylocal(LocalPath&, LocalPath&, m_time_t) = 0;

    // delete file
    virtual bool unlinklocal(LocalPath&) = 0;

    // delete empty directory
    virtual bool rmdirlocal(LocalPath&) = 0;

    // create directory, optionally hidden
    virtual bool mkdirlocal(LocalPath&, bool = false) = 0;

    // make sure that we stay within the range of timestamps supported by the server data structures (unsigned 32-bit)
    static void captimestamp(m_time_t*);
    
    // set mtime
    virtual bool setmtimelocal(LocalPath&, m_time_t) = 0;

    // change working directory
    virtual bool chdirlocal(LocalPath&) const = 0;

    // locate byte offset of last path component
    virtual size_t lastpartlocal(const string*) const = 0;

    // obtain lowercased extension
    virtual bool getextension(const LocalPath&, char*, size_t) const = 0;

    // check if synchronization is supported for a specific path
    virtual bool issyncsupported(LocalPath&, bool* = NULL) { return true; }

    // add notification (has to be called for all directories in tree for full crossplatform support)
    virtual void addnotify(LocalNode*, string*) { }

    // delete notification
    virtual void delnotify(LocalNode*) { }

    // get the absolute path corresponding to a path
    virtual bool expanselocalpath(LocalPath& path, LocalPath& absolutepath) = 0;

    // default permissions for new files
    int getdefaultfilepermissions() { return 0600; }
    void setdefaultfilepermissions(int) { }

    // default permissions for new folder
    int getdefaultfolderpermissions() { return 0700; }
    void setdefaultfolderpermissions(int) { }

    // convenience function for getting filesystem shortnames
    std::unique_ptr<LocalPath> fsShortname(LocalPath& localpath);

    // set whenever an operation fails due to a transient condition (e.g. locking violation)
    bool transient_error;
    
    // set whenever there was a global file notification error or permanent failure
    // (this is in addition to the DirNotify-local error)
    bool notifyerr;
    bool notifyfailed;

    // set whenever an operation fails because the target already exists
    bool target_exists;

    // append local operating system version information to string.
    // Set includeArchExtraInfo to know if the app is 32 bit running on 64 bit (on windows, that is via the WOW subsystem)
    virtual void osversion(string*, bool includeArchExtraInfo) const { }

    // append id for stats
    virtual void statsid(string*) const { }

    MegaClient* client;

    FileSystemAccess();
    virtual ~FileSystemAccess() { }
};
} // namespace

#endif
