/**
 * @file mega/types.h
 * @brief Mega SDK types and includes
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

#ifndef MEGA_TYPES_H
#define MEGA_TYPES_H 1

#ifdef _MSC_VER
#if MEGA_LINKED_AS_SHARED_LIBRARY
 #define MEGA_API __declspec(dllimport)
#elif MEGA_CREATE_SHARED_LIBRARY
 #define MEGA_API __declspec(dllexport)
#endif
#endif

#ifndef MEGA_API
 #define MEGA_API
#endif

// it needs to be reviewed that serialization/unserialization is not relying on this
typedef char __static_check_01__[sizeof(bool) == sizeof(char) ? 1 : -1];
// if your build fails here, please contact MEGA developers

// platform-specific includes and defines
#ifdef _WIN32
#include "mega/win32/megasys.h"
#else
#include "mega/posix/megasys.h"
#endif

#ifdef USE_CRYPTOPP
#include <cryptopp/config.h> // so we can test CRYPTO_VERSION below
#endif

// signed 64-bit generic offset
typedef int64_t m_off_t;

// opaque filesystem fingerprint
typedef uint64_t fsfp_t;

namespace mega {
// within ::mega namespace, byte is unsigned char (avoids ambiguity when std::byte from c++17 and perhaps other defined ::byte are available)
#if defined(USE_CRYPTOPP) && (CRYPTOPP_VERSION >= 600) && ((__cplusplus >= 201103L) || (__RPCNDR_H_VERSION__ == 500))
using byte = CryptoPP::byte;
#elif __RPCNDR_H_VERSION__ != 500
typedef unsigned char byte;

#endif
}

#ifdef USE_CRYPTOPP
#include "mega/crypto/cryptopp.h"
#else
#include "megacrypto.h"
#endif

#include "mega/crypto/sodium.h"

#include <memory>
#include <string>
#include <chrono>
#include <mutex>

namespace mega {

// import these select types into the namespace directly, to avoid adding std::byte from c++17
using std::string;
using std::map;
using std::set;
using std::list;
using std::vector;
using std::pair;
using std::multimap;
using std::deque;
using std::multiset;
using std::queue;
using std::streambuf;
using std::tuple;
using std::ostringstream;
using std::unique_ptr;
using std::shared_ptr;
using std::weak_ptr;
using std::move;
using std::mutex;
using std::recursive_mutex;
using std::lock_guard;

#ifdef WIN32
using std::wstring;
#endif

// forward declaration
struct AttrMap;
class BackoffTimer;
class Command;
class CommandPubKeyRequest;
struct DirectRead;
struct DirectReadNode;
struct DirectReadSlot;
struct FileAccess;
struct FileAttributeFetch;
struct FileAttributeFetchChannel;
struct FileFingerprint;
struct FileFingerprintCmp;
struct HttpReq;
struct GenericHttpReq;
struct HttpReqCommandPutFA;
struct LocalNode;
class MegaClient;
struct NewNode;
struct Node;
struct NodeCore;
class PubKeyAction;
class Request;
struct Transfer;
class TreeProc;
class LocalTreeProc;
struct User;
struct Waiter;
struct Proxy;
struct PendingContactRequest;
class TransferList;
struct Achievement;
class SyncConfig;

namespace UserAlert
{
    struct Base;
}
class AuthRing;

#define EOO 0

// Our own version of time_t which we can be sure is 64 bit.
// Utils.h has functions m_time() and so on corresponding to time() which help us to use this type and avoid arithmetic overflow when working with time_t on systems where it's 32-bit
typedef int64_t m_time_t;

// monotonously increasing time in deciseconds
typedef uint32_t dstime;

#define NEVER (~(dstime)0)
#define EVER(ds) ((ds+1))

#define STRINGIFY(x) # x
#define TOSTRING(x) STRINGIFY(x)

// HttpReq states
typedef enum { REQ_READY, REQ_GET_URL, REQ_PREPARED, REQ_UPLOAD_PREPARED_BUT_WAIT,
               REQ_ENCRYPTING, REQ_DECRYPTING, REQ_DECRYPTED,
               REQ_INFLIGHT,
               REQ_SUCCESS, REQ_FAILURE, REQ_DONE, REQ_ASYNCIO,
               } reqstatus_t;

typedef enum { USER_HANDLE, NODE_HANDLE } targettype_t;

typedef enum { METHOD_POST, METHOD_GET, METHOD_NONE} httpmethod_t;

typedef enum { REQ_BINARY, REQ_JSON } contenttype_t;

// new node source types
typedef enum { NEW_NODE, NEW_PUBLIC, NEW_UPLOAD } newnodesource_t;

class chunkmac_map;

/**
 * @brief Declaration of API error codes.
 */
typedef enum ErrorCodes : int
{
    API_OK = 0,                     ///< Everything OK.
    API_EINTERNAL = -1,             ///< Internal error.
    API_EARGS = -2,                 ///< Bad arguments.
    API_EAGAIN = -3,                ///< Request failed, retry with exponential backoff.
    DAEMON_EFAILED = -4,            ///< If returned from the daemon: EFAILED
    API_ERATELIMIT = -4,            ///< If returned from the API: Too many requests, slow down.
    API_EFAILED = -5,               ///< Request failed permanently.  This one is only produced by the API, only per command (not batch level)
    API_ETOOMANY = -6,              ///< Too many requests for this resource.
    API_ERANGE = -7,                ///< Resource access out of range.
    API_EEXPIRED = -8,              ///< Resource expired.
    API_ENOENT = -9,                ///< Resource does not exist.
    API_ECIRCULAR = -10,            ///< Circular linkage.
    API_EACCESS = -11,              ///< Access denied.
    API_EEXIST = -12,               ///< Resource already exists.
    API_EINCOMPLETE = -13,          ///< Request incomplete.
    API_EKEY = -14,                 ///< Cryptographic error.
    API_ESID = -15,                 ///< Bad session ID.
    API_EBLOCKED = -16,             ///< Resource administratively blocked.
    API_EOVERQUOTA = -17,           ///< Quota exceeded.
    API_ETEMPUNAVAIL = -18,         ///< Resource temporarily not available.
    API_ETOOMANYCONNECTIONS = -19,  ///< Too many connections on this resource.
    API_EWRITE = -20,               ///< File could not be written to (or failed post-write integrity check)
    API_EREAD = -21,                ///< File could not be read from (or changed unexpectedly during reading)
    API_EAPPKEY = -22,              ///< Invalid or missing application key.
    API_ESSL = -23,                 ///< SSL verification failed
    API_EGOINGOVERQUOTA = -24,      ///< Not enough quota
    API_EMFAREQUIRED = -26,         ///< Multi-factor authentication required
    API_EMASTERONLY = -27,          ///< Access denied for sub-users (only for business accounts)
    API_EBUSINESSPASTDUE = -28,     ///< Business account expired
    API_EPAYWALL = -29,             ///< Over Disk Quota Paywall
    LOCAL_ENOSPC = -1000,           ///< Insufficient space
} error;

