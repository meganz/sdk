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
typedef enum { REQ_READY, REQ_PREPARED, REQ_INFLIGHT, REQ_SUCCESS, REQ_FAILURE, REQ_DONE, REQ_ASYNCIO } reqstatus_t;

typedef enum { USER_HANDLE, NODE_HANDLE } targettype_t;

typedef enum { METHOD_POST, METHOD_GET, METHOD_NONE} httpmethod_t;

typedef enum { REQ_BINARY, REQ_JSON } contenttype_t;

// new node source types
typedef enum { NEW_NODE, NEW_PUBLIC, NEW_UPLOAD } newnodesource_t;

// file chunk MAC
struct ChunkMAC
{
    ChunkMAC() : offset(0), finished(false) { }

    byte mac[SymmCipher::BLOCKSIZE];
    unsigned int offset;
    bool finished;
};

// file chunk macs
class chunkmac_map : public map<m_off_t, ChunkMAC>
{
public:
    int64_t macsmac(SymmCipher *cipher);
    void serialize(string& d) const;
    bool unserialize(const char*& ptr, const char* end);
    void calcprogress(m_off_t size, m_off_t& chunkpos, m_off_t& completedprogress, m_off_t* lastblockprogress = nullptr);
};

/**
 * @brief Declaration of API error codes.
 */
typedef enum ErrorCodes
{
    API_OK = 0,                     ///< Everything OK.
    API_EINTERNAL = -1,             ///< Internal error.
    API_EARGS = -2,                 ///< Bad arguments.
    API_EAGAIN = -3,                ///< Request failed, retry with exponential backoff.
    API_ERATELIMIT = -4,            ///< Too many requests, slow down.
    API_EFAILED = -5,               ///< Request failed permanently.
    API_ETOOMANY = -6,              ///< Too many requests for this resource.
    API_ERANGE = -7,                ///< Resource access out of rage.
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
    API_EWRITE = -20,               /**< File could not be written to (or failed
                                         post-write integrity check). */
    API_EREAD = -21,                /**< File could not be read from (or changed
                                         unexpectedly during reading). */
    API_EAPPKEY = -22,              ///< Invalid or missing application key.
    API_ESSL = -23,                 ///< SSL verification failed
    API_EGOINGOVERQUOTA = -24,      ///< Not enough quota
    API_EMFAREQUIRED = -26,         ///< Multi-factor authentication required
    API_EMASTERONLY = -27,          ///< Access denied for sub-users (only for business accounts)
    API_EBUSINESSPASTDUE = -28      ///< Business account expired
} error;

// returned by loggedin()
typedef enum { NOTLOGGEDIN, EPHEMERALACCOUNT, CONFIRMEDACCOUNT, FULLACCOUNT } sessiontype_t;

// node/user handles are 8-11 base64 characters, case sensitive, and thus fit
// in a 64-bit int
typedef uint64_t handle;

// (can use unordered_set if available)
typedef set<handle> handle_set;

// file attribute type
typedef uint16_t fatype;

// list of files
typedef list<struct File*> file_list;

// node types:
// FILE - regular file nodes
// FOLDER - regular folder nodes
// ROOT - the cloud drive root node
// INCOMING - inbox
// RUBBISH - rubbish bin
typedef enum { TYPE_UNKNOWN = -1, FILENODE = 0, FOLDERNODE, ROOTNODE, INCOMINGNODE, RUBBISHNODE } nodetype_t;

// node type key lengths
const int FILENODEKEYLENGTH = 32;
const int FOLDERNODEKEYLENGTH = 16;

typedef list<class Sync*> sync_list;

// persistent resource cache storage
struct Cachable
{
    virtual bool serialize(string*) = 0;

    int32_t dbid;

    bool notified;

    Cachable();
    virtual ~Cachable() { }
};

// numeric representation of string (up to 8 chars)
typedef uint64_t nameid;

// access levels:
// RDONLY - cannot add, rename or delete
// RDWR - cannot rename or delete
// FULL - all operations that do not require ownership permitted
// OWNER - node is in caller's ROOT, INCOMING or RUBBISH trees
typedef enum { ACCESS_UNKNOWN = -1, RDONLY = 0, RDWR, FULL, OWNER, OWNERPRELOGIN } accesslevel_t;

