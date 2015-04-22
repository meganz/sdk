/**
 * @file mega/megaclient.h
 * @brief Client access engine core logic
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

#ifndef MEGACLIENT_H
#define MEGACLIENT_H 1

#include "json.h"
#include "db.h"
#include "gfx.h"
#include "filefingerprint.h"
#include "request.h"
#include "treeproc.h"
#include "sharenodekeys.h"
#include "account.h"
#include "backofftimer.h"
#include "http.h"
#include "pubkeyaction.h"
#include "userAttributes.h"
#include "secureBuffer.h"
#include "sharedbuffer.h"

#include <functional>

namespace mega {

typedef std::function<void(error)> VerifyKeyCallback;
typedef std::function<void(ValueMap, error)> GetSigKeysCallback;
typedef std::function<void(error)> FetchKeyringsCallback;

class MEGA_API MegaClient
{
public:
    // own identity
    handle me;

    // root nodes (files, incoming, rubbish)
    handle rootnodes[3];

    // all nodes
    node_map nodes;

    // all users
    user_map users;

    // process API requests and HTTP I/O
    void exec();

    // wait for I/O or other events
    int wait();

    // abort exponential backoff
    bool abortbackoff(bool = true);

    // ID tag of the next request
    int nextreqtag();

    // corresponding ID tag of the currently executing callback
    int restag;

    // ephemeral session support
    void createephemeral();
    void resumeephemeral(handle, const byte*, int = 0);

    // full account confirmation/creation support
    void sendsignuplink(const char*, const char*, const byte*);
    void querysignuplink(const byte*, unsigned);
    void confirmsignuplink(const byte*, unsigned, uint64_t);
    void setkeypair(std::function<void(error)> callback);

    // set and get user attributes
    void setuserattribute();

    void getuserattribute();

    /**
     * @brief Initializes the Ed25519 EdDSA key user properties.
     *
     * A key pair will be added, if not present, yet.
     *
     * @return Error code (default: 1 on success).
     */
    int inited25519();

    // user login: e-mail, pwkey
    void login(const char*, const byte*);

    // user login: e-mail, pwkey, emailhash
    void fastlogin(const char*, const byte*, uint64_t);

    // session login: binary session, bytecount
    void login(const byte*, int);

    // get user data
    void getuserdata();

    // ATTR

    /**
     * @brief Get the signing keys for the currently loggend in user.
     *
     * If the keys do not exist on the server, we will create and upload them
     * before returning.
     *
     * @param reset Create and upload the keys without checking the server.
     */
    void getownsigningkeys(bool reset = false);

    /**
     * @brief Get the signing keys for the currently logged in user.
     *
     * @param callback The callback for the end of thd request.
     * @param reset Reset the key information for the currently logged in user.
     */
    void getownsigningkeys(GetSigKeysCallback callback, bool reset = false);

    /**
     * @param Upload the key informaiton for the currently logged in user.
     */
    void uploadkeys();

    /**
     * @brief Generate the keys for the given user and upload them to the server.
     *
     * @param callback The calback for the enrd of the request.
     */
    void uploadkeys(GetSigKeysCallback callback);

    /**
     * @brief Sign the given RSA key.
     *
     * @param SharedBuffer a buffer with the RSA key.
     * @return A buffer with the key signature.
     */
    SharedBuffer signRSAKey(SharedBuffer &&buffer);

    /**
     * @brief Verify the users RSA key with the sigPubk stored in the attribute server.
     *
     * @param user The user to verify the key for.
     * @param funct The callback function for the completion of the request.
     */
    void verifyRSAKeySignature(User *user, std::function<void(error)> funct);

    /**
     * @brief Complete the verification of the signature for the given users
     * public RSA key.
     *
     * The RSA key for the user should have already been fetched at this point.
     *
     * @param user The user to verify for.
     * @param sig The signature of the key to verify.
     */
    bool verifyRSAKeySignature_(User *user, SharedBuffer &sig);

    /**
     * @brief Fetch the RSA and ed25519 keyrings.
     *
     * @param callback The callback for when the request completes.
     */
    void fetchKeyrings(FetchKeyringsCallback callback);

    /**
     * @param Verify the given key aganist the keyring.
     *
     * @param user The users email address or ASCII encoded handle.
     * @param key The key to verify.
     * @param rsa 1 if the key is an RSA public key, 0 if its an ed25519 public
     *            key.
     * @param callback The callback for when the request completes.
     */
    void verifyKeyFingerPrint(const char *user, SharedBuffer &key, int rsa,
            VerifyKeyCallback callback);

    /**
     * @brief Complete the key verification.
     *
     * @param user The users email address or ASCII encoded handle.
     * @param key The key to verify.
     * @param rsa 1 if the key is an rsa key, 0 if ed25519.
     * @param callback The callback for when the request completes.
     */
    void verifyKeyFingerPrint_(const char *user, SharedBuffer &key, int rsa,
            VerifyKeyCallback callback);

    /**
     * @brief Get the ed25519 public static key for the given user.
     *
     * @param user The email address or ASCII encoded handle for the user.
     */
    void getPublicStaticKey(const char *user);

    /**
     * @brief Serilize one of the keyring maps to a value map.
     *
     * @param rsa 1 if the map to serilize is the rsa keyring, 0 if its the
     *            ed25519 keyring.
     * @return A ValueMap containing the serilized map.
     */
    ValueMap serilizeMap(int rsa);

    /**
     * @brief Deserilize the given map and set it in this client.
     *
     * @param map The ValueMap with the map to deserilize.
     * @param rsa 1 if the map to deserilize is an rsa keyring, 1 for ed25519.
     */
    bool deserilizeMap(ValueMap map, int rsa);

    /**
     * @brief Create the fingerprint for the given key.
     *
     * @param keyBytes The key to generate the fingerprint for.
     * @return A SharedBuffer with the generated fingerprint.
     */
    SharedBuffer createFingerPrint(SharedBuffer &keyBytes);

    /**
     * @brief Ge the signing key for another user.
     *
     * @param user The user to get the public signing key for.
     */
    void getothersigningkey(const char *user);

    /**
     * @brief Get a user attribute.
     *
     * @param user The users email or ASCII encoded handle.
     * @param an The name of the attribute to get.
     */
    void getuserattribute(const char *user, const char *an);

    /**
     * @brief Set a generic attribute for the currenly logged in user.
     *
     * @param user The users email or ASCII encoded handle.
     * @param an The name of the attribute to set.
     * @param map The ValueMap with the attribute values.
     * @param priv 0 if the attribute is to be public, 1 if private.
     * @param nonhistoric 0 if the attribute is to be historic, 1 if not.
     */
    void setuserattribute(const char *user, const char *an, ValueMap map, int priv,
            int nonhistoric);

    /**
     * @brief Get a generic user attribute from the server.
     *
     * @param user The users email or ASCII encoded handle.
     * @param an The name of the attribute to get.
     * @param funct The callback for the end of the request.
     */
    void getgattribute(const char *user, const char *an,
            std::function<void(ValueMap, error)> funct);

    /**
     * @brief Set a generic attribute for the currently logged in user.
     *
     * @param an The name of the attribute to set.
     * @param map The ValueMap of attribute values.
     * @param priv 0 if the attribute is public, 1 if private.
     * @param nonhistoric 0 if the attribute is to be historic, 1 if not.
     * @param funct The callback for the end of the request.
     */
    void setgattribute(const char *an, ValueMap map, int priv, int nonhistoric,
            std::function<void(error)> funct);

    /**
     * @param Get a generic user attribute from the server.
     *
     * @param email The email address or ASCII encoded handle for the user.
     * @param an The name of the attribute to fetch.
     * @param funct The callback for the end of the request.
     */
    void getgattribute(std::string &email, const char *an,
            std::function<void(ValueMap, error)> funct);

    /**
     * @brief Init the static ed25519 keys for the currently logged in user.
     *
     * @return The generated keypair for the user.
     */
    std::pair<SecureBuffer, SecureBuffer> initstatickeys();

    //////////////////////////

    // get the public key of an user
    void getpubkey(const char* user);

    // check if logged in
    sessiontype_t loggedin();

    // dump current session
    int dumpsession(byte*, int);

    // Kill session id
    void killsession(handle session);
    void killallsessions();

    // set folder link: node, key
    error folderaccess(const char*, const char*);

    // open exported file link
    error openfilelink(const char*, int);

    // change login password
    error changepw(const byte*, const byte*);

    // load all trees: nodes, shares, contacts
    void fetchnodes();

    // retrieve user details
    void getaccountdetails(AccountDetails*, bool, bool, bool, bool, bool, bool);

    // update node attributes
    error setattr(Node*, const char** = NULL, const char* prevattr = NULL);

    // prefix and encrypt attribute json
    void makeattr(SymmCipher*, string*, const char*, int = -1) const;

    // check node access level
    int checkaccess(Node*, accesslevel_t);

    // check if a move operation would succeed
    error checkmove(Node*, Node*);

    // delete node
    error unlink(Node*);

    // move node to new parent folder
    error rename(Node*, Node*, syncdel_t = SYNCDEL_NONE, handle = UNDEF);

    // start/stop/pause file transfer
    bool startxfer(direction_t, File*);
    void stopxfer(File* f);
    void pausexfers(direction_t, bool, bool = false);

    // enqueue/abort direct read
    void pread(Node*, m_off_t, m_off_t, void*);
    void pread(handle, SymmCipher* key, int64_t, m_off_t, m_off_t, void*);
    void preadabort(Node*, m_off_t = -1, m_off_t = -1);
    void preadabort(handle, m_off_t = -1, m_off_t = -1);

    // pause flags
    bool xferpaused[2];