class Error
{
public:
    typedef enum
    {
        USER_ETD_UNKNOWN = -1,
        USER_COPYRIGHT_SUSPENSION = 4,  // Account suspended by copyright
        USER_ETD_SUSPENSION = 7, // represents an ETD/ToS 'severe' suspension level
    } UserErrorCode;

    typedef enum
    {
        LINK_UNKNOWN = -1,
        LINK_UNDELETED = 0,  // Link is undeleted
        LINK_DELETED_DOWN = 1, // Link is deleted or down
        LINK_DOWN_ETD = 2,  // Link is down due to an ETD specifically
    } LinkErrorCode;

    Error(error err = API_EINTERNAL)
        : mError(err)
    { }

    void setErrorCode(error err)
    {
        mError = err;
    }

    void setUserStatus(int64_t u) { mUserStatus = u; }
    void setLinkStatus(int64_t l) { mLinkStatus = l; }
    bool hasExtraInfo() const { return mUserStatus != USER_ETD_UNKNOWN || mLinkStatus != LINK_UNKNOWN; }
    int64_t getUserStatus() const { return mUserStatus; }
    int64_t getLinkStatus() const { return mLinkStatus; }
    operator error() const { return mError; }

private:
    error mError = API_EINTERNAL;
    int64_t mUserStatus = USER_ETD_UNKNOWN;
    int64_t mLinkStatus = LINK_UNKNOWN;
};

// returned by loggedin()
typedef enum { NOTLOGGEDIN = 0, EPHEMERALACCOUNT, CONFIRMEDACCOUNT, FULLACCOUNT, EPHEMERALACCOUNTPLUSPLUS } sessiontype_t;

// node/user handles are 8-11 base64 characters, case sensitive, and thus fit
// in a 64-bit int
typedef uint64_t handle;

class NodeHandle
{
    // Handles of nodes are only 6 bytes.
    // This class helps avoid issues when we don't save/restore the top 2 bytes when using an 8 byte uint64 to represent it
    uint64_t h = 0xFFFFFFFFFFFFFFFF;
public:
    bool isUndef() const { return (h & 0xFFFFFFFFFFFF) == 0xFFFFFFFFFFFF; }
    void setUndef() { h = 0xFFFFFFFFFFFFFFFF; }
    NodeHandle& set6byte(uint64_t n) { h = n; assert((n & 0xFFFF000000000000) == 0 || n == 0xFFFFFFFFFFFFFFFF); return *this; }
    NodeHandle& setImpossibleValue(uint64_t n) { h = n; return *this; }
    bool eq(NodeHandle b) const { return (h & 0xFFFFFFFFFFFF) == (b.h & 0xFFFFFFFFFFFF); }
    bool eq(handle b) const { return (h & 0xFFFFFFFFFFFF) == (b & 0xFFFFFFFFFFFF); }
    bool ne(handle b) const { return (h & 0xFFFFFFFFFFFF) != (b & 0xFFFFFFFFFFFF); }
    bool ne(NodeHandle b) const { return (h & 0xFFFFFFFFFFFF) != (b.h & 0xFFFFFFFFFFFF); }
    bool operator<(const NodeHandle& rhs) const { return h < rhs.h; }
    handle as8byte() const { return isUndef() ? 0xFFFFFFFFFFFFFFFF : (h & 0xFFFFFFFFFFFF); }
};

inline bool operator==(NodeHandle a, NodeHandle b) { return a.eq(b); }
inline bool operator==(NodeHandle a, handle b) { return a.eq(b); }
inline bool operator!=(NodeHandle a, handle b) { return a.ne(b); }
inline bool operator!=(NodeHandle a, NodeHandle b) { return a.ne(b); }
std::ostream& operator<<(std::ostream&, NodeHandle h);

struct UploadHandle
{
    handle h = 0xFFFFFFFFFFFFFFFF;
    UploadHandle() {}
    UploadHandle(handle uh) : h(uh) { assert( (h & 0xFFFF000000000000) != 0 ); }

    // generate upload handle for the next upload
    UploadHandle next();

    bool isUndef() const { return h == 0xFFFFFFFFFFFFFFFF; }

    bool eq(UploadHandle b) const { return h == b.h; }
    bool operator<(const UploadHandle& rhs) const { return h < rhs.h; }
};

inline bool operator==(UploadHandle a, UploadHandle b) { return a.eq(b); }

class NodeOrUploadHandle
{
    handle h = 0xFFFFFFFFFFFFFFFF;
    bool mIsNodeHandle = true;

public:
    NodeOrUploadHandle() {}
    explicit NodeOrUploadHandle(NodeHandle nh) : h(nh.as8byte()), mIsNodeHandle(true) {}
    explicit NodeOrUploadHandle(UploadHandle uh) : h(uh.h), mIsNodeHandle(false) {}

    NodeHandle nodeHandle() { return mIsNodeHandle ? NodeHandle().set6byte(h) : NodeHandle(); }
    UploadHandle uploadHandle() { return mIsNodeHandle ? UploadHandle() : UploadHandle(h); }

    bool isNodeHandle() { return mIsNodeHandle; }
    bool isUndef() const { return h == 0xFFFFFFFFFFFFFFFF; }

