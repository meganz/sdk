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
#include "transfer.h"
#include "treeproc.h"
#include "sharenodekeys.h"
#include "account.h"
#include "backofftimer.h"
#include "http.h"
#include "pubkeyaction.h"
#include "pendingcontactrequest.h"
#include "mediafileattribute.h"

namespace mega {

class MEGA_API FetchNodesStats
{
public:
    enum {
        MODE_DB = 0,
        MODE_API = 1,
        MODE_NONE = 2
    };

    enum {
        TYPE_ACCOUNT = 0,
        TYPE_FOLDER = 1,
        TYPE_NONE = 2
    };

    enum {
        API_CACHE = 0,
        API_NO_CACHE = 1,    // use this for DB mode
        API_NONE = 2
    };

    FetchNodesStats();
    void init();
    void toJsonArray(string *json);

    //////////////////
    // General info //
    //////////////////
    int mode; // DB = 0, API = 1
    int cache; // no-cache = 0, no-cache = 1
    int type; // Account = 0, Folder = 1
    dstime startTime; // startup time (ds)

    /**
     * \brief Number of nodes in the cached filesystem
     *
     * From DB: number on nodes in the local database
     * From API: number of nodes in the response to the fetchnodes command
     */
    long long nodesCached;

    /**
     * @brief Number of nodes in the current filesystem, after the reception of action packets
     */
    long long nodesCurrent;

    /**
     * @brief Number of action packets to complete the cached filesystem
     *
     * From DB: Number of action packets to complete the local cache
     * From API: Number of action packets to complete the server-side cache
     */
    int actionPackets;

    ////////////
    // Errors //
    ////////////

    /**
     * @brief Number of error -3 or -4 received during the process (including cs and sc requests)
     */
    int eAgainCount;

    /**
     * @brief Number of HTTP 500 errors received during the process (including cs and sc requests)
     */
    int e500Count;

    /**
     * @brief Number of other errors received during the process (including cs and sc requests)
     *
     * The most common source of these errors are connectivity problems (no Internet, timeouts...)
     */
    int eOthersCount;

    ////////////////////////////////////////////////////////////////////
    // Time elapsed until different steps since the startup time (ds) //
    ////////////////////////////////////////////////////////////////////

    /**
     * @brief Time until the first byte read
     *
     * From DB: time until the first record read from the database
     * From API: time until the first byte read in response to the fetchnodes command (errors excluded)
     */
    dstime timeToFirstByte;

    /**
     * @brief Time until the last byte read
     *
     * From DB: time until the last record is read from the database
     * From API: time until the whole response to the fetchnodes command has been received
     */
    dstime timeToLastByte;

    /**
     * @brief Time until the cached filesystem is ready
     *
     * From DB: time until the database has been read and processed
     * From API: time until the fetchnodes command is processed
     */
    dstime timeToCached;

    /**
     * @brief Time until the filesystem is ready to be used
     *
     * From DB: this time is the same as timeToCached
     * From API: time until action packets have been processed
     * It's needed to wait until the reception of action packets due to
     * server-side caches.
     */
    dstime timeToResult;

    /**
     * @brief Time until synchronizations have been resumed
     *
     * This involves the load of the local cache and the scan of known
     * files. Files that weren't cached are scanned later.
     */
    dstime timeToSyncsResumed;

    /**
     * @brief Time until the filesystem is current
     *
     * From DB: time until action packets have been processed
     * From API: this time is the same as timeToResult
     */
    dstime timeToCurrent;

    /**
     * @brief Time until the resumption of transfers has finished
     *
     * The resumption of transfers is done after the filesystem is current
     */
    dstime timeToTransfersResumed;
};

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

#ifdef ENABLE_CHAT
    // all chats
    textchat_map chats;
#endif

    // process API requests and HTTP I/O
    void exec();

    // wait for I/O or other events
    int wait();

    // splitted implementation of wait() for a better thread management
    int preparewait();
    int dowait();
    int checkevents();

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
    void setkeypair();

    // user login: e-mail, pwkey
    void login(const char*, const byte*);

    // user login: e-mail, pwkey, emailhash
    void fastlogin(const char*, const byte*, uint64_t);

    // session login: binary session, bytecount
    void login(const byte*, int);

    // check password
    error validatepwd(const byte *);

    // get user data
    void getuserdata();

    // get the public key of an user
    void getpubkey(const char* user);