#ifdef ENABLE_SYNC
    // active syncs
    sync_list syncs;
    bool syncadded;

    // indicates whether all startup syncs have been fully scanned
    bool syncsup;
#endif

    // if set, symlinks will be followed except in recursive deletions
    // (give the user ample warning about possible sync repercussions)
    bool followsymlinks;

    // number of parallel connections per transfer (PUT/GET)
    unsigned char connections[2];

    // generate & return next upload handle
    handle uploadhandle(int);

    // add nodes to specified parent node (complete upload, copy files, make
    // folders)
    void putnodes(handle, NewNode*, int);

    // send files/folders to user
    void putnodes(const char*, NewNode*, int);

    // attach file attribute to upload or node handle
    void putfa(handle, fatype, SymmCipher*, string*);

    // queue file attribute retrieval
    error getfa(Node*, fatype, int = 0);
    
    // notify delayed upload completion subsystem about new file attribute
    void checkfacompletion(handle, Transfer* = NULL);

    /**
     * @brief Attach/update/delete a user attribute.
     *
     * @param an Attribute name.
     * @param av Attribute value.
     * @param avl Attribute value length.
     * @param priv 1 for a private, 0 for a public attribute.
     * @return Void.
     */
    void putua(const char* an, const byte* av = NULL, unsigned avl = 0, int priv = 0);

    /**
     * @brief Queue a user attribute retrieval.
     *
     * @param u User.
     * @param an Attribute name.
     * @param p 1 for a private, 0 for a public attribute.
     * @return Void.
     */
    void getua(User* u, const char* an = NULL, int p = 0);

    // add new contact (by e-mail address)
    error invite(const char*, visibility_t = VISIBLE);

    // add/remove/update outgoing share
    void setshare(Node*, const char*, accesslevel_t);

    // export node link or remove existing exported link for this node
    error exportnode(Node*, int);

    // add/delete sync
    error addsync(string*, const char*, string*, Node*, fsfp_t = 0, int = 0);
    void delsync(Sync*, bool = true);

    // close all open HTTP connections
    void disconnect();

    // abort session and free all state information
    void logout();

    // free all state information
    void locallogout();

    // SDK version
    const char* version();

    // maximum outbound throughput (per target server)
    int putmbpscap;

    // User-Agent header for HTTP requests
    string useragent;

    // shopping basket
    handle_vector purchase_basket;

    // enumerate Pro account purchase options
    void purchase_enumeratequotaitems();

    // clear shopping basket
    void purchase_begin();

    // add item to basket
    void purchase_additem(int, handle, unsigned, const char *, unsigned, const char *, const char *);

    // submit purchased products for payment
    void purchase_checkout(int);

    // submit purchase receipt for verification
    void submitpurchasereceipt(int, const char*);

    // toggle global debug flag
    bool toggledebug();

    bool debugstate();

    // report an event to the API logger
    void reportevent(const char*, const char* = NULL);

    // disable public key pinning (for testing purposes)
    static bool disablepkp;

    // root URL for API requests
    static string APIURL;

    // root URL for load balancing requests
    static const char* const BALANCERURL;