    bool eq(NodeOrUploadHandle b) const { return h == b.h && mIsNodeHandle == b.mIsNodeHandle; }
    bool operator<(const NodeOrUploadHandle& rhs) const { return h < rhs.h || (h == rhs.h && int(mIsNodeHandle) < int(rhs.mIsNodeHandle)); }
};

inline bool operator==(NodeOrUploadHandle a, NodeOrUploadHandle b) { return a.eq(b); }

// (can use unordered_set if available)
typedef set<handle> handle_set;

// file attribute type
typedef uint16_t fatype;

// list of files
typedef list<struct File*> file_list;

// node types:
typedef enum {

    // these first two are for sync rework, and should not be used before that branch is merged (directoyScan is from sync rework)
    TYPE_DONOTSYNC = -3,
    TYPE_SPECIAL = -2,

    TYPE_UNKNOWN = -1,
    FILENODE = 0,    // FILE - regular file nodes
    FOLDERNODE,      // FOLDER - regular folder nodes
    ROOTNODE,        // ROOT - the cloud drive root node
    VAULTNODE,       // VAULT - vault, for "My backups" and other special folders
    RUBBISHNODE      // RUBBISH - rubbish bin
} nodetype_t;


// MimeType_t maps to file extensionse declared at Node
typedef enum { MIME_TYPE_UNKNOWN    = 0,
               MIME_TYPE_PHOTO      = 1,    // photoExtensions, photoRawExtensions, photoImageDefExtension
               MIME_TYPE_AUDIO      = 2,    // audioExtensions longAudioExtension
               MIME_TYPE_VIDEO      = 3,    // videoExtensions
               MIME_TYPE_DOCUMENT   = 4     // documentExtensions
             } MimeType_t;


typedef enum { LBL_UNKNOWN = 0, LBL_RED = 1, LBL_ORANGE = 2, LBL_YELLOW = 3, LBL_GREEN = 4,
               LBL_BLUE = 5, LBL_PURPLE = 6, LBL_GREY = 7, } nodelabel_t;

// node type key lengths
const int FILENODEKEYLENGTH = 32;
const int FOLDERNODEKEYLENGTH = 16;

// Max nodes per putnodes command
const unsigned MAXNODESUPLOAD = 1000;
typedef union {
    std::array<byte, FILENODEKEYLENGTH> bytes;
    struct {
        std::array<byte, FOLDERNODEKEYLENGTH> key;
        union {
            std::array<byte, 8> iv_bytes;
            uint64_t iv_u64;
        };
        union {
            std::array<byte, 8> crc_bytes;
            uint64_t crc_u64;
        };
    };
} FileNodeKey;

const int UPLOADTOKENLEN = 36;

typedef std::array<byte, UPLOADTOKENLEN> UploadToken;

// persistent resource cache storage
class Cacheable
{
public:
    virtual ~Cacheable() = default;

    virtual bool serialize(string*) = 0;

    uint32_t dbid = 0;
    bool notified = false;
};

// numeric representation of string (up to 8 chars)
typedef uint64_t nameid;

// access levels:
// RDONLY - cannot add, rename or delete
// RDWR - cannot rename or delete
// FULL - all operations that do not require ownership permitted
// OWNER - node is in caller's ROOT, VAULT or RUBBISH trees
typedef enum { ACCESS_UNKNOWN = -1, RDONLY = 0, RDWR, FULL, OWNER, OWNERPRELOGIN } accesslevel_t;

// operations for outgoing pending contacts
typedef enum { OPCA_ADD = 0, OPCA_DELETE, OPCA_REMIND} opcactions_t;
// operations for incoming pending contacts
typedef enum { IPCA_ACCEPT = 0, IPCA_DENY, IPCA_IGNORE} ipcactions_t;


typedef vector<Node*> node_vector;

// contact visibility:
// HIDDEN - not shown
// VISIBLE - shown
typedef enum { VISIBILITY_UNKNOWN = -1, HIDDEN = 0, VISIBLE = 1, INACTIVE = 2, BLOCKED = 3 } visibility_t;

typedef enum { PUTNODES_APP, PUTNODES_SYNC, PUTNODES_SYNCDEBRIS } putsource_t;

// maps handle-index pairs to file attribute handle.  map value is (file attribute handle, tag)
typedef map<pair<UploadHandle, fatype>, pair<handle, int> > fa_map;

typedef enum {
    SYNC_DISABLED = -3, //user disabled (if no syncError, otherwise automatically disabled . i.e SYNC_TEMPORARY_DISABLED)
    SYNC_FAILED = -2,
    SYNC_CANCELED = -1, // being deleted
    SYNC_INITIALSCAN = 0,
    SYNC_ACTIVE
} syncstate_t;

typedef enum
{
    // Sync is not operating in a backup capacity.
    SYNC_BACKUP_NONE = 0,
    // Sync is mirroring the local source.
    SYNC_BACKUP_MIRROR = 1,
    // Sync is monitoring (and propagating) local changes.
    SYNC_BACKUP_MONITOR = 2
}
SyncBackupState;

enum ScanResult
{
    SCAN_INPROGRESS,
    SCAN_SUCCESS,
    SCAN_FSID_MISMATCH,
    SCAN_INACCESSIBLE
}; // ScanResult

