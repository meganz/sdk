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
#include "useralerts.h"
#include "user.h"
#include "sync.h"
#include "drivenotify.h"
#include "setandelement.h"

namespace mega {

class Logger;
class SyncConfigBag;

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

/**
 * @brief A helper class that keeps the SN (sequence number) members in sync and well initialized.
 *  The server-client sequence number is updated along with every batch of actionpackets received from API
 *  It is used to commit the open transaction in DB, so the account's local state is persisted. Upon resumption,
 *  the scsn is sent to API, which provides the possible updates missing while the client was not running
 */
class SCSN
{
    // scsn that we are sending in sc requests (ie, where we are up to with the persisted node data)
    char scsn[12];

    // sc inconsistency: stop querying for action packets
    bool stopsc = false;

public:

    bool setScsn(JSON*);
    void setScsn(handle);
    void stopScsn();

    bool ready() const;
    bool stopped() const;

    const char* text() const;
    handle getHandle() const;

    friend std::ostream& operator<<(std::ostream& os, const SCSN& scsn);

    SCSN();
    void clear();
};

std::ostream& operator<<(std::ostream &os, const SCSN &scsn);

struct SyncdownContext
{
    bool mBackupActionsPerformed = false;
    bool mBackupForeignChangeDetected = false;
}; // SyncdownContext

// Class to help with upload of file attributes
struct UploadWaitingForFileAttributes
{
    struct FileAttributeValues {
        handle fileAttributeHandle = UNDEF;
        bool valueIsSet = false;
    };

    mapWithLookupExisting<fatype, FileAttributeValues> pendingfa;

    // The transfer must always be known, so we can check for cancellation
    Transfer* transfer = nullptr;

    // This flag is set true if its data upload completes, and we removed it from transfers[]
    // In which case, this is now the "owning" object for the transfer
    bool uploadCompleted = false;
};

// Class to help with upload of file attributes
// One entry for each active upload that has file attribute involvement
// Should the transfer be cancelled, this data structure is easily cleaned.
struct FileAttributesPending : public mapWithLookupExisting<UploadHandle, UploadWaitingForFileAttributes>
{
    void setFileAttributePending(UploadHandle h, fatype type, Transfer* t, bool alreadyavailable = false)
    {
        auto& entry = operator[](h);
        entry.pendingfa[type].valueIsSet = alreadyavailable;
        assert(entry.transfer == t || entry.transfer == nullptr);
        entry.transfer = t;
    }
};


class MEGA_API MegaClient
{
public:
    // own identity
    handle me;
    string uid;

    // root nodes (files, vault, rubbish)
    struct Rootnodes
    {
        NodeHandle files;
        NodeHandle vault;
        NodeHandle rubbish;

        // returns true if the 'h' provided matches any of the rootnodes.
        // (when logged into folder links, the handle of the folder is set to 'files')
        bool isRootNode(NodeHandle h) { return (h == files || h == vault || h == rubbish); }
    } rootnodes;


    // all nodes
    node_map nodes;

    // keep track of user storage, inshare storage, file/folder counts per root node.
    NodeCounterMap mNodeCounters;

    // all users
    user_map users;

    // encrypted master key
    string k;

    // version of the account
    int accountversion;

    // salt of the account (for v2 accounts)
    string accountsalt;

    // timestamp of the creation of the account
    m_time_t accountsince;

    // Global Multi-Factor Authentication enabled
    bool gmfa_enabled;

    // Server-Side Rubbish-bin Scheduler enabled (autopurging)
    bool ssrs_enabled;

    // Account has VOIP push enabled (only for Apple)
    bool aplvp_enabled;

    // Use new format to generate Mega links
    bool mNewLinkFormat = false;

    // Don't start showing the cookie banner until API says so
    bool mCookieBannerEnabled = false;

    // 2 = Opt-in and unblock SMS allowed 1 = Only unblock SMS allowed 0 = No SMS allowed  -1 = flag was not received
    SmsVerificationState mSmsVerificationState;

    // the verified account phone number, filled in from 'ug'
    string mSmsVerifiedPhone;

    // pseudo-random number generator
    PrnGen rng;

    bool ephemeralSession = false;
    bool ephemeralSessionPlusPlus = false;

    static string publicLinkURL(bool newLinkFormat, nodetype_t type, handle ph, const char *key);

    string getWritableLinkAuthKey(handle node);

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
    void createephemeralPlusPlus();
    void resumeephemeral(handle, const byte*, int = 0);
    void resumeephemeralPlusPlus(const std::string& session);
    void cancelsignup();

    // full account confirmation/creation support
    string sendsignuplink2(const char*, const char *, const char*);
    void resendsignuplink2(const char*, const char *);

    void confirmsignuplink2(const byte*, unsigned);
    void setkeypair();

    // prelogin: e-mail
    void prelogin(const char*);

    // user login: e-mail, pwkey
    void login(const char*, const byte*, const char* = NULL);

    // user login: e-mail, password, salt
    void login2(const char*, const char*, string *, const char* = NULL);

    // user login: e-mail, derivedkey, 2FA pin
    void login2(const char*, const byte*, const char* = NULL);

    // user login: e-mail, pwkey, emailhash
    void fastlogin(const char*, const byte*, uint64_t);

    // session login: binary session, bytecount
    void login(string session);

    // check password
    error validatepwd(const byte *);

    // get user data
    void getuserdata(int tag, std::function<void(string*, string*, string*, error)> = nullptr);

    // get miscelaneous flags
    void getmiscflags();

    // get the public key of an user
    void getpubkey(const char* user);

    // check if logged in
    sessiontype_t loggedin();

    // provide state by change callback
    void reportLoggedInChanges();
    sessiontype_t mLastLoggedInReportedState = NOTLOGGEDIN;
    handle mLastLoggedInMeHandle = UNDEF;

    // check if logged in a folder link
    bool loggedinfolderlink();

    // check the reason of being blocked
    void whyamiblocked();

    // sets block state: stops querying for action packets, pauses transfer & removes transfer slot availability
    void block(bool fromServerClientResponse = false);

    // unsets block state
    void unblock();

    // dump current session
    int dumpsession(string&);

    // create a copy of the current session. EACCESS for not fully confirmed accounts
    error copysession();

    // resend the verification email to the same email address as it was previously sent to
    void resendverificationemail();

    // reset the verified phone number
    void resetSmsVerifiedPhoneNumber();

    // get the data for a session transfer
    // the caller takes the ownership of the returned value
    string sessiontransferdata(const char*, string*);

    // Kill session id
    void killsession(handle session);
    void killallsessions();

    // extract public handle and key from a public file/folder link
    error parsepubliclink(const char *link, handle &ph, byte *key, bool isFolderLink);

    // open the SC database and get the SCSN from it
    void checkForResumeableSCDatabase();

    // set folder link: node, key. authKey is the authentication key to be able to write into the folder
    error folderaccess(const char*folderlink, const char* authKey);

    // open exported file link (op=0 -> download, op=1 fetch data)
    void openfilelink(handle ph, const byte *key);

    // decrypt password-protected public link
    // the caller takes the ownership of the returned value in decryptedLink parameter
    error decryptlink(const char* link, const char* pwd, string *decryptedLink);