// operations for outgoing pending contacts
typedef enum { OPCA_ADD = 0, OPCA_DELETE, OPCA_REMIND} opcactions_t;
// operations for incoming pending contacts
typedef enum { IPCA_ACCEPT = 0, IPCA_DENY, IPCA_IGNORE} ipcactions_t;


typedef vector<struct Node*> node_vector;

// contact visibility:
// HIDDEN - not shown
// VISIBLE - shown
typedef enum { VISIBILITY_UNKNOWN = -1, HIDDEN = 0, VISIBLE = 1, INACTIVE = 2, BLOCKED = 3 } visibility_t;

typedef enum { PUTNODES_APP, PUTNODES_SYNC, PUTNODES_SYNCDEBRIS } putsource_t;

// maps handle-index pairs to file attribute handle
typedef map<pair<handle, fatype>, pair<handle, int> > fa_map;

typedef enum { SYNC_FAILED = -2, SYNC_CANCELED = -1, SYNC_INITIALSCAN = 0, SYNC_ACTIVE } syncstate_t;

typedef enum { SYNCDEL_NONE, SYNCDEL_DELETED, SYNCDEL_INFLIGHT, SYNCDEL_BIN,
               SYNCDEL_DEBRIS, SYNCDEL_DEBRISDAY, SYNCDEL_FAILED } syncdel_t;

typedef vector<LocalNode*> localnode_vector;

typedef map<handle, LocalNode*> handlelocalnode_map;

typedef set<LocalNode*> localnode_set;

typedef multimap<int32_t, LocalNode*> idlocalnode_map;

typedef set<Node*> node_set;

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

typedef deque<Transfer*> transfer_list;

// map a request tag with pending dbids of transfers and files
typedef map<int, vector<uint32_t> > pendingdbid_map;

// map a request tag with a pending dns request
typedef map<int, GenericHttpReq*> pendinghttp_map;

// map a request tag with pending paths of temporary files
typedef map<int, vector<string> > pendingfiles_map;

// map an upload handle to the corresponding transer
typedef map<handle, Transfer*> handletransfer_map;

// maps node handles to Node pointers
typedef map<handle, Node*> node_map;

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

typedef std::map<handle, NodeCounter> NodeCounterMap;

// maps node handles to Share pointers
typedef map<handle, struct Share*> share_map;

// maps node handles NewShare pointers
typedef list<struct NewShare*> newshare_list;

// generic handle vector
typedef vector<handle> handle_vector;

// pairs of node handles
typedef set<pair<handle, handle> > handlepair_set;

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

typedef map<const string*, LocalNode*, StringCmp> localnode_map;
typedef map<const string*, Node*, StringCmp> remotenode_map;

typedef enum { TREESTATE_NONE = 0, TREESTATE_SYNCED, TREESTATE_PENDING, TREESTATE_SYNCING } treestate_t;

typedef enum { TRANSFERSTATE_NONE = 0, TRANSFERSTATE_QUEUED, TRANSFERSTATE_ACTIVE, TRANSFERSTATE_PAUSED,
               TRANSFERSTATE_RETRYING, TRANSFERSTATE_COMPLETING, TRANSFERSTATE_COMPLETED,
               TRANSFERSTATE_CANCELLED, TRANSFERSTATE_FAILED } transferstate_t;

struct Notification
{
    dstime timestamp;
    string path;
    LocalNode* localnode;
};

typedef deque<Notification> notify_deque;

// FIXME: use forward_list instad (C++11)
typedef list<HttpReqCommandPutFA*> putfa_list;

typedef map<handle, PendingContactRequest*> handlepcr_map;

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
    ATTR_CONTACT_LINK_VERIFICATION = 17,    // private, non-encrypted - char array in B64 - non-versioned
    ATTR_RICH_PREVIEWS = 18,                // private - byte array
    ATTR_RUBBISH_TIME = 19,                 // private, non-encrypted - char array in B64 - non-versioned
    ATTR_LAST_PSA = 20,                     // private - char array
    ATTR_STORAGE_STATE = 21,                // private - non-encrypted - char array in B64 - non-versioned
    ATTR_GEOLOCATION = 22,                  // private - byte array - non-versioned
    ATTR_CAMERA_UPLOADS_FOLDER = 23,        // private - byte array - non-versioned
    ATTR_MY_CHAT_FILES_FOLDER = 24,         // private - byte array - non-versioned
    ATTR_PUSH_SETTINGS = 25,                // private - non-encripted - char array in B64 - non-versioned
    ATTR_UNSHAREABLE_KEY = 26,              // private - char array
    ATTR_ALIAS = 27,                        // private - byte array - non-versioned
    ATTR_AUTHRSA = 28,                      // private - byte array
    ATTR_AUTHCU255 = 29,                    // private - byte array

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