private:

    // Map for storing the handle : rsa_fingerprint-confidence values.
    std::map<handle, FingerPrintRecord> rsaKeyRing;

    // Map for storing the handle : ed_fingerprint-confidence values.
    std::map<handle, FingerPrintRecord> edKeyRing;

    bool keysFetched;

    // API request queue double buffering:
    // reqs[r] is open for adding commands
    // reqs[r^1] is being processed on the API server
    HttpReq* pendingcs;
    BackoffTimer btcs;

    // server-client command trigger connection
    HttpReq* pendingsc;
    BackoffTimer btsc;

    // badhost report
    HttpReq* badhostcs;
    HttpReq* loadbalancingcs;

    // notify URL for new server-client commands
    string scnotifyurl;

    // unique request ID
    char reqid[10];

    // auth URI component for API requests
    string auth;

    // API response JSON object
    JSON response;

    // response record processing issue
    bool warned;

    // next local user record identifier to use
    int userid;

    // pending file attribute writes
    putfa_list newfa;

    // current attribute being sent
    putfa_list::iterator curfa;
    BackoffTimer btpfa;

    // next internal upload handle
    handle nextuh;

    // maximum number of concurrent transfers
    static const unsigned MAXTRANSFERS = 12;

    // determine if more transfers fit in the pipeline
    bool moretransfers(direction_t);

    // update time at which next deferred transfer retry kicks in
    void nexttransferretry(direction_t d, dstime*);

    // a TransferSlot chunk failed
    bool chunkfailed;

    // open/create state cache database table
    void opensctable();
    
    // fetch state serialize from local cache
    bool fetchsc(DbTable*);

    // server-client command processing
    void sc_updatenode();
    Node* sc_deltree();
    void sc_newnodes();
    void sc_contacts();
    void sc_keys();
    void sc_fileattr();
    void sc_userattr();
    bool sc_shares();

    void init();

    // add node to vector and return index
    unsigned addnode(node_vector*, Node*) const;

    // add child for consideration in syncup()/syncdown()
    void addchild(remotenode_map*, string*, Node*, list<string>*) const;

    // crypto request response
    void cr_response(node_vector*, node_vector*, JSON*);

    // read node tree from JSON object
    void readtree(JSON*);

    // used by wait() to handle event timing
    void checkevent(dstime, dstime*, dstime*);

    // converts UTF-8 to 32-bit word array
    static char* str_to_a32(const char*, int*);

    // last successful interaction with the Internet
    dstime noinetds;

    // was the app notified of a retrying CS request?
    bool csretrying;

    // encode/query handle type
    void encodehandletype(handle*, bool);
    bool isprivatehandle(handle*);
    
    // add direct read
    void queueread(handle, bool, SymmCipher*, int64_t, m_off_t, m_off_t, void*);
    
    // execute pending direct reads
    bool execdirectreads();

    // maximum number parallel connections for the direct read subsystem
    static const int MAXDRSLOTS = 16;

    // abort queued direct read(s)
    void abortreads(handle, bool, m_off_t, m_off_t);