    // encrypt public link with password
    // the caller takes the ownership of the returned value
    error encryptlink(const char* link, const char* pwd, string *encryptedLink);

    // change login password
    error changepw(const char *password, const char *pin = NULL);

    // load all trees: nodes, shares, contacts
    void fetchnodes(bool nocache = false);

    // fetchnodes stats
    FetchNodesStats fnstats;

    // load cryptographic keys: RSA, Ed25519, Cu25519 and their signatures
    void fetchkeys();

    // check existence and integrity of keys and signatures, initialize if missing
    void initializekeys();

    // to be called after resumption from cache (user attributes loaded)
    void loadAuthrings();

    // load cryptographic keys for contacts: RSA, Ed25519, Cu25519
    void fetchContactsKeys();

    // fetch keys related to authrings for a given contact
    void fetchContactKeys(User *user);

    // track a public key in the authring for a given user
    error trackKey(attr_t keyType, handle uh, const std::string &key);

    // track the signature of a public key in the authring for a given user
    error trackSignature(attr_t signatureType, handle uh, const std::string &signature);

    // set the Ed25519 public key as verified for a given user in the authring (done by user manually by comparing hash of keys)
    error verifyCredentials(handle uh);

    // reset the tracking of public keys in the authrings for a given user
    error resetCredentials(handle uh);

    // check credentials are verified for a given user
    bool areCredentialsVerified(handle uh);

    // retrieve user details
    void getaccountdetails(std::shared_ptr<AccountDetails>, bool, bool, bool, bool, bool, bool, int source = -1);

    // check if the available bandwidth quota is enough to transfer an amount of bytes
    void querytransferquota(m_off_t size);

    // update node attributes
    error setattr(Node*, attr_map&& updates, int reqtag, const char* prevattr, CommandSetAttr::Completion&& c, bool canChangeVault);

    // prefix and encrypt attribute json
    static void makeattr(SymmCipher*, string*, const char*, int = -1);

    // convenience version of the above (frequently we are passing a NodeBase's attrstring)
    static void makeattr(SymmCipher*, const std::unique_ptr<string>&, const char*, int = -1);

    // check node access level
    int checkaccess(Node*, accesslevel_t);

    // check if a move operation would succeed
    error checkmove(Node*, Node*);

    // delete node
    error unlink(Node*, bool keepversions, int tag, bool canChangeVault, std::function<void(NodeHandle, Error)>&& resultFunction = nullptr);

    void unlinkOrMoveBackupNodes(NodeHandle backupRootNode, NodeHandle destination, std::function<void(Error)> completion);

    // delete all versions
    void unlinkversions();

    // move node to new parent folder
    error rename(Node*, Node*, syncdel_t, NodeHandle prevparent, const char *newName, bool canChangeVault, CommandMoveNode::Completion&& c);

    // Queue commands (if needed) to remvoe any outshares (or pending outshares) below the specified node
    void removeOutSharesFromSubtree(Node* n, int tag);

    // start/stop/pause file transfer
    bool startxfer(direction_t, File*, TransferDbCommitter&, bool skipdupes, bool startfirst, bool donotpersist, VersioningOption, error* cause = nullptr);
    void stopxfer(File* f, TransferDbCommitter* committer);
    void pausexfers(direction_t, bool pause, bool hard, TransferDbCommitter& committer);

    // maximum number of connections per transfer
    static const unsigned MAX_NUM_CONNECTIONS = 6;

    // set max connections per transfer
    void setmaxconnections(direction_t, int);

    // updates business status
    void setBusinessStatus(BizStatus newBizStatus);

    // updates block boolean
    void setBlocked(bool value);

    // enqueue/abort direct read
    void pread(Node*, m_off_t, m_off_t, void*);
    void pread(handle, SymmCipher* key, int64_t, m_off_t, m_off_t, void*, bool = false,  const char* = NULL, const char* = NULL, const char* = NULL);
    void preadabort(Node*, m_off_t = -1, m_off_t = -1);
    void preadabort(handle, m_off_t = -1, m_off_t = -1);

    // pause flags
    bool xferpaused[2];

    MegaClientAsyncQueue mAsyncQueue;

    // number of parallel connections per transfer (PUT/GET)
    unsigned char connections[2];

    // helpfer function for preparing a putnodes call for new node
    error putnodes_prepareOneFile(NewNode* newnode, Node* parentNode, const char *utf8Name, const UploadToken& binaryUploadToken,
                                  byte *theFileKey, char *megafingerprint, const char *fingerprintOriginal,
                                  std::function<error(AttrMap&)> addNodeAttrsFunc = nullptr,
                                  std::function<error(std::string *)> addFileAttrsFunc = nullptr);

    // helper function for preparing a putnodes call for new folders
    void putnodes_prepareOneFolder(NewNode* newnode, std::string foldername, bool canChangeVault, std::function<void (AttrMap&)> addAttrs = nullptr);

    // static version to be used from worker threads, which cannot rely on the MegaClient::tmpnodecipher as SymCipher (not thread-safe))
    static void putnodes_prepareOneFolder(NewNode* newnode, std::string foldername, PrnGen& rng, SymmCipher &tmpnodecipher, bool canChangeVault, std::function<void(AttrMap&)> addAttrs = nullptr);

    // add nodes to specified parent node (complete upload, copy files, make
    // folders)
    void putnodes(NodeHandle, VersioningOption vo, vector<NewNode>&&, const char *, int tag, bool canChangeVault, CommandPutNodes::Completion&& completion = nullptr);

    // send files/folders to user
    void putnodes(const char*, vector<NewNode>&&, int tag, CommandPutNodes::Completion&& completion = nullptr);

    // attach file attribute to upload or node handle
    void putfa(NodeOrUploadHandle, fatype, SymmCipher*, int tag, std::unique_ptr<string>);

    // queue file attribute retrieval
    error getfa(handle h, string *fileattrstring, const string &nodekey, fatype, int = 0);

    // notify delayed upload completion subsystem about new file attribute
    void checkfacompletion(UploadHandle, Transfer* = NULL, bool uploadCompleted = false);

    // attach/update/delete a user attribute
    void putua(attr_t at, const byte* av = NULL, unsigned avl = 0, int ctag = -1, handle lastPublicHandle = UNDEF, int phtype = 0, int64_t ts = 0,
        std::function<void(Error)> completion = nullptr);

    // attach/update multiple versioned user attributes at once
    void putua(userattr_map *attrs, int ctag = -1);

    // queue a user attribute retrieval
    void getua(User* u, const attr_t at = ATTR_UNKNOWN, int ctag = -1);

    // queue a user attribute retrieval (for non-contacts)
    void getua(const char* email_handle, const attr_t at = ATTR_UNKNOWN, const char *ph = NULL, int ctag = -1);

    // retrieve the email address of a user
    void getUserEmail(const char *uid);

#ifdef DEBUG
    // queue a user attribute removal
    void delua(const char* an);

    // send dev command for testing
    void senddevcommand(const char *command, const char *email, long long q = 0, int bs = 0, int us = 0);
#endif

    // delete or block an existing contact
    error removecontact(const char*, visibility_t = HIDDEN, CommandRemoveContact::Completion completion = nullptr);