#ifdef ENABLE_CHAT
typedef enum { PRIV_UNKNOWN = -2, PRIV_RM = -1, PRIV_RO = 0, PRIV_STANDARD = 2, PRIV_MODERATOR = 3 } privilege_t;
typedef pair<handle, privilege_t> userpriv_pair;
typedef vector< userpriv_pair > userpriv_vector;
typedef map <handle, set <handle> > attachments_map;
struct TextChat : public Cachable
{
    enum {
        FLAG_OFFSET_ARCHIVE = 0
    };

    handle id;
    privilege_t priv;
    int shard;
    userpriv_vector *userpriv;
    bool group;
    string title;        // byte array
    string unifiedKey;   // byte array
    handle ou;
    m_time_t ts;     // creation time
    attachments_map attachedNodes;
    bool publicchat;  // whether the chat is public or private

private:        // use setter to modify these members
    byte flags;     // currently only used for "archive" flag at first bit

public:
    int tag;    // source tag, to identify own changes

    TextChat();
    ~TextChat();

    bool serialize(string *d);
    static TextChat* unserialize(class MegaClient *client, string *d);

    void setTag(int tag);
    int getTag();
    void resetTag();

    struct
    {
        bool attachments : 1;
        bool flags : 1;
        bool mode : 1;
    } changed;

    // return false if failed
    bool setNodeUserAccess(handle h, handle uh, bool revoke = false);
    bool setFlag(bool value, uint8_t offset = 0xFF);
    bool setFlags(byte newFlags);
    bool isFlagSet(uint8_t offset) const;
    bool setMode(bool publicchat);

};
typedef vector<TextChat*> textchat_vector;
typedef map<handle, TextChat*> textchat_map;
#endif

typedef enum { RECOVER_WITH_MASTERKEY = 9, RECOVER_WITHOUT_MASTERKEY = 10, CANCEL_ACCOUNT = 21, CHANGE_EMAIL = 12 } recovery_t;

typedef enum { EMAIL_REMOVED = 0, EMAIL_PENDING_REMOVED = 1, EMAIL_PENDING_ADDED = 2, EMAIL_FULLY_ACCEPTED = 3 } emailstatus_t;

typedef enum { RETRY_NONE = 0, RETRY_CONNECTIVITY = 1, RETRY_SERVERS_BUSY = 2, RETRY_API_LOCK = 3, RETRY_RATE_LIMIT = 4, RETRY_LOCAL_LOCK = 5, RETRY_UNKNOWN = 6} retryreason_t;

typedef enum { STORAGE_UNKNOWN = -9, STORAGE_GREEN = 0, STORAGE_ORANGE = 1, STORAGE_RED = 2, STORAGE_CHANGE = 3 } storagestatus_t;


enum SmsVerificationState {
    // These values (except unknown) are delivered from the servers
    SMS_STATE_UNKNOWN = -1,       // Flag was not received
    SMS_STATE_NOT_ALLOWED = 0,    // No SMS allowed
    SMS_STATE_ONLY_UNBLOCK = 1,   // Only unblock SMS allowed
    SMS_STATE_FULL = 2            // Opt-in and unblock SMS allowed
};

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
        std::string name;
        ScopeStats(std::string s) : name(std::move(s)) {}

        inline string report(bool reset = false) 
        { 
            string s = " " + name + ": " + std::to_string(count) + " " + std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(timeSpent).count()); 
            if (reset)
            {
                count = 0;
                starts -= finishes;
                finishes = 0;
                timeSpent = high_resolution_clock::duration{};
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

        ScopeTimer(ScopeStats& sm) : scope(sm), blockStart(high_resolution_clock::now())
        {
            ++scope.starts;
        }
        ~ScopeTimer()
        {
            ++scope.count;
            ++scope.finishes;
            scope.timeSpent += high_resolution_clock::now() - blockStart;
        }
#else
        ScopeTimer(ScopeStats& sm)
        {
        }
#endif
    };
}

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