    // check if logged in
    sessiontype_t loggedin();

    // dump current session
    int dumpsession(byte*, size_t);

    // create a copy of the current session
    void copysession();

    // get the data for a session transfer
    // the caller takes the ownership of the returned value
    // if the second parameter isn't NULL, it's used as session id instead of the current one
    string *sessiontransferdata(const char*, string* = NULL);

    // Kill session id
    void killsession(handle session);
    void killallsessions();

    // set folder link: node, key
    error folderaccess(const char*folderlink);

    // open exported file link
    error openfilelink(const char*, int);

    // decrypt password-protected public link
    // the caller takes the ownership of the returned value in decryptedLink parameter
    error decryptlink(const char* link, const char* pwd, string *decryptedLink);

    // encrypt public link with password
    // the caller takes the ownership of the returned value
    error encryptlink(const char* link, const char* pwd, string *encryptedLink);

    // change login password
    error changepw(const byte*, const byte*);

    // load all trees: nodes, shares, contacts
    void fetchnodes(bool nocache = false);

    // fetchnodes stats
    FetchNodesStats fnstats;

#ifdef ENABLE_CHAT
    // load cryptographic keys: RSA, Ed25519, Cu25519 and their signatures
    void fetchkeys();    
    void initializekeys();
#endif

    // retrieve user details
    void getaccountdetails(AccountDetails*, bool, bool, bool, bool, bool, bool);

    // check if the available bandwidth quota is enough to transfer an amount of bytes
    void querytransferquota(m_off_t size);

    // update node attributes
    error setattr(Node*, const char* prevattr = NULL);

    // prefix and encrypt attribute json
    void makeattr(SymmCipher*, string*, const char*, int = -1) const;

    // check node access level
    int checkaccess(Node*, accesslevel_t);

    // check if a move operation would succeed
    error checkmove(Node*, Node*);

    // delete node
    error unlink(Node*, bool = false);

    // delete all versions
    void unlinkversions();

    // move node to new parent folder
    error rename(Node*, Node*, syncdel_t = SYNCDEL_NONE, handle = UNDEF);

    // start/stop/pause file transfer
    bool startxfer(direction_t, File*, bool skipdupes = false);
    void stopxfer(File* f);
    void pausexfers(direction_t, bool, bool = false);

    // maximum number of connections per transfer
    static const unsigned MAX_NUM_CONNECTIONS = 6;

    // set max connections per transfer
    void setmaxconnections(direction_t, int);

    // enqueue/abort direct read
    void pread(Node*, m_off_t, m_off_t, void*);
    void pread(handle, SymmCipher* key, int64_t, m_off_t, m_off_t, void*, bool = false);
    void preadabort(Node*, m_off_t = -1, m_off_t = -1);
    void preadabort(handle, m_off_t = -1, m_off_t = -1);

    // pause flags
    bool xferpaused[2];

#ifdef ENABLE_SYNC
    // active syncs
    sync_list syncs;

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
    void putfa(handle, fatype, SymmCipher*, string*, bool checkAccess = true);

    // queue file attribute retrieval
    error getfa(handle h, string *fileattrstring, string *nodekey, fatype, int = 0);
    
    // notify delayed upload completion subsystem about new file attribute
    void checkfacompletion(handle, Transfer* = NULL);

    // attach/update/delete a user attribute
    void putua(attr_t at, const byte* av = NULL, unsigned avl = 0, int ctag = -1);

    // attach/update multiple versioned user attributes at once
    void putua(userattr_map *attrs, int ctag = -1);

    // queue a user attribute retrieval
    void getua(User* u, const attr_t at = ATTR_UNKNOWN, int ctag = -1);

    // queue a user attribute retrieval (for non-contacts)
    void getua(const char* email_handle, const attr_t at = ATTR_UNKNOWN, int ctag = -1);

    // retrieve the email address of a user
    void getUserEmail(const char *uid);

#ifdef DEBUG
    // queue a user attribute removal
    void delua(const char* an);
#endif

    // delete or block an existing contact
    error removecontact(const char*, visibility_t = HIDDEN);

    // add/remove/update outgoing share
    void setshare(Node*, const char*, accesslevel_t, const char* = NULL);

    // Add/delete/remind outgoing pending contact request
    void setpcr(const char*, opcactions_t, const char* = NULL, const char* = NULL);
    void updatepcr(handle, ipcactions_t);