    // add/remove/update outgoing share
    void setshare(Node*, const char*, accesslevel_t, bool writable, const char*,
        int tag, std::function<void(Error, bool writable)> completion);

    // Add/delete/remind outgoing pending contact request
    void setpcr(const char*, opcactions_t, const char* = NULL, const char* = NULL, handle = UNDEF, CommandSetPendingContact::Completion completion = nullptr);
    void updatepcr(handle, ipcactions_t, CommandUpdatePendingContact::Completion completion = nullptr);

    // export node link or remove existing exported link for this node
    error exportnode(Node*, int, m_time_t, bool writable, bool megaHosted,
        int tag, std::function<void(Error, handle, handle)> completion);
    void requestPublicLink(Node* n, int del, m_time_t ets, bool writable, bool megaHosted,
	    int tag, std::function<void(Error, handle, handle)> completion); // auxiliar method to add req

    // add timer
    error addtimer(TimerWithBackoff *twb);

#ifdef ENABLE_SYNC
    /**
     * @brief is node syncable
     * @param isinshare filled with whether the node is within an inshare.
     * @param syncError filled with SyncError with the sync error that makes the node unsyncable
     * @return API_OK if syncable. (regular) error otherwise
     */
    error isnodesyncable(Node*, bool * isinshare = NULL, SyncError *syncError = nullptr);

    /**
     * @brief is local path syncable
     * @param newPath path to check
     * @param excludeBackupId backupId to exclude in checking (that of the new sync)
     * @param syncError filled with SyncError with the sync error that makes the node unsyncable
     * @return API_OK if syncable. (regular) error otherwise
     */
    error isLocalPathSyncable(const LocalPath& newPath, handle excludeBackupId = UNDEF, SyncError *syncError = nullptr);

    /**
     * @brief check config. Will fill syncError in the SyncConfig in case there is one.
     * Will fill syncWarning in the SyncConfig in case there is one.
     * Does not persist the sync configuration.
     * Does not add the syncConfig.
     * Reference parameters are filled in while checking syncConfig, for the benefit of addSync() which calls it.
     * @return And error code if there are problems serious enough with the syncconfig that it should not be added.
     *         Otherwise, API_OK
     */
    error checkSyncConfig(SyncConfig& syncConfig, LocalPath& rootpath, std::unique_ptr<FileAccess>& openedLocalFolder, bool& inshare, bool& isnetwork);

    /**
     * @brief add sync. Will fill syncError/syncWarning in the SyncConfig in case there are any.
     * It will persist the sync configuration if its call to checkSyncConfig succeeds
     * @param syncConfig the Config to attempt to add (takes ownership)
     * @param notifyApp whether the syncupdate_stateconfig callback should be called at this stage or not
     * @param completion Completion function
     * @exludedPath: in sync rework, use this to specify a folder within the sync to exclude (eg, working folder with sync db in it)
     * @return API_OK if added to active syncs. (regular) error otherwise (with detail in syncConfig's SyncError field).
     * Completion is used to signal success/failure.  That may occur during this call, or in future (after server request/reply etc)
     */
    void addsync(SyncConfig&& syncConfig, bool notifyApp, std::function<void(error, SyncError, handle)> completion, const string& logname, const string& excludedPath = string());

    /**
     * @brief
     * Create the remote backup dir under //in/"My Backups"/`DEVICE_NAME`/. If `DEVICE-NAME` folder is missing, create that first.
     *
     * @param bkpName
     * The name of the remote backup dir (desired final outcome is //in/"My Backups"/`DEVICE_NAME`/bkpName)
     *
     * @param extDriveRoot
     * Drive root in case backup is from external drive; empty otherwise
     *
     * @param completion
     * Completion function
     *
     * @return
     * API_OK if remote backup dir has been successfully created.
     * API_EACCESS if "My Backups" handle could not be obtained from user attribute, or if `DEVICE_NAME` was not a dir,
     * or if remote backup dir already existed.
     * API_ENOENT if "My Backups" handle was invalid or its Node was missing.
     * API_EINCOMPLETE if device-id or device-name could not be obtained
     * Any error returned by readDriveId(), in case of external drive.
     * Registration occurs later, during addsync(), not in this function.
     * UndoFunction will be passed on completion, the caller can use it to remove the new backup cloud node if there is a later failure.
     */
    typedef std::function<void(std::function<void()> continuation)> UndoFunction;
    void preparebackup(SyncConfig, std::function<void(Error, SyncConfig, UndoFunction revertOnError)>);

    void copySyncConfig(const SyncConfig& config, std::function<void(handle, error)> completion);

    /**
     * @brief
     * Import sync configs from JSON.
     *
     * @param configs
     * A JSON string encoding the sync configs to import.
     *
     * @param completion
     * The function to call when we've completed importing the configs.
     *
     * @see MegaApi::exportSyncConfigs
     * @see MegaApi::importSyncConfigs
     * @see Syncs::exportSyncConfig
     * @see Syncs::exportSyncConfigs
     * @see Syncs::importSyncConfig
     * @see Syncs::importSyncConfigs
     */
    void importSyncConfigs(const char* configs, std::function<void(error)> completion);

    /**
     * @brief This method ensures that sync user attributes are available.
     *
     * This method calls \c completion function when it finishes, with the
     * corresponding error if was not possible to ensure the attrs are available.
     *
     * Note that it may also need to create certain attributes, like *~jscd, if they
     * don't exist yet.
     *
     * @param completion Function that is called when completed
     */
    void ensureSyncUserAttributes(std::function<void(Error)> completion);

private:
    void ensureSyncUserAttributesCompleted(Error e);
    std::function<void(Error)> mOnEnsureSyncUserAttributesComplete;

public:

    // disable synchronization. syncError specifies why we are disabling it.
    // newEnabledFlag specifies whether we will try to auto-resume it on eg. app restart
    void disableSyncContainingNode(NodeHandle nodeHandle, SyncError syncError, bool newEnabledFlag);

#endif  // ENABLE_SYNC

    /**
     * @brief creates a tlv with one record and returns it encrypted with master key
     * @param name name of the record
     * @param text value of the record
     * @return encrypted base64 string with the tlv contents
     */
    std::string cypherTLVTextWithMasterKey(const char* name, const std::string& text);
    std::string decypherTLVTextWithMasterKey(const char* name, const std::string& text);

    // close all open HTTP connections
    void disconnect();

    // close server-client HTTP connection
    void catchup();
    // abort lock request
    void abortlockrequest();

    // abort session and free all state information
    void logout(bool keepSyncConfigsFile, CommandLogout::Completion completion = nullptr);

    // free all state information
    void locallogout(bool removecaches, bool keepSyncsConfigFile);

    // SDK version
    const char* version();

    // get the last available version of the app
    void getlastversion(const char *appKey);

    // get a local ssl certificate for communications with the webclient
    void getlocalsslcertificate();

    // send a DNS request to resolve a hostname
    void dnsrequest(const char*);

    // send chat stats
    void sendchatstats(const char*, int port);

    // send chat logs with user's annonymous id
    void sendchatlogs(const char*, mega::handle userid, mega::handle callid, int port);

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
    void purchase_additem(int, handle, unsigned, const char *, unsigned, const char *, handle = UNDEF, int = 0, int64_t = 0);