public:
    // application callbacks
    struct MegaApp* app;

    // event waiter
    Waiter* waiter;

    // HTTP access
    HttpIO* httpio;

    // directory change notification
    struct FileSystemAccess* fsaccess;

    // bitmap graphics handling
    GfxProc* gfx;
    
    // DB access
    DbAccess* dbaccess;

    // state cache table for logged in user
    DbTable* sctable;

    // scsn as read from sctable
    handle cachedscsn;

    // have we just completed fetching new nodes?
    bool statecurrent;

    // record type indicator for sctable
    enum { CACHEDSCSN, CACHEDNODE, CACHEDUSER, CACHEDLOCALNODE } sctablerectype;

    // initialize/update state cache referenced sctable
    void initsc();
    void updatesc();
    void finalizesc(bool);

    // MegaClient-Server response JSON
    JSON json;

    // Server-MegaClient request JSON and processing state flag ("processing a element")
    JSON jsonsc;
    bool insca;

    // no two interrelated client instances should ever have the same sessionid
    char sessionid[10];

    // application key
    char appkey[16];

    // incoming shares to be attached to a corresponding node
    newshare_list newshares;

    // current request tag
    int reqtag;

    // user maps: by handle and by case-normalized e-mail address
    uh_map uhindex;
    um_map umindex;

    // pending file attributes
    fa_map pendingfa;

    // upload waiting for file attributes
    handletransfer_map faputcompletion;    

    // file attribute fetch channels
    fafc_map fafcs;

    // generate attribute string based on the pending attributes for this upload
    void pendingattrstring(handle, string*);

    // active/pending direct reads
    handledrn_map hdrns;
    dsdrn_map dsdrns;
    dr_list drq;
    drs_list drss;

    // merge newly received share into nodes
    void mergenewshares(bool);

    // transfer queues (PUT/GET)
    transfer_map transfers[2];

    // transfer tslots
    transferslot_list tslots;

    // next TransferSlot to doio() on
    transferslot_list::iterator slotit;

    // FileFingerprint to node mapping
    fingerprint_set fingerprints;

    // asymmetric to symmetric key rewriting
    handle_vector nodekeyrewrite;
    handle_vector sharekeyrewrite;

    static const char* const EXPORTEDLINK;

    // minimum number of bytes in transit for upload/download pipelining
    static const int MINPIPELINE = 65536;

    // initial state load in progress?
    bool fetchingnodes;

    // server-client request sequence number
    char scsn[12];

    bool setscsn(JSON*);

    void purgenodes(node_vector* = NULL);
    void purgeusers(user_vector* = NULL);
    bool readusers(JSON*);

    user_vector usernotify;
    void notifyuser(User*);

    node_vector nodenotify;
    void notifynode(Node*);

    // write changed/added/deleted users to the DB cache and notify the
    // application
    void notifypurge();

    // remove node subtree
    void deltree(handle);

    Node* nodebyhandle(handle);
    Node* nodebyfingerprint(FileFingerprint*);

    // generate & return upload handle
    handle getuploadhandle();