enum SyncError {
    NO_SYNC_ERROR = 0,
    UNKNOWN_ERROR = 1,
    UNSUPPORTED_FILE_SYSTEM = 2,            // File system type is not supported
    INVALID_REMOTE_TYPE = 3,                // Remote type is not a folder that can be synced
    INVALID_LOCAL_TYPE = 4,                 // Local path does not refer to a folder
    INITIAL_SCAN_FAILED = 5,                // The initial scan failed
    LOCAL_PATH_TEMPORARY_UNAVAILABLE = 6,   // Local path is temporarily unavailable: this is fatal when adding a sync
    LOCAL_PATH_UNAVAILABLE = 7,             // Local path is not available (can't be open)
    REMOTE_NODE_NOT_FOUND = 8,              // Remote node does no longer exists
    STORAGE_OVERQUOTA = 9,                  // Account reached storage overquota
    ACCOUNT_EXPIRED = 10,                   // Account expired (business or Pro Flexi)
    FOREIGN_TARGET_OVERSTORAGE = 11,        // Sync transfer fails (upload into an inshare whose account is overquota)
    REMOTE_PATH_HAS_CHANGED = 12,           // Remote path has changed (currently unused: not an error)
    REMOTE_PATH_DELETED = 13,               // (obsolete -> unified with REMOTE_NODE_NOT_FOUND) Remote path has been deleted
    SHARE_NON_FULL_ACCESS = 14,             // Existing inbound share sync or part thereof lost full access
    LOCAL_FILESYSTEM_MISMATCH = 15,         // Filesystem fingerprint does not match the one stored for the synchronization
    PUT_NODES_ERROR = 16,                   // Error processing put nodes result
    ACTIVE_SYNC_BELOW_PATH = 17,            // There's a synced node below the path to be synced
    ACTIVE_SYNC_ABOVE_PATH = 18,            // There's a synced node above the path to be synced
    REMOTE_NODE_MOVED_TO_RUBBISH = 19,      // Moved to rubbish
    REMOTE_NODE_INSIDE_RUBBISH = 20,        // Attempted to be added in rubbish
    VBOXSHAREDFOLDER_UNSUPPORTED = 21,      // Found unsupported VBoxSharedFolderFS
    LOCAL_PATH_SYNC_COLLISION = 22,         // Local path includes a synced path or is included within one
    ACCOUNT_BLOCKED = 23,                   // Account blocked
    UNKNOWN_TEMPORARY_ERROR = 24,           // Unknown temporary error
    TOO_MANY_ACTION_PACKETS = 25,           // Too many changes in account, local state discarded
    LOGGED_OUT = 26,                        // Logged out
    WHOLE_ACCOUNT_REFETCHED = 27,           // The whole account was reloaded, missed actionpacket changes could not have been applied
    MISSING_PARENT_NODE = 28,               // Setting a new parent to a parent whose LocalNode is missing its corresponding Node crossref
    BACKUP_MODIFIED = 29,                   // Backup has been externally modified.
    BACKUP_SOURCE_NOT_BELOW_DRIVE = 30,     // Backup source path not below drive path.
    SYNC_CONFIG_WRITE_FAILURE = 31,         // Unable to write sync config to disk.
    ACTIVE_SYNC_SAME_PATH = 32,             // There's a synced node at the path to be synced
    COULD_NOT_MOVE_CLOUD_NODES = 33,        // rename() failed
    COULD_NOT_CREATE_IGNORE_FILE = 34,      // Couldn't create a sync's initial ignore file.
    SYNC_CONFIG_READ_FAILURE = 35,          // Couldn't read sync configs from disk.
    UNKNOWN_DRIVE_PATH = 36,                // Sync's drive path isn't known.
    INVALID_SCAN_INTERVAL = 37,             // The user's specified an invalid scan interval.
    NOTIFICATION_SYSTEM_UNAVAILABLE = 38,   // Filesystem notification subsystem has encountered an unrecoverable error.
    UNABLE_TO_ADD_WATCH = 39,               // Unable to add a filesystem watch.
    UNABLE_TO_RETRIEVE_ROOT_FSID = 40,      // Unable to retrieve a sync root's FSID.
    UNABLE_TO_OPEN_DATABASE = 41,           // Unable to open state cache database.
    INSUFFICIENT_DISK_SPACE = 42,           // Insufficient space for download.
};

enum SyncWarning {
    NO_SYNC_WARNING = 0,
    LOCAL_IS_FAT = 1,                      // Found FAT (not a failure per se)
    LOCAL_IS_HGFS = 2,                      // Found HGFS (not a failure per se)
};


typedef enum { SYNCDEL_NONE, SYNCDEL_DELETED, SYNCDEL_INFLIGHT, SYNCDEL_BIN,
               SYNCDEL_DEBRIS, SYNCDEL_DEBRISDAY, SYNCDEL_FAILED } syncdel_t;

typedef vector<LocalNode*> localnode_vector;

typedef map<handle, LocalNode*> handlelocalnode_map;

typedef set<LocalNode*> localnode_set;

typedef multimap<int32_t, LocalNode*> idlocalnode_map;

struct UnlinkOrDebris {
    bool unlink = false;
    bool debris = false;
    bool canChangeVault = false;
    UnlinkOrDebris(bool u, bool d, bool v) : unlink(u), debris(d), canChangeVault(v) {}
};

typedef map<Node*, UnlinkOrDebris> unlink_or_debris_set;


// enumerates a node's children
// FIXME: switch to forward_list once C++11 becomes more widely available
typedef list<Node*> node_list;

// undefined node handle
const handle UNDEF = ~(handle)0;

#define ISUNDEF(h) (!((h) + 1))

typedef list<struct TransferSlot*> transferslot_list;

// FIXME: use forward_list instad (C++11)
typedef list<HttpReqCommandPutFA*> putfa_list;

// map a FileFingerprint to the transfer for that FileFingerprint
typedef map<FileFingerprint*, Transfer*, FileFingerprintCmp> transfer_map;

template <class T, class E>
class deque_with_lazy_bulk_erase
{
    // This is a wrapper class for deque.  Erasing an element from the middle of a deque is not cheap since all the subsequent elements need to be shuffled back.
    // This wrapper intercepts the erase() calls for single items, and instead marks each one as 'erased'.
    // The supplied template class E contains the normal deque entry T, plus a flag or similar to mark an entry erased.
    // Any other operation on the deque performs all the gathered erases in a single std::remove_if for efficiency.
    // This makes an enormous difference when cancelling 100k transfers in MEGAsync's transfers window for example.
    deque<E> mDeque;
    size_t nErased = 0;

public:

    typedef typename deque<E>::iterator iterator;

    void erase(iterator i)
    {
        assert(i != mDeque.end());
        i->erase();
        ++nErased;
    }