    // export node link or remove existing exported link for this node
    error exportnode(Node*, int, m_time_t);
    void getpubliclink(Node* n, int del, m_time_t ets); // auxiliar method to add req

    // add/delete sync
    error isnodesyncable(Node*, bool* = NULL);
    error addsync(string*, const char*, string*, Node*, fsfp_t = 0, int = 0, void* = NULL);
    void delsync(Sync*, bool = true);

    // close all open HTTP connections
    void disconnect();

    // abort lock request
    void abortlockrequest();

    // abort session and free all state information
    void logout();

    // free all state information
    void locallogout();

    // remove caches
    void removecaches();

    // SDK version
    const char* version();

    // get the last available version of the app
    void getlastversion(const char *appKey);

    // get a local ssl certificate for communications with the webclient
    void getlocalsslcertificate();

    // send a DNS request to resolve a hostname
    void dnsrequest(const char*);

    // send a GeLB request for a service with a timeout (in ms) and a number of retries
    void gelbrequest(const char*, int, int);

    // send chat stats
    void sendchatstats(const char*);

    // send chat logs with user's annonymous id
    void sendchatlogs(const char*, const char*);

    // send a HTTP request
    void httprequest(const char*, int, bool = false, const char* = NULL, int = 1);

    // maximum outbound throughput (per target server)
    int putmbpscap;

    // User-Agent header for HTTP requests
    string useragent;

    // Issuer of a detected fake SSL certificate
    string sslfakeissuer;

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

    // store credit card
    error creditcardstore(const char *);

    // get credit card subscriptions
    void creditcardquerysubscriptions();

    // cancel credit card subscriptions
    void creditcardcancelsubscriptions(const char *reason = NULL);

    // get payment methods
    void getpaymentmethods();

    // store user feedback
    void userfeedbackstore(const char *);

    // send event
    void sendevent(int, const char *);

    // clean rubbish bin
    void cleanrubbishbin();

    // determine if more transfers fit in the pipeline
    bool moretransfers(direction_t);

#ifdef ENABLE_CHAT

    // create a new chat with multiple users and different privileges
    void createChat(bool group, const userpriv_vector *userpriv);

    // invite a user to a chat
    void inviteToChat(handle chatid, handle uh, int priv, const char *title = NULL);

    // remove a user from a chat
    void removeFromChat(handle chatid, handle uh);

    // get the URL of a chat
    void getUrlChat(handle chatid);

    // process object arrays by the API server (users + privileges)
    userpriv_vector * readuserpriv(JSON* j);

    // grant access to a chat peer to one specific node
    void grantAccessInChat(handle chatid, handle h, const char *uid);

    // revoke access to a chat peer to one specific node
    void removeAccessInChat(handle chatid, handle h, const char *uid);

    // update permissions of a peer in a chat
    void updateChatPermissions(handle chatid, handle uh, int priv);

    // truncate chat from message id
    void truncateChat(handle chatid, handle messageid);

    // set title of the chat
    void setChatTitle(handle chatid, const char *title = NULL);

    // get the URL of the presence server
    void getChatPresenceUrl();

    // register a token device to route push notifications
    void registerPushNotification(int deviceType, const char *token = NULL);
#endif

    // get mega achievements
    void getaccountachievements(AchievementsDetails *details);

    // get mega achievements list (for advertising for unregistered users)
    void getmegaachievements(AchievementsDetails *details);

    // get welcome pdf
    void getwelcomepdf();

    // toggle global debug flag
    bool toggledebug();

    bool debugstate();

    // report an event to the API logger
    void reportevent(const char*, const char* = NULL);

    // set max download speed
    bool setmaxdownloadspeed(m_off_t bpslimit);

    // set max upload speed
    bool setmaxuploadspeed(m_off_t bpslimit);

    // get max download speed
    m_off_t getmaxdownloadspeed();

    // get max upload speed
    m_off_t getmaxuploadspeed();

    // get the handle of the older version for a NewNode
    handle getovhandle(Node *parent, string *name);

    // use HTTPS for all communications
    bool usehttps;
    
    // use an alternative port for downloads (8080)
    bool usealtdownport;

    // select the download port automatically
    bool autodownport;

    // use an alternative port for uploads (8080)
    bool usealtupport;

    // select the upload port automatically
    bool autoupport;

    // finish downloaded chunks in order
    bool orderdownloadedchunks;

    // disable public key pinning (for testing purposes)
    static bool disablepkp;