#ifdef ENABLE_SYNC    
    // sync debris folder name in //bin
    static const char* const SYNCDEBRISFOLDERNAME;

    // we are adding the //bin/SyncDebris/yyyy-mm-dd subfolder(s)
    bool syncdebrisadding;

    // activity flag
    bool syncactivity;

    // app scanstate flag
    bool syncscanstate;

    // scan required flag
    bool syncdownrequired;

    // block local fs updates processing while locked ops are in progress
    bool syncfsopsfailed;

    // retry accessing temporarily locked filesystem items
    bool syncfslockretry;
    BackoffTimer syncfslockretrybt;

    // retry of transiently failed local filesystem ops
    bool syncdownretry;
    BackoffTimer syncdownbt;

    // sync PUT Nagle timer
    bool syncnagleretry;
    BackoffTimer syncnaglebt;

    // rescan timer if fs notification unavailable or broken
    bool syncscanfailed;
    BackoffTimer syncscanbt;

    // vanished from a local synced folder
    localnode_set localsyncnotseen;

    // maps local fsid to corresponding LocalNode*
    handlelocalnode_map fsidnode;

    // local nodes that need to be added remotely
    localnode_vector synccreate;

    // number of sync-initiated putnodes() in progress
    int syncadding;

    // sync id dispatch
    handle nextsyncid();
    handle currsyncid;

    // SyncDebris folder addition result
    void putnodes_syncdebris_result(error, NewNode*);

    // if no sync putnodes operation is in progress, apply the updates stored
    // in syncadded/syncdeleted/syncoverwritten to the remote tree
    void syncupdate();

    // create missing folders, copy/start uploading missing files
    void syncup(LocalNode*, dstime*);

    // sync putnodes() completion
    void putnodes_sync_result(error, NewNode*, int);

    // start downloading/copy missing files, create missing directories
    bool syncdown(LocalNode*, string*, bool);

    // move nodes to //bin/SyncDebris/yyyy-mm-dd/ or unlink directly
    void movetosyncdebris(Node*, bool);

    // move queued nodes to SyncDebris (for syncing into the user's own cloud drive)
    void execmovetosyncdebris();
    node_set todebris;

    // unlink queued nodes directly (for inbound share syncing)
    void execsyncunlink();
    node_set tounlink;
    
    // commit all queueud deletions
    void execsyncdeletions();

    // process localnode subtree
    void proclocaltree(LocalNode*, LocalTreeProc*);