    void applyErase()
    {
        if (nErased)
        {
            // quite often the elements are at the front, no need to traverse the whole thing
            // removal from the front or back of a deque is cheap
            while (nErased && !mDeque.empty() && mDeque.front().isErased())
            {
                mDeque.pop_front();
                --nErased;
            }
            while (nErased && !mDeque.empty() && mDeque.back().isErased())
            {
                mDeque.pop_back();
                --nErased;
            }
            if (nErased)
            {
                auto newEnd = std::remove_if(mDeque.begin(), mDeque.end(), [](const E& e) { return e.isErased(); } );
                mDeque.erase(newEnd, mDeque.end());
                nErased = 0;
            }
        }
    }

    size_t size()                                        { applyErase(); return mDeque.size(); }
    size_t empty()                                       { applyErase(); return mDeque.empty(); }
    void clear()                                         { mDeque.clear(); }
    iterator begin(bool canHandleErasedElements = false) { if (!canHandleErasedElements) applyErase(); return mDeque.begin(); }
    iterator end(bool canHandleErasedElements = false)   { if (!canHandleErasedElements) applyErase(); return mDeque.end(); }
    void push_front(T t)                                 { applyErase(); mDeque.push_front(E(t)); }
    void push_back(T t)                                  { applyErase(); mDeque.push_back(E(t)); }
    void insert(iterator i, T t)                         { applyErase(); mDeque.insert(i, E(t)); }
    T& operator[](size_t n)                              { applyErase(); return mDeque[n]; }

};

template <class T1, class T2> class mapWithLookupExisting : public map<T1, T2>
{
    typedef map<T1, T2> base; // helps older gcc
public:
    T2* lookupExisting(T1 key)
    {
        auto it = base::find(key);
        if (it == base::end()) return nullptr;
        return &it->second;
    }
};

// map a request tag with pending dbids of transfers and files
typedef map<int, vector<uint32_t> > pendingdbid_map;

// map a request tag with a pending dns request
typedef map<int, GenericHttpReq*> pendinghttp_map;

// map an upload handle to the corresponding transfer
typedef map<UploadHandle, Transfer*> uploadhandletransfer_map;

// maps node handles to Node pointers
typedef map<NodeHandle, Node*> node_map;

struct NodeCounter
{
    m_off_t storage = 0;
    m_off_t versionStorage = 0;
    size_t files = 0;
    size_t folders = 0;
    size_t versions = 0;
    void operator += (const NodeCounter&);
    void operator -= (const NodeCounter&);
};

typedef std::map<NodeHandle, NodeCounter> NodeCounterMap;

// maps node handles to Share pointers
typedef map<handle, struct Share*> share_map;

// maps node handles NewShare pointers
typedef list<struct NewShare*> newshare_list;

// generic handle vector
typedef vector<handle> handle_vector;

// node and user vectors
typedef vector<struct User*> user_vector;
typedef vector<UserAlert::Base*> useralert_vector;
typedef vector<struct PendingContactRequest*> pcr_vector;

// actual user data (indexed by userid)
typedef map<int, User> user_map;

// maps user handles to userids
typedef map<handle, int> uh_map;

// maps lowercase user e-mail addresses to userids
typedef map<string, int> um_map;

// file attribute fetch map
typedef map<handle, FileAttributeFetch*> faf_map;

// file attribute fetch channel map
typedef map<int, FileAttributeFetchChannel*> fafc_map;

// transfer type
typedef enum { GET = 0, PUT, API, NONE } direction_t;
typedef enum { LARGEFILE = 0, SMALLFILE } filesizetype_t;

struct StringCmp
{
    bool operator()(const string* a, const string* b) const
    {
        return *a < *b;
    }
};

typedef map<handle, DirectReadNode*> handledrn_map;
typedef multimap<dstime, DirectReadNode*> dsdrn_map;
typedef list<DirectRead*> dr_list;
typedef list<DirectReadSlot*> drs_list;

typedef enum { TREESTATE_NONE = 0, TREESTATE_SYNCED, TREESTATE_PENDING, TREESTATE_SYNCING } treestate_t;

typedef enum { TRANSFERSTATE_NONE = 0, TRANSFERSTATE_QUEUED, TRANSFERSTATE_ACTIVE, TRANSFERSTATE_PAUSED,
               TRANSFERSTATE_RETRYING, TRANSFERSTATE_COMPLETING, TRANSFERSTATE_COMPLETED,
               TRANSFERSTATE_CANCELLED, TRANSFERSTATE_FAILED } transferstate_t;


// FIXME: use forward_list instad (C++11)
typedef list<HttpReqCommandPutFA*> putfa_list;

typedef map<handle, unique_ptr<PendingContactRequest>> handlepcr_map;

// Type-Value (for user attributes)
typedef vector<string> string_vector;
typedef map<string, string> string_map;
typedef string_map TLV_map;


