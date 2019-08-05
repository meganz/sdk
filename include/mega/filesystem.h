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

#include "types.h"
#include "waiter.h"

namespace mega {
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

// generic host file/directory access interface
struct MEGA_API FileAccess
{
    // file size
    m_off_t size;

    // mtime of a file opened for reading
    m_time_t mtime;

    // local filesystem record id (survives renames & moves)
    handle fsid;
    bool fsidvalid;

    // type of opened path
    nodetype_t type;

    // if the open failed, retry indicates a potentially transient reason
    bool retry;

    //error code related to the last call to fopen() without parameters
    int errorcode;

    // for files "opened" in nonblocking mode, the current local filename
    string localname;

    // waiter to notify on filesystem events
    Waiter *waiter;

    // open for reading, writing or reading and writing
    virtual bool fopen(string*, bool, bool) = 0;

    // open by name only
    bool fopen(string*);

    // check if a local path is a folder
    bool isfolder(string*);

    // update localname (only has an effect if operating in by-name mode)
    virtual void updatelocalname(string*) = 0;

    // absolute position read, with NUL padding
    bool fread(string *, unsigned, unsigned, m_off_t);

    // absolute position read to byte buffer
    virtual bool frawread(byte *, unsigned, m_off_t);

    // non-locking ops: open/close temporary hFile
    bool openf();
    void closef();

    // absolute position write
    virtual bool fwrite(const byte *, unsigned, m_off_t) = 0;

    // system-specific raw read/open/close
    virtual bool sysread(byte *, unsigned, m_off_t) = 0;
    virtual bool sysstat(m_time_t*, m_off_t*) = 0;
    virtual bool sysopen(bool async = false) = 0;
    virtual void sysclose() = 0;

    FileAccess(Waiter *waiter);
    virtual ~FileAccess();

    virtual bool asyncavailable() { return false; }

    AsyncIOContext *asyncfopen(string *);

    // non-locking ops: open/close temporary hFile
    bool asyncopenf();
    void asyncclosef();

    AsyncIOContext *asyncfopen(string *, bool, bool, m_off_t = 0);
    virtual void asyncsysopen(AsyncIOContext*);

    AsyncIOContext* asyncfread(string *, unsigned, unsigned, m_off_t);
    virtual void asyncsysread(AsyncIOContext*);

    AsyncIOContext* asyncfwrite(const byte *, unsigned, m_off_t);
    virtual void asyncsyswrite(AsyncIOContext*);


protected:
    virtual AsyncIOContext* newasynccontext();
    static void asyncopfinished(void *param);
    bool isAsyncOpened;
    int numAsyncReads;
};

struct MEGA_API InputStreamAccess
{
    virtual m_off_t size() = 0;
    virtual bool read(byte *, unsigned) = 0;
    virtual ~InputStreamAccess() { }
};

// generic host directory enumeration
struct MEGA_API DirAccess
{
    // open for scanning
    virtual bool dopen(string*, FileAccess*, bool) = 0;

    // get next record
    virtual bool dnext(string*, string*, bool = true, nodetype_t* = NULL) = 0;

    virtual ~DirAccess() { }
};

// generic filesystem change notification
struct MEGA_API DirNotify
{
    typedef enum { EXTRA, DIREVENTS, RETRY, NUMQUEUES } notifyqueue;

    // notifyq[EXTRA] is like DIREVENTS, but delays its processing (for network filesystems)
    // notifyq[DIREVENTS] is fed with filesystem changes
    // notifyq[RETRY] receives transient errors that need to be retried
    notify_deque notifyq[NUMQUEUES];

    // set if no notification available on this platform or a permanent failure
    // occurred
    int failed;

    // reason of the permanent failure of filesystem notifications
    string failreason;

    // set if a temporary error occurred
    int error;

    // base path
    string localbasepath;

    virtual void addnotify(LocalNode*, string*) { }
    virtual void delnotify(LocalNode*) { }