#endif

    // recursively cancel transfers in a subtree
    void stopxfers(LocalNode*);

    // update paths of all PUT transfers
    void updateputs();

    // determine if all transfer slots are full
    bool slotavail() const;

    // dispatch as many queued transfers as possible
    void dispatchmore(direction_t);

    // transfer queue dispatch/retry handling
    bool dispatch(direction_t);

    void defer(direction_t, int td, int = 0);
    void freeq(direction_t);

    dstime transferretrydelay();

    // active request buffer
    int r;

    // client-server request double-buffering
    Request reqs[2];

    // upload handle -> node handle map (filled by upload completion)
    handlepair_set uhnh;

    // transfer chunk failed
    void setchunkfailed(string*);
    string badhosts;
    
    // queue for load balancing requests
    std::queue<CommandLoadBalancing*> loadbalancingreqs;

    // process object arrays by the API server
    int readnodes(JSON*, int, putsource_t = PUTNODES_APP, NewNode* = NULL, int = 0, int = 0);

    void readok(JSON*);
    void readokelement(JSON*);
    void readoutshares(JSON*);
    void readoutshareelement(JSON*);

    void readcr();
    void readsr();

    void procsnk(JSON*);
    void procsuk(JSON*);

    void setkey(SymmCipher*, const char*);
    bool decryptkey(const char*, byte*, int, SymmCipher*, int, handle);

    void handleauth(handle, byte*);

    bool procsc();

    // API warnings
    void warn(const char*);
    bool warnlevel();

    Node* childnodebyname(Node*, const char*);

    // purge account state and abort server-client connection
    void purgenodesusersabortsc();

    static const int USERHANDLE = 8;
    static const int NODEHANDLE = 6;

    // session ID length (binary)
    static const unsigned SIDLEN = 2 * SymmCipher::KEYLENGTH + USERHANDLE * 4 / 3 + 1;

    void proccr(JSON*);
    void procsr(JSON*);

    // account access: master key
    // folder link access: folder key
    SymmCipher key;

    // account access (full account): RSA key
    AsymmCipher asymkey;

#ifdef USE_SODIUM
    /// EdDSA signing key (Ed25519 private key seed).
    EdDSA signkey;
#endif

    // binary session ID
    string sid;

    // apply keys
    int applykeys();

    // symmetric password challenge
    int checktsid(byte* sidbuf, unsigned len);

    // locate user by e-mail address or by handle
    User* finduser(const char*, int = 0);
    User* finduser(handle, int = 0);
    void mapuser(handle, const char*);

    // queue public key request for user
    void queuepubkeyreq(User*, PubKeyAction*);

    // rewrite foreign keys of the node (tree)
    void rewriteforeignkeys(Node* n);

    // simple string hash
    static void stringhash(const char*, byte*, SymmCipher*);
    static uint64_t stringhash64(string*, SymmCipher*);

    // set authentication context, either a session ID or a exported folder node handle
    void setsid(const byte*, unsigned);
    void setrootnode(handle);

    // process node subtree
    void proctree(Node*, TreeProc*, bool skipinshares = false);

    // hash password
    error pw_key(const char*, byte*) const;

    // load balancing request
    void loadbalancing(const char *);

    // convert hex digit to number
    static int hexval(char);

    SymmCipher tmpcipher;

    MegaClient(MegaApp*, Waiter*, HttpIO*, FileSystemAccess*, DbAccess*, GfxProc*, const char*, const char*);
    ~MegaClient();
};
} // namespace

#endif