    // submit purchased products for payment
    void purchase_checkout(int);

    // submit purchase receipt for verification
    void submitpurchasereceipt(int, const char*, handle lph = UNDEF, int phtype = 0, int64_t ts = 0);

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
    void sendevent(int, const char *, int tag);

    // create support ticket
    void supportticket(const char *message, int type);

    // clean rubbish bin
    void cleanrubbishbin();

    // change the storage status
    bool setstoragestatus(storagestatus_t);

    // get info about a folder link
    void getpubliclinkinfo(handle h);

    // send an sms to verificate a phone number (returns EARGS if phone number has invalid format)
    error smsverificationsend(const string& phoneNumber, bool reVerifyingWhitelisted = false);

    // check the verification code received by sms is valid (returns EARGS if provided code has invalid format)
    error smsverificationcheck(const string& verificationCode);

#ifdef ENABLE_CHAT

    // create a new chat with multiple users and different privileges
    void createChat(bool group, bool publicchat, const userpriv_vector* userpriv = NULL, const string_map* userkeymap = NULL, const char* title = NULL, bool meetingRoom = false, int chatOptions = ChatOptions::kEmpty);

    // invite a user to a chat
    void inviteToChat(handle chatid, handle uh, int priv, const char *unifiedkey = NULL, const char *title = NULL);

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

    void archiveChat(handle chatid, bool archived);

    // request meta information from an url (title, description, icon)
    void richlinkrequest(const char*);

    // create/get or delete chat-link
    void chatlink(handle chatid, bool del, bool createifmissing);

    // get the URL for chat-link
    void chatlinkurl(handle publichandle);

    // convert public chat into private chat
    void chatlinkclose(handle chatid, const char *title);

    // auto-join publicchat
    void chatlinkjoin(handle publichandle, const char *unifiedkey);

    // set retention time for a chatroom in seconds, after which older messages in the chat are automatically deleted
    void setchatretentiontime(handle chatid, unsigned period);
#endif

    // get mega achievements
    void getaccountachievements(AchievementsDetails *details);

    // get mega achievements list (for advertising for unregistered users)
    void getmegaachievements(AchievementsDetails *details);

    // get welcome pdf
    void getwelcomepdf();

    // report an event to the API logger
    void reportevent(const char*, const char* = NULL);
    void reportevent(const char*, const char*, int tag);

    // set max download speed
    bool setmaxdownloadspeed(m_off_t bpslimit);

    // set max upload speed
    bool setmaxuploadspeed(m_off_t bpslimit);

    // get max download speed
    m_off_t getmaxdownloadspeed();

    // get max upload speed
    m_off_t getmaxuploadspeed();

    // get the handle of the older version for a NewNode
    Node* getovnode(Node *parent, string *name);

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

    // retry API_ESSL errors
    bool retryessl;

    // flag to request an extra loop of the SDK to finish something pending
    bool looprequested;

    // timestamp until the bandwidth is overquota in deciseconds, related to Waiter::ds
    m_time_t overquotauntil;

    // storage status
    storagestatus_t ststatus;

    class CacheableStatusMap : private map<int64_t, CacheableStatus>
    {
    public:
        CacheableStatusMap(MegaClient *client) { mClient = client; }

        // returns the cached value for type, or defaultValue if not found
        int64_t lookup(CacheableStatus::Type type, int64_t defaultValue);

        // add/update cached status, both in memory and DB
        bool addOrUpdate(CacheableStatus::Type type, int64_t value);

        // adds a new item to the map. It also initializes dedicated vars in the client (used to load from DB)
        void loadCachedStatus(CacheableStatus::Type type, int64_t value);

        // for unserialize
        CacheableStatus *getPtr(CacheableStatus::Type type);

        void clear() { map::clear(); }

    private:
        MegaClient *mClient = nullptr;
    };

    // cacheable status
    CacheableStatusMap mCachedStatus;

    // warning timestamps related to storage overquota in paywall mode
    vector<m_time_t> mOverquotaWarningTs;

    // deadline timestamp related to storage overquota in paywall mode
    m_time_t mOverquotaDeadlineTs;

    // minimum bytes per second for streaming (0 == no limit, -1 == use default)
    int minstreamingrate;

    // user handle for customer support user
    static const string SUPPORT_USER_HANDLE;

    // root URL for chat stats
    static const string SFUSTATSURL;

    // root URL for Website
    static const string MEGAURL;

    // newsignup link URL prefix
    static const char* newsignupLinkPrefix();

    // confirm link URL prefix
    static const char* confirmLinkPrefix();

    // verify link URL prefix
    static const char* verifyLinkPrefix();

    // recover link URL prefix
    static const char* recoverLinkPrefix();

    // cancel link URL prefix
    static const char* cancelLinkPrefix();

    // file that is blocking the sync engine
    LocalPath blockedfile;

    // stats id
    std::string statsid;

    // number of ongoing asynchronous fopen
    int asyncfopens;

    // list of notifications to display to the user; includes items already seen
    UserAlerts useralerts;

    // true if user data is cached
    bool cachedug;

    // backoff for the expiration of cached user data
    BackoffTimer btugexpiration;

    // if logged into public folder (which might optionally be writable)
    bool loggedIntoFolder() const;

    // if logged into writable folder
    bool loggedIntoWritableFolder() const;

    // start receiving external drive [dis]connect notifications
    bool startDriveMonitor();

    // stop receiving external drive [dis]connect notifications
    void stopDriveMonitor();

    // returns true if drive monitor is started
    bool driveMonitorEnabled();

private:
#ifdef USE_DRIVE_NOTIFICATIONS
    DriveInfoCollector mDriveInfoCollector;
#endif
    BackoffTimer btcs;
    BackoffTimer btbadhost;
    BackoffTimer btworkinglock;

    vector<TimerWithBackoff *> bttimers;

    // server-client command trigger connection
    std::unique_ptr<HttpReq> pendingsc;
    std::unique_ptr<HttpReq> pendingscUserAlerts;
    BackoffTimer btsc;

    // account is blocked: stops querying for action packets, pauses transfer & removes transfer slot availability
    bool mBlocked = false;
    bool mBlockedSet = false; //value set in current execution

    bool pendingscTimedOut = false;

    // badhost report
    HttpReq* badhostcs;

    // Working lock
    unique_ptr<HttpReq> workinglockcs;

    // notify URL for new server-client commands
    string scnotifyurl;

    // unique request ID
    char reqid[10];

    // lang URI component for API requests
    string lang;

    struct FolderLink {
        // public handle of the folder link ('&n=' param in the POST)
        handle mPublicHandle = UNDEF;

        // auth token that enables writing into the folder link (appended to the `n` param in POST)
        string mWriteAuth;      // (optional, only for writable links)

        // auth token that relates the usage of the folder link to a user's session id ('&sid=' param in the POST)
        string mAccountAuth;    // (optional, set by the app)
    };
    FolderLink mFolderLink;

    // API response JSON object
    JSON response;

    // response record processing issue
    bool warned;

    // next local user record identifier to use
    int userid;

    // backoff for file attributes
    BackoffTimer btpfa;
    bool faretrying;