    void notify(notifyqueue, LocalNode *, const char*, size_t, bool = false);

    // filesystem fingerprint
    virtual fsfp_t fsfingerprint() const;

    // Returns true if the filesystem's IDs are stable (e.g. never change between mounts).
    // This should return false for any FAT filesystem.
    virtual bool fsstableids() const;

    // ignore this
    string ignore;

    Sync *sync;

    DirNotify(string*, string*);
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

    // instantiate FileAccess object
    virtual FileAccess* newfileaccess() = 0;

    // instantiate DirAccess object
    virtual DirAccess* newdiraccess() = 0;

    // instantiate DirNotify object (default to periodic scanning handler if no
    // notification configured) with given root path
    virtual DirNotify* newdirnotify(string*, string*);

    // check if character is lowercase hex ASCII
    bool islchex(char) const;
    bool islocalfscompatible(unsigned char) const;
    void escapefsincompatible(string*) const;
    void unescapefsincompatible(string*) const;

    // convert MEGA path (UTF-8) to local format
    virtual void path2local(string*, string*) const = 0;
    virtual void local2path(string*, string*) const = 0;

    // convert MEGA-formatted filename (UTF-8) to local filesystem name; escape
    // forbidden characters using urlencode
    virtual void local2name(string*) const;

    // convert local path to MEGA format (UTF-8) with unescaping
    void name2local(string*) const;

    //Normalize UTF-8 string
    void normalize(string *) const;

    // generate local temporary file name
    virtual void tmpnamelocal(string*) const = 0;

    // obtain local secondary name
    virtual bool getsname(string*, string*) const = 0;

    // rename file, overwrite target
    virtual bool renamelocal(string*, string*, bool = true) = 0;

    // copy file, overwrite target, set mtime
    virtual bool copylocal(string*, string*, m_time_t) = 0;

    // delete file
    virtual bool unlinklocal(string*) = 0;

    // delete empty directory
    virtual bool rmdirlocal(string*) = 0;

    // create directory, optionally hidden
    virtual bool mkdirlocal(string*, bool = false) = 0;

    // make sure that we stay within the range of timestamps supported by the server data structures (unsigned 32-bit)
    static void captimestamp(m_time_t*);
    
    // set mtime
    virtual bool setmtimelocal(string *, m_time_t) = 0;

    // change working directory
    virtual bool chdirlocal(string*) const = 0;

    // locate byte offset of last path component
    virtual size_t lastpartlocal(string*) const = 0;

    // obtain lowercased extension
    virtual bool getextension(string*, char*, size_t) const = 0;

    // check if synchronization is supported for a specific path
    virtual bool issyncsupported(string*, bool* = NULL) { return true; }

    // add notification (has to be called for all directories in tree for full crossplatform support)
    virtual void addnotify(LocalNode*, string*) { }

    // delete notification
    virtual void delnotify(LocalNode*) { }

    // get the absolute path corresponding to a path
    virtual bool expanselocalpath(string *path, string *absolutepath) = 0;

    // default permissions for new files
    int getdefaultfilepermissions() { return 0600; }
    void setdefaultfilepermissions(int) { }

    // default permissions for new folder
    int getdefaultfolderpermissions() { return 0700; }
    void setdefaultfolderpermissions(int) { }

    // set whenever an operation fails due to a transient condition (e.g. locking violation)
    bool transient_error;
    
    // set whenever there was a global file notification error or permanent failure
    // (this is in addition to the DirNotify-local error)
    bool notifyerr;
    bool notifyfailed;

    // set whenever an operation fails because the target already exists
    bool target_exists;

    // append local operating system version information to string
    virtual void osversion(string*) const { }

    // append id for stats
    virtual void statsid(string*) const { }

    MegaClient* client;

    FileSystemAccess();
    virtual ~FileSystemAccess() { }
};
} // namespace

#endif