// user attribute types
typedef enum {
    ATTR_UNKNOWN = -1,
    ATTR_AVATAR = 0,                        // public - char array - non-versioned
    ATTR_FIRSTNAME = 1,                     // public - char array - non-versioned
    ATTR_LASTNAME = 2,                      // public - char array - non-versioned
    ATTR_AUTHRING = 3,                      // private - byte array
    ATTR_LAST_INT = 4,                      // private - byte array
    ATTR_ED25519_PUBK = 5,                  // public - byte array - versioned
    ATTR_CU25519_PUBK = 6,                  // public - byte array - versioned
    ATTR_KEYRING = 7,                       // private - byte array - versioned
    ATTR_SIG_RSA_PUBK = 8,                  // public - byte array - versioned
    ATTR_SIG_CU255_PUBK = 9,                // public - byte array - versioned
    ATTR_COUNTRY = 10,                      // public - char array - non-versioned
    ATTR_BIRTHDAY = 11,                     // public - char array - non-versioned
    ATTR_BIRTHMONTH = 12,                   // public - char array - non-versioned
    ATTR_BIRTHYEAR = 13,                    // public - char array - non-versioned
    ATTR_LANGUAGE = 14,                     // private, non-encrypted - char array in B64 - non-versioned
    ATTR_PWD_REMINDER = 15,                 // private, non-encrypted - char array in B64 - non-versioned
    ATTR_DISABLE_VERSIONS = 16,             // private, non-encrypted - char array in B64 - non-versioned
    ATTR_CONTACT_LINK_VERIFICATION = 17,    // private, non-encrypted - char array in B64 - versioned
    ATTR_RICH_PREVIEWS = 18,                // private - byte array
    ATTR_RUBBISH_TIME = 19,                 // private, non-encrypted - char array in B64 - non-versioned
    ATTR_LAST_PSA = 20,                     // private - char array
    ATTR_STORAGE_STATE = 21,                // private - non-encrypted - char array in B64 - non-versioned
    ATTR_GEOLOCATION = 22,                  // private - byte array - non-versioned
    ATTR_CAMERA_UPLOADS_FOLDER = 23,        // private - byte array - non-versioned
    ATTR_MY_CHAT_FILES_FOLDER = 24,         // private - byte array - non-versioned
    ATTR_PUSH_SETTINGS = 25,                // private - non-encrypted - char array in B64 - non-versioned
    ATTR_UNSHAREABLE_KEY = 26,              // private - char array - versioned
    ATTR_ALIAS = 27,                        // private - byte array - versioned
    ATTR_AUTHRSA = 28,                      // private - byte array
    ATTR_AUTHCU255 = 29,                    // private - byte array
    ATTR_DEVICE_NAMES = 30,                 // private - byte array - versioned
    ATTR_MY_BACKUPS_FOLDER = 31,            // private - non-encrypted - char array in B64 - non-versioned
    //ATTR_BACKUP_NAMES = 32,               // (deprecated) private - byte array - versioned
    ATTR_COOKIE_SETTINGS = 33,              // private - byte array - non-versioned
    ATTR_JSON_SYNC_CONFIG_DATA = 34,        // private - byte array - non-versioned
    ATTR_DRIVE_NAMES = 35,                  // private - byte array - versioned
    ATTR_NO_CALLKIT = 36,                   // private, non-encrypted - char array in B64 - non-versioned

} attr_t;
typedef map<attr_t, string> userattr_map;

typedef enum {

    AES_CCM_12_16 = 0x00,
    AES_CCM_10_16 = 0x01,
    AES_CCM_10_08 = 0x02,
    AES_GCM_12_16_BROKEN = 0x03, // Same as 0x00 (due to a legacy bug)
    AES_GCM_10_08_BROKEN = 0x04, // Same as 0x02 (due to a legacy bug)
    AES_GCM_12_16 = 0x10,
    AES_GCM_10_08 = 0x11

} encryptionsetting_t;

typedef enum { AES_MODE_UNKNOWN, AES_MODE_CCM, AES_MODE_GCM } encryptionmode_t;

typedef enum { RECOVER_WITH_MASTERKEY = 9, RECOVER_WITHOUT_MASTERKEY = 10, CANCEL_ACCOUNT = 21, CHANGE_EMAIL = 12 } recovery_t;

typedef enum { EMAIL_REMOVED = 0, EMAIL_PENDING_REMOVED = 1, EMAIL_PENDING_ADDED = 2, EMAIL_FULLY_ACCEPTED = 3 } emailstatus_t;

typedef enum { RETRY_NONE = 0, RETRY_CONNECTIVITY = 1, RETRY_SERVERS_BUSY = 2, RETRY_API_LOCK = 3, RETRY_RATE_LIMIT = 4, RETRY_LOCAL_LOCK = 5, RETRY_UNKNOWN = 6} retryreason_t;

typedef enum {
    STORAGE_UNKNOWN = -9,
    STORAGE_GREEN = 0,      // there is storage is available
    STORAGE_ORANGE = 1,     // storage is almost full
    STORAGE_RED = 2,        // storage is full
    STORAGE_CHANGE = 3,     // the status of the storage might have changed
    STORAGE_PAYWALL = 4,    // storage is full and user didn't remedy despite of warnings
} storagestatus_t;


enum SmsVerificationState {
    // These values (except unknown) are delivered from the servers
    SMS_STATE_UNKNOWN = -1,       // Flag was not received
    SMS_STATE_NOT_ALLOWED = 0,    // No SMS allowed
    SMS_STATE_ONLY_UNBLOCK = 1,   // Only unblock SMS allowed
    SMS_STATE_FULL = 2            // Opt-in and unblock SMS allowed
};

typedef enum
{
    END_CALL_REASON_REJECTED     = 0x02,    /// 1on1 call was rejected while ringing
    END_CALL_REASON_BY_MODERATOR = 0x06,    /// group or meeting call has been ended by moderator
} endCall_t;

typedef unsigned int achievement_class_id;
typedef map<achievement_class_id, Achievement> achievements_map;

struct recentaction
{
    m_time_t time;
    handle user;
    handle parent;
    bool updated;
    bool media;
    node_vector nodes;
};
typedef vector<recentaction> recentactions_vector;

typedef enum { BIZ_STATUS_UNKNOWN = -2, BIZ_STATUS_EXPIRED = -1, BIZ_STATUS_INACTIVE = 0, BIZ_STATUS_ACTIVE = 1, BIZ_STATUS_GRACE_PERIOD = 2 } BizStatus;
typedef enum { BIZ_MODE_UNKNOWN = -1, BIZ_MODE_SUBUSER = 0, BIZ_MODE_MASTER = 1 } BizMode;

typedef enum {
    ACCOUNT_TYPE_UNKNOWN = -1,
    ACCOUNT_TYPE_FREE = 0,
    ACCOUNT_TYPE_PROI = 1,
    ACCOUNT_TYPE_PROII = 2,
    ACCOUNT_TYPE_PROIII = 3,
    ACCOUNT_TYPE_LITE = 4,
    ACCOUNT_TYPE_BUSINESS = 100,
    ACCOUNT_TYPE_PRO_FLEXI = 101
} AccountType;

typedef enum
{
    ACTION_CREATE_ACCOUNT              = 0,
    ACTION_RESUME_ACCOUNT              = 1,
    ACTION_CANCEL_ACCOUNT              = 2,
    ACTION_CREATE_EPLUSPLUS_ACCOUNT    = 3,
    ACTION_RESUME_EPLUSPLUS_ACCOUNT    = 4,
} AccountActionType;