    // next internal upload handle (call UploadHandle::next() to update value)
    UploadHandle mUploadHandle;

    // just one notification after fetchnodes and catch-up actionpackets
    bool notifyStorageChangeOnStateCurrent = false;

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

    // fetch statusTable from local cache
    bool fetchStatusTable(DbTable*);

    // open/create status database table
    void doOpenStatusTable();

    // remove old (2 days or more) transfers from cache, if they were not resumed
    void purgeOrphanTransfers(bool remove = false);

    // close the local transfer cache
    void closetc(bool remove = false);

    // server-client command processing
    void sc_updatenode();
    Node* sc_deltree();
    handle sc_newnodes();
    void sc_contacts();
    void sc_keys();
    void sc_fileattr();
    void sc_userattr();
    bool sc_shares();
    bool sc_upgrade();
    void sc_paymentreminder();
    void sc_opc();
    void sc_ipc();
    void sc_upc(bool incoming);
    void sc_ph();
    void sc_se();
#ifdef ENABLE_CHAT
    void sc_chatupdate(bool readingPublicChat);
    void sc_chatnode();
    void sc_chatflags();
#endif
    void sc_uac();
    void sc_la();
    void sc_ub();
    void sc_sqac();

    void init();

    // remove caches
    void removeCaches();

    // add node to vector and return index
    unsigned addnode(node_vector*, Node*) const;

    // add child for consideration in syncup()/syncdown()
    void addchild(remotenode_map*, string*, Node*, list<string>*, FileSystemType fsType) const;

    // crypto request response
    void cr_response(node_vector*, node_vector*, JSON*);

    // read node tree from JSON object
    void readtree(JSON*);

    // converts UTF-8 to 32-bit word array
    static char* utf8_to_a32forjs(const char*, int*);

    // was the app notified of a retrying CS request?
    bool csretrying;

    // encode/query handle type
    void encodehandletype(handle*, bool);
    bool isprivatehandle(handle*);

    // add direct read
    void queueread(handle, bool, SymmCipher*, int64_t, m_off_t, m_off_t, void*, const char* = NULL, const char* = NULL, const char* = NULL);

    // execute pending direct reads
    bool execdirectreads();

    // maximum number parallel connections for the direct read subsystem
    static const int MAXDRSLOTS = 16;

    // abort queued direct read(s)
    void abortreads(handle, bool, m_off_t, m_off_t);

    static const char PAYMENT_PUBKEY[];

    void dodiscarduser(User* u, bool discardnotified);

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
    unique_ptr<FileSystemAccess> fsaccess;

    // bitmap graphics handling
    GfxProc* gfx;

    // enable / disable the gfx layer
    bool gfxdisabled;

    // DB access
    DbAccess* dbaccess = nullptr;

    // state cache table for logged in user
    unique_ptr<DbTable> sctable;

    // there is data to commit to the database when possible
    bool pendingsccommit;

    // transfer cache table
    unique_ptr<DbTable> tctable;

    // during processing of request responses, transfer table updates can be wrapped up in a single begin/commit
    TransferDbCommitter* mTctableRequestCommitter = nullptr;

    // status cache table for logged in user. For data pertaining status which requires immediate commits
    unique_ptr<DbTable> statusTable;

    // scsn as read from sctable
    handle cachedscsn;

    // initial state load in progress?  initial state can come from the database cache or via an 'f' command to the API.
    // Either way there can still be a lot of historic actionpackets to follow since that snaphot, especially if the user has not been online for a long time.
    bool fetchingnodes;
    int fetchnodestag;

    // have we just completed fetching new nodes?  (ie, caught up on all the historic actionpackets since the fetchnodes)
    bool statecurrent;

    // File Attribute upload system.  These can come from:
    //  - upload transfers
    //  - app requests to attach a thumbnail/preview to a node
    //  - app requests for media upload (which return the fa handle)
    // initially added to queuedfa, and up to 10 moved to activefa.
    putfa_list queuedfa;
    putfa_list activefa;

    // API request queue double buffering:
    // reqs[r] is open for adding commands
    // reqs[r^1] is being processed on the API server
    HttpReq* pendingcs;

    // Only queue the "Server busy" event once, until the current cs completes, otherwise we may DDOS
    // ourselves in cases where many clients get 500s for a while and then recover at the same time
    bool pendingcs_serverBusySent = false;

    // pending HTTP requests
    pendinghttp_map pendinghttp;

    // record type indicator for sctable
    enum { CACHEDSCSN, CACHEDNODE, CACHEDUSER, CACHEDLOCALNODE, CACHEDPCR, CACHEDTRANSFER, CACHEDFILE, CACHEDCHAT, CACHEDSET, CACHEDSETELEMENT } sctablerectype;

    // record type indicator for statusTable
    enum StatusTableRecType { CACHEDSTATUS };

    // open/create state cache database table
    void opensctable();

    // opens (or creates if non existing) a status database table.
    //   if loadFromCache is true, it will load status from the table.
    void openStatusTable(bool loadFromCache);

    // initialize/update state cache referenced sctable
    void initsc();
    void updatesc();
    void finalizesc(bool);

    // truncates status table
    void initStatusTable();

    // flag to pause / resume the processing of action packets
    bool scpaused;

    // MegaClient-Server response JSON
    JSON json;

    // Server-MegaClient request JSON and processing state flag ("processing a element")
    JSON jsonsc;
    bool insca;
    bool insca_notlast;

    // no two interrelated client instances should ever have the same sessionid
    char sessionid[10];

    // session key to protect local storage
    string sessionkey;

    // key protecting non-shareable GPS coordinates in nodes (currently used only by CUv2 in iOS)
    string unshareablekey;

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

    // A record of which file attributes are needed (or now available) per upload transfer
    FileAttributesPending fileAttributesUploading;

    // file attribute fetch channels
    fafc_map fafcs;

    // generate attribute string based on the pending attributes for this upload
    void pendingattrstring(UploadHandle, string*);

    // active/pending direct reads
    handledrn_map hdrns;   // DirectReadNodes, main ownership.  One per file, each with one DirectRead per client request.
    dsdrn_map dsdrns;      // indicates the time at which DRNs should be retried
    dr_list drq;           // DirectReads that are in DirectReadNodes which have fectched URLs
    drs_list drss;         // DirectReadSlot for each DR in drq, up to Max

    // merge newly received share into nodes
    void mergenewshares(bool);
    void mergenewshare(NewShare *s, bool notify);    // merge only the given share

    // transfer queues (PUT/GET)
    transfer_map transfers[2];
    BackoffTimerGroupTracker transferRetryBackoffs[2];
    uint32_t lastKnownCancelCount = 0;

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

    // keep track of next transfer slot timeout
    BackoffTimerGroupTracker transferSlotsBackoff;

    // next TransferSlot to doio() on
    transferslot_list::iterator slotit;

    // FileFingerprint to node mapping
    Fingerprints mFingerprints;

    // flag to skip removing nodes from mFingerprints when all nodes get deleted
    bool mOptimizePurgeNodes = false;

    // send updates to app when the storage size changes
    int64_t mNotifiedSumSize = 0;

