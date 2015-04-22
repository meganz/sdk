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

// platform-specific includes and defines
#include "megasys.h"

// signed 64-bit generic offset
typedef int64_t m_off_t;

// opaque filesystem fingerprint
typedef uint64_t fsfp_t;

#ifdef USE_CRYPTOPP
#include "crypto/cryptopp.h"
#else
#include "megacrypto.h"
#endif

#ifdef USE_SODIUM
#include "crypto/sodium.h"
#endif

namespace mega {
using namespace std;

// forward declaration
struct AttrMap;
class BackoffTimer;
class Command;
class CommandLoadBalancing;
struct DirectRead;
struct DirectReadNode;
struct DirectReadSlot;
struct FileAccess;
struct FileAttributeFetch;
struct FileAttributeFetchChannel;
struct FileFingerprint;
struct FileFingerprintCmp;
struct HttpReq;
struct HttpReqCommandPutFA;
struct LocalNode;
class MegaClient;
struct NewNode;
struct Node;
struct NodeCore;
class PubKeyAction;
class Request;
class SharedBuffer;
struct Transfer;
class TreeProc;
class LocalTreeProc;
struct User;
struct Waiter;
struct Proxy;

#define EOO 0

typedef int64_t m_time_t;

// monotonously increasing time in deciseconds
typedef uint32_t dstime;

#define NEVER (~(dstime)0)
#define EVER(ds) ((ds+1))

#define STRINGIFY(x) # x
#define TOSTRING(x) STRINGIFY(x)

// HttpReq states
typedef enum { REQ_READY, REQ_PREPARED, REQ_INFLIGHT, REQ_SUCCESS, REQ_FAILURE, REQ_DONE } reqstatus_t;

typedef enum { USER_HANDLE, NODE_HANDLE } targettype_t;

typedef enum { REQ_BINARY, REQ_JSON } contenttype_t;

// new node source types
typedef enum { NEW_NODE, NEW_PUBLIC, NEW_UPLOAD } newnodesource_t;

// file chunk MAC
struct ChunkMAC
{
    byte mac[SymmCipher::BLOCKSIZE];
};

// file chunk macs
typedef map<m_off_t, ChunkMAC> chunkmac_map;

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
    API_EKEYVERFAIL = -23           ///< Key verification failure.
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

typedef vector<struct Node*> node_vector;

// contact visibility:
// HIDDEN - not shown
// VISIBLE - shown
typedef enum { VISIBILITY_UNKNOWN = -1, HIDDEN = 0, VISIBLE, ME } visibility_t;

typedef enum { PUTNODES_APP, PUTNODES_SYNC, PUTNODES_SYNCDEBRIS } putsource_t;

// maps handle-index pairs to file attribute handle
typedef map<pair<handle, fatype>, pair<handle, int> > fa_map;

typedef enum { SYNC_FAILED = -2, SYNC_CANCELED = -1, SYNC_INITIALSCAN = 0, SYNC_ACTIVE } syncstate_t;

typedef enum { SYNCDEL_NONE, SYNCDEL_DELETED, SYNCDEL_INFLIGHT, SYNCDEL_BIN,
               SYNCDEL_DEBRIS, SYNCDEL_DEBRISDAY } syncdel_t;

typedef vector<LocalNode*> localnode_vector;

typedef map<handle, LocalNode*> handlelocalnode_map;

typedef list<LocalNode*> localnode_list;

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

// map an upload handle to the corresponding transer
typedef map<handle, Transfer*> handletransfer_map;

// maps node handles to Node pointers
typedef map<handle, Node*> node_map;

// maps node handles to Share pointers
typedef map<handle, struct Share*> share_map;

// maps node handles NewShare pointers
typedef list<struct NewShare*> newshare_list;

// generic handle vector
typedef vector<handle> handle_vector;

// pairs of node handles
typedef set<pair<handle, handle> > handlepair_set;

// node and user vectors
typedef vector<struct NodeCore*> nodecore_vector;
typedef vector<struct User*> user_vector;

// actual user data (indexed by userid)
typedef map<int, User> user_map;

// maps user handles to userids
typedef map<handle, int> uh_map;

// maps lowercase user e-mail addresses to userids
typedef map<string, int> um_map;

// file attribute data
typedef map<unsigned, string> fadata_map;

// syncid to node handle mapping
typedef map<handle, handle> syncidhandle_map;

// NewNodes index to syncid mapping
typedef map<int, handle> newnodesyncid_map;

// for dynamic node addition requests, used by the sync subsystem
typedef vector<struct NewNode*> newnode_vector;

// file attribute fetch map
typedef map<handle, FileAttributeFetch*> faf_map;

// file attribute fetch channel map
typedef map<int, FileAttributeFetchChannel*> fafc_map;

// transfer type
typedef enum { GET, PUT } direction_t;

typedef set<pair<int, handle> > fareq_set;

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

// FIXME: use forward_list instead
typedef list<NewNode*> newnode_list;
typedef list<handle> handle_list;

typedef map<handle, NewNode*> handlenewnode_map;

typedef map<handle, char> handlecount_map;

// maps FileFingerprints to node
typedef multiset<FileFingerprint*, FileFingerprintCmp> fingerprint_set;

typedef enum { TREESTATE_NONE = 0, TREESTATE_SYNCED, TREESTATE_PENDING, TREESTATE_SYNCING } treestate_t;

struct Notification
{
    dstime timestamp;
    string path;
    LocalNode* localnode;
};

typedef deque<Notification> notify_deque;

// FIXME: use forward_list instad (C++11)
typedef list<HttpReqCommandPutFA*> putfa_list;
} // namespace

#endif