    // retry API_ESSL errors
    bool retryessl;

    // flag to request an extra loop of the SDK to finish something pending
    bool looprequested;

    // timestamp until the bandwidth is overquota in deciseconds, related to Waiter::ds
    m_time_t overquotauntil;

    // root URL for API requests
    static string APIURL;

    // root URL for GeLB requests
    static string GELBURL;

    // root URL for chat stats
    static string CHATSTATSURL;

    // account auth for public folders
    string accountauth;

    // file that is blocking the sync engine
    string blockedfile;

    // stats id
    static char* statsid;

    // number of ongoing asynchronous fopen
    int asyncfopens;

private:
    BackoffTimer btcs;
    BackoffTimer btbadhost;
    BackoffTimer btworkinglock;

    // server-client command trigger connection
    HttpReq* pendingsc;
    BackoffTimer btsc;

    // badhost report
    HttpReq* badhostcs;

    // Working lock
    HttpReq* workinglockcs;

    // notify URL for new server-client commands
    string scnotifyurl;

    // unique request ID
    char reqid[10];

    // auth URI component for API requests
    string auth;

    // lang URI component for API requests
    string lang;

    // public handle being used
    handle publichandle;

    // API response JSON object
    JSON response;

    // response record processing issue
    bool warned;

    // next local user record identifier to use
    int userid;

    // backoff for file attributes
    BackoffTimer btpfa;
    bool faretrying;

    // next internal upload handle
    handle nextuh;

    // maximum number of concurrent transfers (uploads + downloads)
    static const unsigned MAXTOTALTRANSFERS;

    // maximum number of concurrent transfers (uploads or downloads)
    static const unsigned MAXTRANSFERS;

    // maximum number of queued putfa before halting the upload queue
    static const int MAXQUEUEDFA;

    // maximum number of concurrent putfa
    static const int MAXPUTFA;

    // update time at which next deferred transfer retry kicks in
    void nexttransferretry(direction_t d, dstime*);

    // a TransferSlot chunk failed
    bool chunkfailed;
    
    // fetch state serialize from local cache
    bool fetchsc(DbTable*);

    // close the local transfer cache
    void closetc(bool remove = false);

    // server-client command processing
    void sc_updatenode();
    Node* sc_deltree();
    void sc_newnodes();
    void sc_contacts();
    void sc_keys();
    void sc_fileattr();
    void sc_userattr();
    bool sc_shares();
    bool sc_upgrade();
    void sc_opc();
    void sc_ipc();
    void sc_upc();
    void sc_ph();
    void sc_se();
#ifdef ENABLE_CHAT
    void sc_chatupdate();
    void sc_chatnode();
#endif
    void sc_uac();

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

    static const char PAYMENT_PUBKEY[];

public:
    void enabletransferresumption(const char *loggedoutid = NULL);
    void disabletransferresumption(const char *loggedoutid = NULL);

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

    // there is data to commit to the database when possible
    bool pendingsccommit;

    // transfer cache table
    DbTable* tctable;
    // scsn as read from sctable
    handle cachedscsn;

    // have we just completed fetching new nodes?
    bool statecurrent;

    // pending file attribute writes
    putfa_list queuedfa;

    // current file attributes being sent
    putfa_list activefa;

    // API request queue double buffering:
    // reqs[r] is open for adding commands
    // reqs[r^1] is being processed on the API server
    HttpReq* pendingcs;

    // pending HTTP requests
    pendinghttp_map pendinghttp;

    // record type indicator for sctable
    enum { CACHEDSCSN, CACHEDNODE, CACHEDUSER, CACHEDLOCALNODE, CACHEDPCR, CACHEDTRANSFER, CACHEDFILE, CACHEDCHAT } sctablerectype;

    // open/create state cache database table
    void opensctable();

    // initialize/update state cache referenced sctable
    void initsc();
    void updatesc();
    void finalizesc(bool);

    // flag to pause / resume the processing of action packets
    bool scpaused;

    // MegaClient-Server response JSON
    JSON json;

    // Server-MegaClient request JSON and processing state flag ("processing a element")
    JSON jsonsc;
    bool insca;

    // no two interrelated client instances should ever have the same sessionid
    char sessionid[10];

    // session key to protect local storage
    string sessionkey;

    // application key
    char appkey[16];

    // incoming shares to be attached to a corresponding node
    newshare_list newshares;