    // asymmetric to symmetric key rewriting
    handle_vector nodekeyrewrite;
    handle_vector sharekeyrewrite;

    static const char* const EXPORTEDLINK;

    // default number of seconds to wait after a bandwidth overquota
    static dstime DEFAULT_BW_OVERQUOTA_BACKOFF_SECS;

    // number of seconds to invalidate the cached user data
    static dstime USER_DATA_EXPIRATION_BACKOFF_SECS;

    // total number of Node objects
    long long totalNodes;

    // tracks how many nodes have had a successful applykey()
    long long mAppliedKeyNodeCount = 0;

    // server-client request sequence number
    SCSN scsn;

    bool readusers(JSON*, bool actionpackets);

    user_vector usernotify;
    void notifyuser(User*);

    pcr_vector pcrnotify;
    void notifypcr(PendingContactRequest*);

    node_vector nodenotify;
    void notifynode(Node*);

    // update transfer in the persistent cache
    void transfercacheadd(Transfer*, TransferDbCommitter*);

    // remove a transfer from the persistent cache
    void transfercachedel(Transfer*, TransferDbCommitter* committer);

    // add a file to the persistent cache
    void filecacheadd(File*, TransferDbCommitter& committer);

    // remove a file from the persistent cache
    void filecachedel(File*, TransferDbCommitter* committer);

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

    Node* nodeByHandle(NodeHandle) const;
    Node* nodeByPath(const char* path, Node* node = nullptr);

    Node* nodebyhandle(handle) const;
    Node* nodebyfingerprint(FileFingerprint*);
#ifdef ENABLE_SYNC
    Node* nodebyfingerprint(LocalNode*);
#endif /* ENABLE_SYNC */

    node_vector *nodesbyfingerprint(FileFingerprint* fingerprint);
    void nodesbyoriginalfingerprint(const char* fingerprint, Node* parent, node_vector *nv);

    // get up to "maxcount" nodes, not older than "since", ordered by creation time
    node_vector getRecentNodes(unsigned maxcount, m_time_t since, bool includerubbishbin);

    // get a vector of recent actions in the account
    recentactions_vector getRecentActions(unsigned maxcount, m_time_t since);

    // determine if the file is a video, photo, or media (video or photo).  If the extension (with trailing .) is not precalculated, pass null
    bool nodeIsMedia(const Node*, bool *isphoto, bool *isvideo) const;

    // determine if the file is a photo.
    bool nodeIsPhoto(const Node *n, bool checkPreview) const;

    // determine if the file is a video.
    bool nodeIsVideo(const Node *n) const;

    // determine if the file is an audio.
    bool nodeIsAudio(const Node *n) const;

    // determine if the file is a document.
    bool nodeIsDocument(const Node *n) const;

    // maps node handle to public handle
    std::map<handle, handle> mPublicLinks;

#ifdef ENABLE_SYNC

    // one unified structure for SyncConfigs, the Syncs that are running, and heartbeat data
    Syncs syncs;

    // indicates whether all startup syncs have been fully scanned
    bool syncsup;

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

    // Sync monitor timer.
    //
    // Meaningful only to backup syncs.
    //
    // Set when a backup is mirroring and syncdown(...) returned after
    // having made changes to bring the cloud in line with local disk.
    //
    // That is, the backup remains in the mirror state.
    //
    // The timer is used to force another call to syncdown(...) so that we
    // can give the sync a chance to transition into the monitor state,
    // regardless of whether the local disk has changed.
    bool mSyncMonitorRetry;
    BackoffTimer mSyncMonitorTimer;

    // vanished from a local synced folder
    localnode_set localsyncnotseen;

    // maps local fsid to corresponding LocalNode*
    handlelocalnode_map fsidnode;

    // local nodes that need to be added remotely
    // we need to distinguish those that are allowed to alter vault
    // since the node create command applies the vw flag for all contained nodes
    localnode_vector synccreateForVault;
    localnode_vector synccreateGeneral;

    // number of sync-initiated putnodes() in progress
    int syncadding;

    // total number of LocalNode objects
    long long totalLocalNodes;

    // sync id dispatch
    handle nextsyncid();
    handle currsyncid;

    // SyncDebris folder addition result
    void putnodes_syncdebris_result(error, vector<NewNode>&);

    // if no sync putnodes operation is in progress, apply the updates stored
    // in syncadded/syncdeleted/syncoverwritten to the remote tree
    void syncupdate();
    void syncupdate(localnode_vector&, bool canChangeVault);

    // create missing folders, copy/start uploading missing files
    bool syncup(LocalNode* l, dstime* nds, size_t& parentPending);
    bool syncup(LocalNode* l, dstime* nds);

    // sync putnodes() completion
    void putnodes_sync_result(error, vector<NewNode>&);

    // start downloading/copy missing files, create missing directories
    bool syncdown(LocalNode*, LocalPath&, SyncdownContext& cxt);
    bool syncdown(LocalNode*, LocalPath&);

    // move nodes to //bin/SyncDebris/yyyy-mm-dd/ or unlink directly
    void movetosyncdebris(Node*, bool unlink, bool canChangeVault);

    // move queued nodes to SyncDebris (for syncing into the user's own cloud drive)
    void execmovetosyncdebris();
    unlink_or_debris_set toDebris;

    // unlink queued nodes directly (for inbound share syncing)
    void execsyncunlink();
    unlink_or_debris_set toUnlink;

    // commit all queueud deletions
    void execsyncdeletions();

    // process localnode subtree
    void proclocaltree(LocalNode*, LocalTreeProc*);

    // unlink the LocalNode from the corresponding node
    // if the associated local file or folder still exists
    void unlinkifexists(LocalNode*, FileAccess*);
#endif

    // recursively cancel transfers in a subtree
    void stopxfers(LocalNode*, TransferDbCommitter& committer);

    // update paths of all PUT transfers
    void updateputs();

    // determine if all transfer slots are full
    bool slotavail() const;

    // transfer queue dispatch/retry handling
    void dispatchTransfers();

    void freeq(direction_t);

    // client-server request double-buffering
    RequestDispatcher reqs;

    // returns if the current pendingcs includes a fetch nodes command
    bool isFetchingNodesPendingCS();

    // transfer chunk failed
    void setchunkfailed(string*);
    string badhosts;

    bool requestLock;
    dstime disconnecttimestamp;
    dstime nextDispatchTransfersDs = 0;

    // process object arrays by the API server
    int readnodes(JSON*, int, putsource_t, vector<NewNode>*, int, bool applykeys);

    void readok(JSON*);
    void readokelement(JSON*);
    void readoutshares(JSON*);
    void readoutshareelement(JSON*);

    void readipc(JSON*);
    void readopc(JSON*);

    error readmiscflags(JSON*);

    void procph(JSON*);

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

    const Node* childnodebyname(const Node*, const char*, bool = false) const;
    Node* childnodebyname(Node*, const char*, bool = false);
    Node* childnodebynametype(Node*, const char*, nodetype_t mustBeType);
    Node* childnodebyattribute(Node*, nameid, const char*);
    static void honorPreviousVersionAttrs(Node *previousNode, AttrMap &attrs);
    vector<Node*> childnodesbyname(Node*, const char*, bool = false);
    Node* childNodeTypeByName(Node *p, const char *name, nodetype_t type);