typedef enum {
    AUTH_METHOD_UNKNOWN     = -1,
    AUTH_METHOD_SEEN        = 0,
    AUTH_METHOD_FINGERPRINT = 1,    // used only for AUTHRING_ED255
    AUTH_METHOD_SIGNATURE   = 2,    // used only for signed keys (RSA and Cu25519)
} AuthMethod;

typedef std::map<attr_t, AuthRing> AuthRingsMap;

// inside 'mega' namespace, since use C++11 and can't rely on C++14 yet, provide make_unique for the most common case.
// This keeps our syntax small, while making sure the compiler ensures the object is deleted when no longer used.
// Sometimes there will be ambiguity about std::make_unique vs mega::make_unique if cpp files "use namespace std", in which case specify ::mega::.
// It's better that we use the same one in older and newer compilers so we detect any issues.
template<class T, class... constructorArgs>
unique_ptr<T> make_unique(constructorArgs&&... args)
{
    return (unique_ptr<T>(new T(std::forward<constructorArgs>(args)...)));
}

//#define MEGA_MEASURE_CODE   // uncomment this to track time spent in major subsystems, and log it every 2 minutes, with extra control from megacli

namespace CodeCounter
{
    // Some classes that allow us to easily measure the number of times a block of code is called, and the sum of the time it takes.
    // Only enabled if MEGA_MEASURE_CODE is turned on.
    // Usage generally doesn't need to be protected by the macro as the classes and methods will be empty when not enabled.

    using namespace std::chrono;

    struct ScopeStats
    {
#ifdef MEGA_MEASURE_CODE
        uint64_t count = 0;
        uint64_t starts = 0;
        uint64_t finishes = 0;
        high_resolution_clock::duration timeSpent{};
        high_resolution_clock::duration longest{};
        std::string name;
        ScopeStats(std::string s) : name(std::move(s)) {}

        inline string report(bool reset = false)
        {
            string s = " " + name + ": " + std::to_string(count) + " " +
                    std::to_string(duration_cast<milliseconds>(timeSpent).count()) + " " +
                    std::to_string(duration_cast<milliseconds>(longest).count());
            if (reset)
            {
                count = 0;
                starts -= finishes;
                finishes = 0;
                timeSpent = high_resolution_clock::duration{};
                longest = high_resolution_clock::duration{};
            }
            return s;
        }
#else
        ScopeStats(std::string s) {}
#endif
    };

    struct DurationSum
    {
#ifdef MEGA_MEASURE_CODE
        high_resolution_clock::duration sum{ 0 };
        high_resolution_clock::time_point deltaStart;
        bool started = false;
        inline void start(bool b = true) { if (b && !started) { deltaStart = high_resolution_clock::now(); started = true; }  }
        inline void stop(bool b = true) { if (b && started) { sum += high_resolution_clock::now() - deltaStart; started = false; } }
        inline bool inprogress() { return started; }
        inline string report(bool reset = false)
        {
            string s = std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(sum).count());
            if (reset) sum = high_resolution_clock::duration{ 0 };
            return s;
        }
#else
        inline void start(bool = true) {  }
        inline void stop(bool = true) {  }
#endif
    };

    struct ScopeTimer
    {
#ifdef MEGA_MEASURE_CODE
        ScopeStats& scope;
        high_resolution_clock::time_point blockStart;
        high_resolution_clock::duration diff{};
        bool done = false;

        ScopeTimer(ScopeStats& sm) : scope(sm), blockStart(high_resolution_clock::now())
        {
            ++scope.starts;
        }
        ~ScopeTimer()
        {
            complete();
        }
        high_resolution_clock::duration timeSpent()
        {
            return high_resolution_clock::now() - blockStart;
        }
        void complete()
        {
            // can be called early in which case the destructor's call is ignored
            if (!done)
            {
                ++scope.count;
                ++scope.finishes;
                diff = high_resolution_clock::now() - blockStart;
                scope.timeSpent += diff;
                if (diff > scope.longest) scope.longest = diff;
                done = true;
            }
        }
#else
        ScopeTimer(ScopeStats& sm) {}
        void complete() {}
#endif
    };
}


// Hold the status of a status variable
class CacheableStatus : public Cacheable
{
public:
    enum Type
    {
        STATUS_UNKNOWN = 0,
        STATUS_STORAGE = 1,
        STATUS_BUSINESS = 2,
        STATUS_BLOCKED = 3,
        STATUS_PRO_LEVEL = 4,
    };

    CacheableStatus(Type type, int64_t value);

    // serializes the object to a string
    bool serialize(string* data) override;

    // deserializes the string to a SyncConfig object. Returns null in case of failure
    // returns a pointer to the unserialized value, owned by MegaClient passed as parameter
    static CacheableStatus* unserialize(MegaClient *client, const std::string& data);
    Type type() const;
    int64_t value() const;

    void setValue(const int64_t value);

    string typeToStr();
    static string typeToStr(Type type);

private:

    // need this to ensure serialization doesn't mutate state (Cacheable::serialize is non-const)
    bool serialize(std::string& data) const;

    Type mType = STATUS_UNKNOWN;
    int64_t mValue = 0;

};

typedef enum
{
    INVALID = -1,
    TWO_WAY = 0,
    UP_SYNC = 1,
    DOWN_SYNC = 2,
    CAMERA_UPLOAD = 3,
    MEDIA_UPLOAD = 4,
    BACKUP_UPLOAD = 5
}
BackupType;

typedef mega::byte ChatOptions_t;
struct ChatOptions
{
public:
    enum: ChatOptions_t
    {
        kEmpty         = 0x00,
        kSpeakRequest  = 0x01,
        kWaitingRoom   = 0x02,
        kOpenInvite    = 0x04,
    };

    // update with new options added, to get the max value allowed, with regard to the existing options
    static constexpr ChatOptions_t maxValidValue = kSpeakRequest | kWaitingRoom | kOpenInvite;

    ChatOptions(): mChatOptions(ChatOptions::kEmpty){}
    ChatOptions(ChatOptions_t options): mChatOptions(options){}
    ChatOptions(bool speakRequest, bool waitingRoom , bool openInvite)
        : mChatOptions(static_cast<ChatOptions_t>((speakRequest ? kSpeakRequest : 0)
                                            | (waitingRoom ? kWaitingRoom : 0)
                                            | (openInvite ? kOpenInvite : 0)))
    {
    }