    // current request tag
    int reqtag;

    // user maps: by handle and by case-normalized e-mail address
    uh_map uhindex;
    um_map umindex;

    // mapping of pending contact handles to their structure
    handlepcr_map pcrindex;

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
    void mergenewshare(NewShare *s, bool notify);    // merge only the given share

    // transfer queues (PUT/GET)
    transfer_map transfers[2];

    // transfer list to manage the priority of transfers
    TransferList transferlist;

    // cached transfers (PUT/GET)
    transfer_map cachedtransfers[2];

    // cached files and their dbids
    vector<string> cachedfiles;
    vector<uint32_t> cachedfilesdbids;

    // database IDs of cached files and transfers
    // waiting for the completion of a putnodes
    pendingdbid_map pendingtcids;

    // path of temporary files
    // waiting for the completion of a putnodes
    pendingfiles_map pendingfiles;

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

    // default number of seconds to wait after a bandwidth overquota
    static dstime DEFAULT_BW_OVERQUOTA_BACKOFF_SECS;

    // initial state load in progress?
    bool fetchingnodes;
    int fetchnodestag;

    // total number of Node objects
    long long totalNodes;

    // server-client request sequence number
    char scsn[12];

    bool setscsn(JSON*);

    void purgenodes(node_vector* = NULL);
    void purgeusers(user_vector* = NULL);
    bool readusers(JSON*);

    user_vector usernotify;
    void notifyuser(User*);

    pcr_vector pcrnotify;
    void notifypcr(PendingContactRequest*);

    node_vector nodenotify;
    void notifynode(Node*);

    // update transfer in the persistent cache
    void transfercacheadd(Transfer*);

    // remove a transfer from the persistent cache
    void transfercachedel(Transfer*);

    // add a file to the persistent cache
    void filecacheadd(File*);

    // remove a file from the persistent cache
    void filecachedel(File*);

#ifdef ENABLE_CHAT
    textchat_map chatnotify;
    void notifychat(TextChat *);
#endif

#ifdef USE_MEDIAINFO
    MediaFileInfo mediaFileInfo;
#endif

    // write changed/added/deleted users to the DB cache and notify the
    // application
    void notifypurge();

    // remove node subtree
    void deltree(handle);

    Node* nodebyhandle(handle);
    Node* nodebyfingerprint(FileFingerprint*);
    node_vector *nodesbyfingerprint(FileFingerprint* fingerprint);

    // generate & return upload handle
    handle getuploadhandle();

#ifdef ENABLE_SYNC    
    // sync debris folder name in //bin
    static const char* const SYNCDEBRISFOLDERNAME;

    // we are adding the //bin/SyncDebris/yyyy-mm-dd subfolder(s)
    bool syncdebrisadding;

    // minute of the last created folder in SyncDebris
    m_time_t syncdebrisminute;

    // activity flag
    bool syncactivity;

    // syncops indicates that a sync-relevant tree update may be pending
    bool syncops;

    // app scanstate flag
    bool syncscanstate;

    // scan required flag
    bool syncdownrequired;

    bool syncuprequired;

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

    // timer for extra notifications
    // (workaround for buggy network filesystems)
    bool syncextraretry;
    BackoffTimer syncextrabt;

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

    // total number of LocalNode objects
    long long totalLocalNodes;

    // sync id dispatch
    handle nextsyncid();
    handle currsyncid;

    // SyncDebris folder addition result
    void putnodes_syncdebris_result(error, NewNode*);

    // if no sync putnodes operation is in progress, apply the updates stored
    // in syncadded/syncdeleted/syncoverwritten to the remote tree
    void syncupdate();

    // create missing folders, copy/start uploading missing files
    bool syncup(LocalNode*, dstime*);

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

    // client-server request double-buffering
    RequestDispatcher reqs;

    // upload handle -> node handle map (filled by upload completion)
    handlepair_set uhnh;

    // transfer chunk failed
    void setchunkfailed(string*);
    string badhosts;

    bool requestLock;
    dstime disconnecttimestamp;

    // process object arrays by the API server
    int readnodes(JSON*, int, putsource_t = PUTNODES_APP, NewNode* = NULL, int = 0, int = 0);

    void readok(JSON*);
    void readokelement(JSON*);
    void readoutshares(JSON*);
    void readoutshareelement(JSON*);

    void readipc(JSON*);
    void readopc(JSON*);

    void procph(JSON*);