    // purge account state and abort server-client connection
    void purgenodesusersabortsc(bool keepOwnUser);

    static const int USERHANDLE = 8;
    static const int PCRHANDLE = 8;
    static const int NODEHANDLE = 6;
    static const int CHATHANDLE = 8;
    static const int SESSIONHANDLE = 8;
    static const int PURCHASEHANDLE = 8;
    static const int BACKUPHANDLE = 8;
    static const int DRIVEHANDLE = 8;
    static const int CONTACTLINKHANDLE = 6;
    static const int CHATLINKHANDLE = 6;
    static const int SETHANDLE = Set::HANDLESIZE;
    static const int SETELEMENTHANDLE = SetElement::HANDLESIZE;

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
    string mPrivKey;    // serialized version for apps

    // RSA public key
    AsymmCipher pubk;

    // EdDSA signing key (Ed25519 private key seed).
    EdDSA *signkey;

    // ECDH key (x25519 private key).
    ECDH *chatkey;

    // set when keys for every current contact have been checked
    AuthRingsMap mAuthRings;

    // used during initialization to accumulate required updates to authring (to send them all atomically)
    AuthRingsMap mAuthRingsTemp;

    // true while authrings are being fetched
    bool mFetchingAuthrings;

    // actual state of keys
    bool fetchingkeys;

    // invalidate received keys (when fail to load)
    void clearKeys();

    // delete chatkey and signing key
    void resetKeyring();

    // binary session ID
    string sid;

    // distinguish activity from different MegaClients in logs
    string clientname;

    // number our http requests so we can distinguish them (and the curl debug logging for them) in logs
    unsigned transferHttpCounter = 0;

    // apply keys
    void applykeys();

    // send andy key rewrites prepared when keys were applied
    void sendkeyrewrites();

    // symmetric password challenge
    int checktsid(byte* sidbuf, unsigned len);

    // locate user by e-mail address or by handle
    User* finduser(const char*, int = 0);
    User* finduser(handle, int = 0);
    User* ownuser();
    void mapuser(handle, const char*);
    void discarduser(handle, bool = true);
    void discarduser(const char*);
    void mappcr(handle, unique_ptr<PendingContactRequest>&&);
    bool discardnotifieduser(User *);

    PendingContactRequest* findpcr(handle);

    // queue public key request for user
    void queuepubkeyreq(User*, std::unique_ptr<PubKeyAction>);
    void queuepubkeyreq(const char*, std::unique_ptr<PubKeyAction>);

    // rewrite foreign keys of the node (tree)
    void rewriteforeignkeys(Node* n);

    // simple string hash
    static void stringhash(const char*, byte*, SymmCipher*);
    static uint64_t stringhash64(string*, SymmCipher*);

    // builds the authentication URI to be sent in POST requests
    string getAuthURI(bool supressSID = false, bool supressAuthKey = false);

    bool setlang(string *code);

    // create a new folder with given name and stores its node's handle into the user's attribute ^!bak
    error setbackupfolder(const char* foldername, int tag, std::function<void(Error)> addua_completion);

    // sets the auth token to be used when logged into a folder link
    void setFolderLinkAccountAuth(const char *auth);

    // returns the public handle of the folder link if the account is logged into a public folder, otherwise UNDEF.
    handle getFolderLinkPublicHandle();

    // check if end call reason is valid
    bool isValidEndCallReason(int reason);

    // check if there is a valid folder link (rootnode received and the valid key)
    bool isValidFolderLink();

    //returns the top-level node for a node
    Node *getrootnode(Node*);

    //returns true if the node referenced by the handle belongs to the logged-in account
    bool isPrivateNode(NodeHandle h);

    //returns true if the node referenced by the handle belongs to other account than the logged-in account
    bool isForeignNode(NodeHandle h);

    // process node subtree
    void proctree(Node*, TreeProc*, bool skipinshares = false, bool skipversions = false);

    // hash password
    error pw_key(const char*, byte*) const;

    // returns a pointer to tmptransfercipher setting its key to the one provided
    // tmptransfercipher key will change: to be used right away: this is not a dedicated SymmCipher for the transfer!
    SymmCipher *getRecycledTemporaryTransferCipher(const byte *key, int type = 1);

    // returns a pointer to tmpnodecipher setting its key to the one provided
    // tmpnodecipher key will change: to be used right away: this is not a dedicated SymmCipher for the node!
    SymmCipher *getRecycledTemporaryNodeCipher(const string *key);
    SymmCipher *getRecycledTemporaryNodeCipher(const byte *key);

    // request a link to recover account
    void getrecoverylink(const char *email, bool hasMasterkey);

    // query information about recovery link
    void queryrecoverylink(const char *link);

    // request private key for integrity checking the masterkey
    void getprivatekey(const char *code);

    // confirm a recovery link to restore the account
    void confirmrecoverylink(const char *code, const char *email, const char *password, const byte *masterkey = NULL, int accountversion = 1);

    // request a link to cancel the account
    void getcancellink(const char *email, const char* = NULL);

    // confirm a link to cancel the account
    void confirmcancellink(const char *code);

    // get a link to change the email address
    void getemaillink(const char *email, const char *pin = NULL);

    // confirm a link to change the email address
    void confirmemaillink(const char *code, const char *email, const byte *pwkey);

    // create contact link
    void contactlinkcreate(bool renew);

    // query contact link
    void contactlinkquery(handle);

    // delete contact link
    void contactlinkdelete(handle);

    // multi-factor authentication setup
    void multifactorauthsetup(const char* = NULL);

    // multi-factor authentication get
    void multifactorauthcheck(const char*);

    // multi-factor authentication disable
    void multifactorauthdisable(const char*);

    // fetch time zone
    void fetchtimezone();

    void keepmealive(int, bool enable = true);

    void getpsa(bool urlSupport);

    // tells the API the user has seen existing alerts
    void acknowledgeuseralerts();

    // manage overquota errors
    void activateoverquota(dstime timeleft, bool isPaywall);

    // achievements enabled for the account
    bool achievements_enabled;

    // non-zero if login with user+pwd was done (reset upon fetchnodes completion)
    bool isNewSession;

    // timestamp of the last login with user and password
    m_time_t tsLogin;

    // true if user has disabled fileversioning
    bool versions_disabled;

    // the SDK is trying to log out
    int loggingout = 0;

    bool executingLocalLogout = false;

    // the logout request succeeded, time to clean up localy once returned from CS response processing
    std::function<void(MegaClient*)> mOnCSCompletion;

    // true if the account is a master business account, false if it's a sub-user account
    BizMode mBizMode;

    // -1: expired, 0: inactive (no business subscription), 1: active, 2: grace-period
    BizStatus mBizStatus;

    // list of handles of the Master business account/s
    std::set<handle> mBizMasters;

    // timestamp when a business account will enter into Grace Period
    m_time_t mBizGracePeriodTs;

    // timestamp when a business account will finally expire
    m_time_t mBizExpirationTs;

    // whether the destructor has started running yet
    bool destructorRunning = false;