    // setters/modifiers
    void set(ChatOptions_t val)             { mChatOptions = val; }
    void add(ChatOptions_t val)             { mChatOptions = mChatOptions | val; }
    void remove(ChatOptions_t val)          { mChatOptions = mChatOptions & static_cast<ChatOptions_t>(~val); }
    void updateSpeakRequest(bool enabled)   { enabled ? add(kSpeakRequest)  : remove(kSpeakRequest);}
    void updateWaitingRoom(bool enabled)    { enabled ? add(kWaitingRoom)   : remove(kWaitingRoom);}
    void updateOpenInvite(bool enabled)     { enabled ? add(kOpenInvite)    : remove(kOpenInvite);}

    // getters
    ChatOptions_t value() const             { return mChatOptions; }
    bool areEqual(ChatOptions_t val)        { return mChatOptions == val; }
    bool speakRequest() const               { return mChatOptions & kSpeakRequest; }
    bool waitingRoom() const                { return mChatOptions & kWaitingRoom; }
    bool openInvite() const                 { return mChatOptions & kOpenInvite; }
    bool isValid()                          { return unsigned(mChatOptions) <= unsigned(maxValidValue); }
    bool isEmpty()                          { return mChatOptions == kEmpty; }

protected:
    ChatOptions_t mChatOptions = kEmpty;
};

enum VersioningOption
{
    // In the cases where these options are specified for uploads, the `ov` flag will be
    // set if there is a pre-existing node in the target folder, with the same name.

    NoVersioning,             // Node will be put directly to parent, with no versions, and no other node affected
    ClaimOldVersion,          // The Node specified by `ov` (if any) will become the first version of the node put
    ReplaceOldVersion,        // the Node specified by `ov` (if any) will be deleted, and this new node takes its place, retaining any version chain.
    UseLocalVersioningFlag,   // One of the two above will occur, based on the versions_disabled flag
    UseServerVersioningFlag   // One of those two will occur, based on the API's current state of that flag
};

// cross reference pointers.  For the case where two classes have pointers to each other, and they should
// either always be NULL or if one refers to the other, the other refers to the one.
// This class makes sure that the two pointers are always consistent, and also prevents copy/move (unless the pointers are NULL)
template<class TO, class FROM>
FROM*& crossref_other_ptr_ref(TO* s);  // to be supplied for each pair of classes (to assign to the right member thereof) (gets around circular declarations)

template <class  TO, class  FROM>
class MEGA_API  crossref_ptr
{
    friend class crossref_ptr<FROM, TO>;

    template<class A, class B>
    friend B*& crossref_other_ptr_ref(A* s);  // friend so that specialization can access `ptr`

    TO* ptr = nullptr;

public:
    crossref_ptr() = default;

    ~crossref_ptr()
    {
        reset();
    }

    void crossref(TO* to, FROM* from)
    {
        assert(to && from);
        assert(ptr == nullptr);
        assert( !(crossref_other_ptr_ref<TO, FROM>(to)) );
        ptr = to;
        crossref_other_ptr_ref<TO, FROM>(ptr) = from;
    }

    void reset()
    {
        if (ptr)
        {
            assert( !!(crossref_other_ptr_ref<TO, FROM>(ptr)) );
            crossref_other_ptr_ref<TO, FROM>(ptr) = nullptr;
            ptr = nullptr;
        }
    }

    void store_unchecked(TO* p) { ptr = p; }
    TO*  release_unchecked() { auto p = ptr; ptr = nullptr; return p;  }

    TO* get()              { return ptr; }
    TO* operator->() const { assert(ptr != (void*)~0); return ptr; }
    operator TO*() const   { assert(ptr != (void*)~0); return ptr; }

    // no copying
    crossref_ptr(const crossref_ptr&) = delete;
    void operator=(const crossref_ptr&) = delete;

    // only allow move if the pointers are null (check at runtime with assert)
    crossref_ptr(crossref_ptr&& p) { assert(!p.ptr); }
    void operator=(crossref_ptr&& p) { assert(!p.ptr); ptr = p; }
};

class CancelToken
{
    // A small item with representation shared between many objects
    // They can all be cancelled in one go by setting the token flag true
    shared_ptr<bool> flag;

public:

    // invalid token, can't be cancelled.  No storage
    CancelToken() {}

    // create with a token available to be cancelled
    explicit CancelToken(bool value)
        : flag(std::make_shared<bool>(value))
    {
        if (value)
        {
            ++tokensCancelledCount;
        }
    }

    void cancel()
    {
        if (flag)
        {
            *flag = true;
            ++tokensCancelledCount;
        }
    }

    bool isCancelled() const
    {
        return !!flag && *flag;
    }

    bool exists()
    {
        return !!flag;
    }

    static std::atomic<uint32_t> tokensCancelledCount;

    static bool haveAnyCancelsOccurredSince(uint32_t& lastKnownCancelCount)
    {
        if (lastKnownCancelCount == tokensCancelledCount.load())
        {
            return false;
        }
        else
        {
            lastKnownCancelCount = tokensCancelledCount.load();
            return true;
        }
    }
};

} // namespace

#define MEGA_DISABLE_COPY(class_name) \
    class_name(const class_name&) = delete; \
    class_name& operator=(const class_name&) = delete;

#define MEGA_DISABLE_MOVE(class_name) \
    class_name(class_name&&) = delete; \
    class_name& operator=(class_name&&) = delete;

#define MEGA_DISABLE_COPY_MOVE(class_name) \
    MEGA_DISABLE_COPY(class_name) \
    MEGA_DISABLE_MOVE(class_name)

#define MEGA_DEFAULT_COPY(class_name) \
    class_name(const class_name&) = default; \
    class_name& operator=(const class_name&) = default;

#define MEGA_DEFAULT_MOVE(class_name) \
    class_name(class_name&&) = default; \
    class_name& operator=(class_name&&) = default;

#define MEGA_DEFAULT_COPY_MOVE(class_name) \
    MEGA_DEFAULT_COPY(class_name) \
    MEGA_DEFAULT_MOVE(class_name)

#endif