    void readcr();
    void readsr();

    void procsnk(JSON*);
    void procsuk(JSON*);

    void procmcf(JSON*);
    void procmcna(JSON*);

    void setkey(SymmCipher*, const char*);
    bool decryptkey(const char*, byte*, int, SymmCipher*, int, handle);

    void handleauth(handle, byte*);

    bool procsc();

    // API warnings
    void warn(const char*);
    bool warnlevel();

    Node* childnodebyname(Node*, const char*, bool = false);

    // purge account state and abort server-client connection
    void purgenodesusersabortsc();

    static const int USERHANDLE = 8;
    static const int PCRHANDLE = 8;
    static const int NODEHANDLE = 6;
    static const int CHATHANDLE = 8;
    static const int SESSIONHANDLE = 8;
    static const int PURCHASEHANDLE = 8;

    // max new nodes per request
    static const int MAX_NEWNODES = 2000;

    // session ID length (binary)
    static const unsigned SIDLEN = 2 * SymmCipher::KEYLENGTH + USERHANDLE * 4 / 3 + 1;

    void proccr(JSON*);
    void procsr(JSON*);

    // account access: master key
    // folder link access: folder key
    SymmCipher key;

    // dummy key to obfuscate non protected cache
    SymmCipher tckey;

    // account access (full account): RSA private key
    AsymmCipher asymkey;

#ifdef ENABLE_CHAT
    // RSA public key
    AsymmCipher pubk;

    // EdDSA signing key (Ed25519 private key seed).
    EdDSA *signkey;

    // ECDH key (x25519 private key).
    ECDH *chatkey;

    // actual state of keys
    bool fetchingkeys;

    // invalidate received keys (when fail to load)
    void clearKeys();

    // delete chatkey and signing key
    void resetKeyring();
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
    User* ownuser();
    void mapuser(handle, const char*);
    void discarduser(handle);
    void discarduser(const char*);
    void mappcr(handle, PendingContactRequest*);
    bool discardnotifieduser(User *);

    PendingContactRequest* findpcr(handle);

    // queue public key request for user
    void queuepubkeyreq(User*, PubKeyAction*);
    void queuepubkeyreq(const char*, PubKeyAction*);

    // rewrite foreign keys of the node (tree)
    void rewriteforeignkeys(Node* n);

    // simple string hash
    static void stringhash(const char*, byte*, SymmCipher*);
    static uint64_t stringhash64(string*, SymmCipher*);

    // set authentication context, either a session ID or a exported folder node handle
    void setsid(const byte*, unsigned);
    void setrootnode(handle);

    bool setlang(string *code);

    // returns the handle of the root node if the account is logged into a public folder, otherwise UNDEF.
    handle getrootpublicfolder();

    // returns the public handle of the folder link if the account is logged into a public folder, otherwise UNDEF.
    handle getpublicfolderhandle();

    // process node subtree
    void proctree(Node*, TreeProc*, bool skipinshares = false, bool skipversions = false);

    // hash password
    error pw_key(const char*, byte*) const;

    // convert hex digit to number
    static int hexval(char);

    SymmCipher tmpnodecipher;
    SymmCipher tmptransfercipher;

    void exportDatabase(string filename);
    bool compareDatabases(string filename1, string filename2);

    // request a link to recover account
    void getrecoverylink(const char *email, bool hasMasterkey);

    // query information about recovery link
    void queryrecoverylink(const char *link);

    // request private key for integrity checking the masterkey
    void getprivatekey(const char *code);

    // confirm a recovery link to restore the account
    void confirmrecoverylink(const char *code, const char *email, const byte *pwkey, const byte *masterkey = NULL);

    // request a link to cancel the account
    void getcancellink(const char *email);

    // confirm a link to cancel the account
    void confirmcancellink(const char *code);

    // get a link to change the email address
    void getemaillink(const char *email);

    // confirm a link to change the email address
    void confirmemaillink(const char *code, const char *email, const byte *pwkey);

    // achievements enabled for the account
    bool achievements_enabled;

    // non-zero if login with user+pwd was done (reset upon fetchnodes completion)
    bool tsLogin;

    // true if user has disabled fileversioning
    bool versions_disabled;

    MegaClient(MegaApp*, Waiter*, HttpIO*, FileSystemAccess*, DbAccess*, GfxProc*, const char*, const char*);
    ~MegaClient();
};
} // namespace

#endif