    // Keep track of high level operation counts and times, for performance analysis
    struct PerformanceStats
    {
        CodeCounter::ScopeStats execFunction = { "MegaClient_exec" };
        CodeCounter::ScopeStats transferslotDoio = { "TransferSlot_doio" };
        CodeCounter::ScopeStats execdirectreads = { "execdirectreads" };
        CodeCounter::ScopeStats transferComplete = { "transfer_complete" };
        CodeCounter::ScopeStats megaapiSendPendingTransfers = { "megaapi_sendtransfers" };
        CodeCounter::ScopeStats prepareWait = { "MegaClient_prepareWait" };
        CodeCounter::ScopeStats doWait = { "MegaClient_doWait" };
        CodeCounter::ScopeStats checkEvents = { "MegaClient_checkEvents" };
        CodeCounter::ScopeStats applyKeys = { "MegaClient_applyKeys" };
        CodeCounter::ScopeStats dispatchTransfers = { "dispatchTransfers" };
        CodeCounter::ScopeStats csResponseProcessingTime = { "cs batch response processing" };
        CodeCounter::ScopeStats csSuccessProcessingTime = { "cs batch received processing" };
        CodeCounter::ScopeStats scProcessingTime = { "sc processing" };
        uint64_t transferStarts = 0, transferFinishes = 0;
        uint64_t transferTempErrors = 0, transferFails = 0;
        uint64_t prepwaitImmediate = 0, prepwaitZero = 0, prepwaitHttpio = 0, prepwaitFsaccess = 0, nonzeroWait = 0;
        CodeCounter::DurationSum csRequestWaitTime;
        CodeCounter::DurationSum transfersActiveTime;
        std::string report(bool reset, HttpIO* httpio, Waiter* waiter, const RequestDispatcher& reqs);
    } performanceStats;

    std::string getDeviceidHash();

    /**
     * @brief This function calculates the time (in deciseconds) that a user
     * transfer request must wait for a retry.
     *
     * A pro user who has reached the limit must wait for the renewal or
     * an upgrade on the pro plan.
     *
     * @param req Pointer to HttpReq object
     * @note a 99408 event is sent for non-pro clients with a negative
     * timeleft in the request header
     *
     * @return returns the backoff time in dstime
     */
    dstime overTransferQuotaBackoff(HttpReq* req);

    MegaClient(MegaApp*, Waiter*, HttpIO*, unique_ptr<FileSystemAccess>&&, DbAccess*, GfxProc*, const char*, const char*, unsigned workerThreadCount);
    ~MegaClient();

    void filenameAnomalyDetected(FilenameAnomalyType type, const LocalPath& localPath, const string& remotePath);
    unique_ptr<FilenameAnomalyReporter> mFilenameAnomalyReporter;

struct MyAccountData
{
    void setProLevel(AccountType prolevel) { mProLevel = prolevel; }
    AccountType getProLevel() { return mProLevel; };
    void setProUntil(m_time_t prountil) { mProUntil = prountil; }

    // returns remaining time for the current pro-level plan
    // keep in mind that free plans do not have a remaining time; instead, the IP bandwidth is reset after a back off period
    m_time_t getTimeLeft();

private:
    AccountType mProLevel = AccountType::ACCOUNT_TYPE_UNKNOWN;
    m_time_t mProUntil = -1;
} mMyAccount;

private:
    // Since it's quite expensive to create a SymmCipher, this are provided to use for quick operations - just set the key and use.
    SymmCipher tmpnodecipher;

    // Since it's quite expensive to create a SymmCipher, this is provided to use for quick operation - just set the key and use.
    SymmCipher tmptransfercipher;

    // creates a new id filling `id` with random bytes, up to `length`
    void resetId(char *id, size_t length);


//
// Sets and Elements
//

public:
    // generate "asp" command
    void putSet(Set&& s, std::function<void(Error, const Set*)> completion);

    // generate "asr" command
    void removeSet(handle sid, std::function<void(Error)> completion);

    // generate "aft" command
    void fetchSet(handle sid, std::function<void(Error, Set*, map<handle, SetElement>*)> completion);

    // generate "aep" command
    void putSetElement(SetElement&& el, std::function<void(Error, const SetElement*)> completion);

    // generate "aer" command
    void removeSetElement(handle sid, handle eid, std::function<void(Error)> completion);

    // handle "aesp" parameter, part of 'f'/ "fetch nodes" response
    bool procaesp();

    // load Sets and Elements from json
    error readSetsAndElements(JSON& j, map<handle, Set>& newSets, map<handle, map<handle, SetElement>>& newElements);

    // return Set with given sid or nullptr if it was not found
    const Set* getSet(handle sid) const;

    // return all available Sets, indexed by id
    const map<handle, Set>& getSets() const { return mSets; }

    // add new Set or replace exisiting one
    const Set* addSet(Set&& a);

    // search for Set with the same id, and update its members
    bool updateSet(Set&& s);

    // delete Set with elemId from local memory; return true if found and deleted
    bool deleteSet(handle sid);

    // return Element with given eid from Set sid, or nullptr if not found
    const SetElement* getSetElement(handle sid, handle eid) const;

    // return all available Elements in a Set, indexed by eid
    const map<handle, SetElement>* getSetElements(handle sid) const;

    // add new SetElement or replace exisiting one
    const SetElement* addOrUpdateSetElement(SetElement&& el);

    // delete Element with eid from Set with sid in local memory; return true if found and deleted
    bool deleteSetElement(handle sid, handle eid);

private:

    error readSets(JSON& j, map<handle, Set>& sets);
    error readSet(JSON& j, Set& s);
    error readElements(JSON& j, map<handle, map<handle, SetElement>>& elements);
    error readElement(JSON& j, SetElement& el);
    error decryptSetData(Set& s);
    error decryptElementData(SetElement& el, const string& setKey);
    string decryptKey(const string& k, SymmCipher& cipher) const;
    bool decryptAttrs(const string& attrs, const string& decrKey, string_map& output);
    string encryptAttrs(const string_map& attrs, const string& encryptionKey);

    void sc_asp(); // AP after new or updated Set
    void sc_asr(); // AP after removed Set
    void sc_aep(); // AP after new or updated Set Element
    void sc_aer(); // AP after removed Set Element

    bool initscsets();
    bool fetchscset(string* data, uint32_t id);
    bool updatescsets();
    void notifypurgesets();
    void notifyset(Set*);
    vector<Set*> setnotify;
    map<handle, Set> mSets; // indexed by Set id

    bool initscsetelements();
    bool fetchscsetelement(string* data, uint32_t id);
    bool updatescsetelements();
    void notifypurgesetelements();
    void notifysetelement(SetElement*);
    void clearsetelementnotify(handle sid);
    vector<SetElement*> setelementnotify;
    map<handle, map<handle, SetElement>> mSetElements; // indexed by Set id, then Element id

// -------- end of Sets and Elements

};
} // namespace

#if __cplusplus < 201100L
#define char_is_not_digit std::not1(std::ptr_fun(static_cast<int(*)(int)>(std::isdigit)))
#define char_is_not_space std::not1(std::ptr_fun<int, int>(std::isspace))
#else
#define char_is_not_digit [](char c) { return !std::isdigit(c); }
#define char_is_not_space [](char c) { return !std::isspace(c); }
#endif

#endif
