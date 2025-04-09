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

#include "account.h"
#include "backofftimer.h"
#include "db.h"
#include "drivenotify.h"
#include "filefingerprint.h"
#include "gfx.h"
#include "http.h"
#include "json.h"
#include "mediafileattribute.h"
#include "name_collision.h"
#include "nodemanager.h"
#include "pendingcontactrequest.h"
#include "pubkeyaction.h"
#include "pwm_file_parser.h"
#include "request.h"
#include "setandelement.h"
#include "sharenodekeys.h"
#include "sync.h"
#include "transfer.h"
#include "transferstats.h"
#include "treeproc.h"
#include "user.h"
#include "useralerts.h"

// FUSE support.
#include <mega/fuse/common/client_adapter.h>
#include <mega/fuse/common/service.h>

#include <optional>

namespace mega {

class Logger;
struct NetworkConnectivityTestResults;

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

class ScDbStateRecord : public Cacheable
{
public:
    string seqTag;

    bool serialize(string* data) const override;

    static ScDbStateRecord unserialize(const std::string& data);
};

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

class MegaClient;

class MEGA_API KeyManager
{
public:
    KeyManager(MegaClient& client) : mClient(client) {}

    // it's called to initialize the ^!keys attribute, since it does not exist yet
    // prRSA is expected in base64 and 4 Ints format: pqdu
    void init(const string& prEd25519, const string& prCu25519, const string& prRSA);

    // it derives master key and sets mKey
    void setKey(const SymmCipher& masterKey);

    // decrypts and decodes the ^!keys attribute
    bool fromKeysContainer(const string& data);

    // encodes and encrypts the ^!keys attribute
    string toKeysContainer();

    // --- Getters / Setters ----

    uint32_t generation() const;
    string privEd25519() const;
    string privCu25519() const;

    void setPostRegistration(bool postRegistration);
    bool getPostRegistration() const;

    bool addPendingOutShare(handle sharehandle, std::string uid);
    bool addPendingInShare(std::string sharehandle, handle userHandle, std::string encrytedKey);
    bool removePendingOutShare(handle sharehandle, std::string uid);
    bool removePendingInShare(std::string shareHandle);
    bool addShareKey(handle sharehandle, std::string shareKey, bool sharedSecurely = false);
    string getShareKey(handle sharehandle) const;
    bool isShareKeyTrusted(handle sharehandle) const;
    bool isShareKeyInUse(handle sharehandle) const;
    void setSharekeyInUse(handle sharehandle, bool sent);

    // Clears, if set, the in-use bit of the sharekeys no longer used.
    void syncSharekeyInUseBit();

    // return empty string if the user's credentials are not verified (or if fail to encrypt)
    std::string encryptShareKeyTo(handle userhandle, std::string shareKey);

    // return empty string if the user's credentials are not verified (or if fail to decrypt)
    std::string decryptShareKeyFrom(handle userhandle, std::string shareKey);

    void setAuthRing(std::string authring);
    void setAuthCU255(std::string authring);
    void setPrivRSA(std::string privRSA);
    std::string getPrivRSA();
    bool promotePendingShares();
    bool isUnverifiedOutShare(handle nodeHandle, const string& uid) const;
    bool isUnverifiedInShare(handle nodeHandle, handle userHandle);

    void loadShareKeys();

    void commit(std::function<void()> applyChanges, std::function<void (error e)> completion = nullptr);
    void reset();

    // returns a formatted string, for logging purposes
    string toString() const;

    // Returns true if the warnings related to shares with non-verified contacts are enabled.
    bool getContactVerificationWarning();

    // Enable/disable the warnings for shares with non-verified contacts.
    void setContactVerificationWarning(bool enabled);

    // this method allows to change the manual verification feature-flag for testing purposes
    void setManualVerificationFlag(bool enabled) { mManualVerification = enabled; }

    // query whether manual verification is required.
    bool getManualVerificationFlag() const { return mManualVerification; }

protected:
    std::deque<std::pair<std::function<void()>, std::function<void(error e)>>> nextQueue;
    std::deque<std::pair<std::function<void()>, std::function<void(error e)>>> activeQueue;

    void nextCommit();
    void tryCommit(Error e, std::function<void ()> completion);
    void updateAttribute(std::function<void (Error)> completion);

private:

    // Tags used by TLV blob
    enum {
        TAG_VERSION = 1,
        TAG_CREATION_TIME = 2,
        TAG_IDENTITY = 3,
        TAG_GENERATION = 4,
        TAG_ATTR = 5,
        TAG_PRIV_ED25519 = 16,
        TAG_PRIV_CU25519 = 17,
        TAG_PRIV_RSA = 18,
        TAG_AUTHRING_ED25519 = 32,
        TAG_AUTHRING_CU25519 = 33,
        TAG_SHAREKEYS = 48,
        TAG_PENDING_OUTSHARES = 64,
        TAG_PENDING_INSHARES = 65,
        TAG_BACKUPS = 80,
        TAG_WARNINGS = 96,
    };

    // Bit position for different flags for each sharekey. Bits 2 to 7 reserved for future usage.
    enum ShareKeyFlagsId
    {
        TRUSTED = 0,    // If the sharekey is trusted
        INUSE = 1,      // If there is an active outshare or folder-link using the sharekey
    };

    // Bitmap with flags for each sharekey. The field is 1 byte size in the attribute.
    // See used bits and flag meaning in "ShareKeyFlagsId" enumeration.
    typedef std::bitset<8> ShareKeyFlags;

    static const uint8_t IV_LEN = 12;
    static const std::string SVCRYPTO_PAIRWISE_KEY;

    MegaClient& mClient;

    // key used to encrypt/decrypt the ^!keys attribute (derived from Master Key)
    SymmCipher mKey;

    // true if user needs to manually verify contact's credentials to encrypt/decrypt share keys
    bool mManualVerification = false;

    // enable / disable logs related to the contents of ^!keys
    static const bool mDebugContents = false;

    // true when the account is being created -> don't show warning to user "updading security",
    // false when the account is being upgraded to ^!keys -> show the warning
    bool mPostRegistration = false;

    // if the last known value of generation is greater than a value received in a ^!keys,
    // then a rogue API could be tampering with the attribute
    bool mDowngradeAttack = false;

    uint8_t mVersion = 0;
    uint32_t mCreationTime = 0;
    handle mIdentity = UNDEF;
    uint32_t mGeneration = 0;
    string mAttr;
    string mPrivEd25519, mPrivCu25519, mPrivRSA;
    string mAuthEd25519, mAuthCu25519;
    string mBackups;
    string mOther;

    // maps node handle of the shared folder to a pair of sharekey bytes and sharekey flags.
    map<handle, pair<string, ShareKeyFlags>> mShareKeys;

    // maps node handle to the target users (where value can be a user's handle in B64 or the email address)
    map<handle, set<string>> mPendingOutShares;

    // maps base64 node handles to pairs of source user handle and share key
    map<string, pair<handle, string>> mPendingInShares;

    // warnings as stored as a key-value map
    map<string, string> mWarnings;

    // decode data from the decrypted ^!keys attribute and stores values in `km`
    // returns false in case of unserializatison isues
    static bool unserialize(KeyManager& km, const string& keysContainer);

    // prepares the header for a new serialized record of type 'tag' and 'len' bytes
    string tagHeader(const byte tag, size_t len) const;

    // Serialize pairs of tags and values as Length+Tag+Lengh+Value.
    // warnings and pending inshares are encoded like that when serialized.
    static bool deserializeFromLTLV(const string& blob, map<string, string>& data);
    static string serializeToLTLV(const map<string, string>& data);

    // encode data from the decrypted ^!keys attribute
    string serialize() const;

    string serializeShareKeys() const;
    static bool deserializeShareKeys(KeyManager& km, const string& blob);
    static string shareKeysToString(const KeyManager& km);

    string serializePendingOutshares() const;
    static bool deserializePendingOutshares(KeyManager& km, const string& blob);
    static string pendingOutsharesToString(const KeyManager& km);

    string serializePendingInshares() const;
    static bool deserializePendingInshares(KeyManager& km, const string& blob);
    static string pendingInsharesToString(const KeyManager& km);

    string serializeBackups() const;
    static bool deserializeBackups(KeyManager& km, const string& blob);

    string serializeWarnings() const;
    static bool deserializeWarnings(KeyManager& km, const string& blob);
    static string warningsToString(const KeyManager& km);

    std::string computeSymmetricKey(handle user);

    // validates data in `km`: ie. downgrade attack, tampered keys...
    bool isValidKeysContainer(const KeyManager& km);

    void updateValues(KeyManager& km);

    // decodes the RSA private key and sets it at MegaClient::asymkey
    // returns false if it doesn't match the current key or if failed to set the key
    bool decodeRSAKey();

    // update the corresponding authring with `value`, both in KeyManager and MegaClient::mAuthrings
    void updateAuthring(attr_t at, std::string &value);

    // update sharekeys (incl. trust). It doesn't purge non-existing items
    void updateShareKeys(map<handle, pair<std::string, ShareKeyFlags> > &shareKeys);

    // true if the credentials of this user require verification
    bool verificationRequired(handle userHandle);
};

struct DynamicMessageNotification
{
    int64_t id = 0;
    std::string title;
    std::string description;
    std::string imageName; // main notification image
    std::string iconName;
    std::string imagePath;
    int64_t start = 0;
    int64_t end = 0;
    bool showBanner = false;
    std::map<std::string, std::string> callToAction1;
    std::map<std::string, std::string> callToAction2;
    std::map<std::string, std::map<std::string, std::string>> renderModes;
};

class MEGA_API MegaClient
{
public:
    // own identity
    handle me;
    string uid;

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

    // Consider an account as new if it was created less than X days earlier (right now it's 30days; received in "ug":"na")
    bool accountIsNew = false;

    // AB Test flags
    ThreadSafeKeyValue<string, uint32_t> mABTestFlags;

    ThreadSafeKeyValue<string, uint32_t> mFeatureFlags;

private:
    // Pro Flexi plan is enabled
    bool mProFlexi = false;
public:
    bool isProFlexi() const { return mProFlexi; }

    Error sendABTestActive(const char* flag, CommandABTestActive::Completion completion);

    // 2 = Opt-in and unblock SMS allowed 1 = Only unblock SMS allowed 0 = No SMS allowed  -1 = flag was not received
    SmsVerificationState mSmsVerificationState;

    // the verified account phone number, filled in from 'ug'
    string mSmsVerifiedPhone;

    // pseudo-random number generator
    PrnGen rng;

    bool ephemeralSession = false;
    bool ephemeralSessionPlusPlus = false;

    static TypeOfLink validTypeForPublicURL(nodetype_t type);
    static string publicLinkURL(bool newLinkFormat, TypeOfLink type, handle ph, const char *key);

    string getWritableLinkAuthKey(handle node);

    // method to check if a timestamp (m_time_t) is valid or not
    static bool isValidMegaTimeStamp(m_time_t val) { return val > mega_invalid_timestamp; }

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
    string sendsignuplink2(const char*, const char *, const char*, int ctag = 0);
    void resendsignuplink2(const char*, const char *);

    void confirmsignuplink2(const byte*, unsigned);
    void setkeypair();

    // prelogin: e-mail
    void prelogin(const char* email, CommandPrelogin::Completion completion = nullptr);

    // user login: e-mail, pwkey
    void login(const char*, const byte*, const char* = NULL, CommandLogin::Completion completion = nullptr);

    // user login: e-mail, password, salt
    void login2(const char*, const char*, const string *, const char* = NULL, CommandLogin::Completion completion = nullptr);

    // user login: e-mail, derivedkey, 2FA pin
    void login2(const char*, const byte*, const char* = NULL, CommandLogin::Completion completion = nullptr);

    // user login: e-mail, pwkey, emailhash
    void fastlogin(const char*, const byte*, uint64_t, CommandLogin::Completion completion = nullptr);

    // session login: binary session, bytecount
    void login(string session, CommandLogin::Completion completion = nullptr);

    // handle login result, and allow further actions when successful
    void loginResult(CommandLogin::Completion completion,
                     error e,
                     std::function<void()> onLoginOk = nullptr);

    // check password
    error validatepwd(const char* pswd);
    bool validatepwdlocally(const char* pswd);

    // get user data
    void getuserdata(int tag, std::function<void(string*, string*, string*, error)> = nullptr);

    // get miscelaneous flags
    void getmiscflags();

    // get the public key of an user
    void getpubkey(const char* user);

    // check if logged in (avoid repetitive calls <-- requires call to Cryptopp::InverseMod(), which is slow)
    sessiontype_t loggedin();

    // provide state by change callback
    void reportLoggedInChanges();
    sessiontype_t mLastLoggedInReportedState = NOTLOGGEDIN;
    handle mLastLoggedInMeHandle = UNDEF;
    string mLastLoggedInMyEmail;

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
    error parsepubliclink(const char* link, handle& ph, byte* extractedKey, TypeOfLink type);

    // open the SC database and get the SCSN from it
    void checkForResumeableSCDatabase();

    // set folder link: node, key. authKey is the authentication key to be able to write into the folder
    error folderaccess(const char* folderlink,
                       const char* authKey,
                       const bool tryToResumeFolderLinkFromCache = false);

    // open exported file link (op=0 -> download, op=1 fetch data)
    void openfilelink(handle ph, const byte* fileKey);

    // decrypt password-protected public link
    // the caller takes the ownership of the returned value in decryptedLink parameter
    error decryptlink(const char* link, const char* pwd, string *decryptedLink);

    // encrypt public link with password
    // the caller takes the ownership of the returned value
    error encryptlink(const char* link, const char* pwd, string *encryptedLink);

    // change login password
    error changepw(const char *password, const char *pin = NULL);

    // invoked at the moment we actually send `f`
    void resetScForFetchnodes();

    // load all trees: nodes, shares, contacts
    void fetchnodes(bool nocache, bool loadSyncs, bool reloadingMidSession);

    // fetchnodes stats
    FetchNodesStats fnstats;

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

    // update the authring if needed on the server and manage the deactivation of the temporal authring
    error updateAuthring(AuthRing *authring, attr_t authringType, bool temporalAuthring, handle updateduh);

    // set the Ed25519 public key as verified for a given user in the authring (done by user manually by comparing hash of keys)
    error verifyCredentials(handle uh, std::function<void(Error)>);

    // reset the authentication method of Ed25519 key from Fingerprint-verified to Seen for a given user
    error resetCredentials(handle uh, std::function<void (Error)>);

    // check credentials are verified for a given user
    bool areCredentialsVerified(handle uh);

    // retrieve user details
    void getaccountdetails(std::shared_ptr<AccountDetails>, bool, bool, bool, bool, bool, bool, int source = -1);

    // Get user storage information.
    void getstorageinfo(std::function<void(const StorageInfo&, Error)> completion);

    // check if the available bandwidth quota is enough to transfer an amount of bytes
    void querytransferquota(m_off_t size);

    static constexpr char NODE_ATTRIBUTE_DESCRIPTION[] = "des";
    static constexpr char NODE_ATTRIBUTE_TAGS[] = "t";
    static constexpr char NODE_ATTR_SEN[] = "sen";
    static constexpr char NODE_ATTR_LABEL[] = "lbl";
    static constexpr char TAG_DELIMITER = NodeSearchFilter::TAG_DELIMITER;
    static constexpr uint32_t MAX_NUMBER_TAGS = 10;
    static constexpr uint32_t MAX_TAGS_SIZE = 3000;

    // update node attributes
    error setattr(std::shared_ptr<Node>, attr_map&& updates, CommandSetAttr::Completion&& c, bool canChangeVault);

    // prefix and encrypt attribute json
    static void makeattr(SymmCipher*, string*, const char*, int = -1);

    // convenience version of the above (frequently we are passing a NodeBase's attrstring)
    static void makeattr(SymmCipher*, const std::unique_ptr<string>&, const char*, int = -1);

    error addTagToNode(std::shared_ptr<Node> node, const std::string& tag, CommandSetAttr::Completion&& c);
    static std::vector<std::string> getNodeTags(const std::string& delimitedTags);
    std::vector<std::string> getNodeTags(std::shared_ptr<Node> node);

    /*
     * @brief
     * Get all node tags below a specified node.
     *
     * @param cancelToken
     * A token that can be used to terminate the query's execution prematurely.
     *
     * @param handle
     * A handle specifying which node we want to list tags below.
     *
     * If undefined, the query will list tags below all root nodes.
     *
     * @param pattern
     * An optional pattern that can be used to filter which tags we list.
     *
     * @returns
     * std::nullopt on failure.
     * std::set<std::string> on success.
     */
    auto getNodeTagsBelow(CancelToken cancelToken,
                          NodeHandle handle,
                          const std::string& pattern = {}) -> std::optional<std::set<std::string>>;

    auto getNodeTagsBelow(NodeHandle handle, const std::string& pattern = {})
        -> std::optional<std::set<std::string>>;

    error removeTagFromNode(std::shared_ptr<Node> node, const std::string& tag, CommandSetAttr::Completion&& c);
    error updateTagNode(std::shared_ptr<Node>, const std::string& newTag, const std::string& oldTag, CommandSetAttr::Completion&& c);

    // Returns true if welcome pdf should be imported
    // It's depend on client type (true for ClientType::DEFAULT)
    bool shouldWelcomePdfImported() const
    {
        return getClientType() != MegaClient::ClientType::VPN &&
               getClientType() != MegaClient::ClientType::PASSWORD_MANAGER;
    }

public:
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
    error rename(std::shared_ptr<Node>, std::shared_ptr<Node>, syncdel_t, NodeHandle prevparenthandle, const char *newName, bool canChangeVault, CommandMoveNode::Completion&& c);

    // create folder node
    error createFolder(std::shared_ptr<Node> parent, const char* name, int rTag);

    // rename a node (i.e. change the node attribute 'n')
    error renameNode(NodeHandle nh, const char* newName, CommandSetAttr::Completion&& cbRequest);

    // remove node
    error removeNode(NodeHandle nh, bool keepVersions, int rTag);

    // Queue commands (if needed) to remvoe any outshares (or pending outshares) below the specified node
    void removeOutSharesFromSubtree(std::shared_ptr<Node> n, int tag);

    // start/stop/pause file transfer
    bool startxfer(direction_t, File*, TransferDbCommitter&, bool skipdupes, bool startfirst, bool donotpersist, VersioningOption, error* cause, int tag, m_off_t availableDiskSpace = 0);
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
    void pread(Node*, m_off_t, m_off_t, DirectRead::Callback&& callback);
    void pread(handle,
               SymmCipher* cypher,
               int64_t,
               m_off_t,
               m_off_t,
               DirectRead::Callback&& callback,
               bool = false,
               const char* = NULL,
               const char* = NULL,
               const char* = NULL);
    void pread(Node*, m_off_t, m_off_t, void*);
    void pread(handle,
               SymmCipher* cypher,
               int64_t,
               m_off_t,
               m_off_t,
               void*,
               bool = false,
               const char* = NULL,
               const char* = NULL,
               const char* = NULL);
    void preadabort(Node*, m_off_t = -1, m_off_t = -1);
    void preadabort(handle, m_off_t = -1, m_off_t = -1);

    // pause flags
    bool xferpaused[2];

    MegaClientAsyncQueue mAsyncQueue;

    // number of parallel connections per transfer (PUT/GET)
    unsigned char connections[2];

    // helpfer function for preparing a putnodes call for new node
    error putnodes_prepareOneFile(NewNode* newnode, Node* parentNode, const char *utf8Name, const UploadToken& binaryUploadToken,
                                  const byte *theFileKey, const char *megafingerprint, const char *fingerprintOriginal,
                                  std::function<error(AttrMap&)> addNodeAttrsFunc = nullptr,
                                  std::function<error(std::string *)> addFileAttrsFunc = nullptr);

    // helper function for preparing a putnodes call for new folders
    void putnodes_prepareOneFolder(NewNode* newnode, std::string foldername, bool canChangeVault, std::function<void (AttrMap&)> addAttrs = nullptr);

    // static version to be used from worker threads, which cannot rely on the MegaClient::tmpnodecipher as SymCipher (not thread-safe))
    static void putnodes_prepareOneFolder(NewNode* newnode, std::string foldername, PrnGen& rng, SymmCipher &tmpnodecipher, bool canChangeVault, std::function<void(AttrMap&)> addAttrs = nullptr);

    // add nodes to specified parent node (complete upload, copy files, make
    // folders)
    void putnodes(NodeHandle,
                  VersioningOption vo,
                  vector<NewNode>&&,
                  const char*,
                  int tag,
                  bool canChangeVault,
                  std::string customerIpPort = {},
                  CommandPutNodes::Completion&& completion = nullptr);

    // send files/folders to user
    void putnodes(const char*, vector<NewNode>&&, int tag, CommandPutNodes::Completion&& completion = nullptr);

    void putFileAttributes(handle h, fatype t, const std::string& encryptedAttributes, int tag);

    // attach file attribute to upload or node handle
    error putfa(NodeOrUploadHandle, fatype, SymmCipher*, int tag, std::unique_ptr<string>);

    // move as many as possible from pendingfa to activefa
    void activatefa();

    // queue file attribute retrieval
    error getfa(handle h, string *fileattrstring, const string &nodekey, fatype, int = 0);

    // notify delayed upload completion subsystem about new file attribute
    void checkfacompletion(UploadHandle, Transfer* = NULL, bool uploadCompleted = false);

    // attach/update/delete a user attribute using encryption
    void putua(attr_t at,
               string_map&& records,
               int ctag = -1,
               handle lastPublicHandle = UNDEF,
               int phtype = 0,
               int64_t ts = 0,
               std::function<void(Error)> completion = nullptr);

    // attach/update/delete a user attribute
    void putua(attr_t at, const byte* av = NULL, unsigned avl = 0, int ctag = -1, handle lastPublicHandle = UNDEF, int phtype = 0, int64_t ts = 0,
        std::function<void(Error)> completion = nullptr);

    // attach/update multiple versioned user attributes at once
    void putua(userattr_map *attrs, int ctag = -1, std::function<void(Error)> completion = nullptr);

    // queue a user attribute retrieval
    bool getua(User* u, const attr_t at = ATTR_UNKNOWN, int ctag = -1, CommandGetUA::CompletionErr completionErr = nullptr, CommandGetUA::CompletionBytes completionBytes = nullptr, CommandGetUA::CompletionTLV completionTLV = nullptr);

    // queue a user attribute retrieval (for non-contacts)
    void getua(const char* email_handle, const attr_t at = ATTR_UNKNOWN, const char *ph = NULL, int ctag = -1, CommandGetUA::CompletionErr ce = nullptr, CommandGetUA::CompletionBytes cb = nullptr, CommandGetUA::CompletionTLV ctlv = nullptr);

    // retrieve the email address of a user
    void getUserEmail(const char* userID);

    // Set email for an user
    void setEmail(User* u, const std::string& email);

//
// Account upgrade to V2
//
public:
    void saveV1Pwd(const char* pwd);
private:
    void upgradeAccountToV2(const string& pwd, int ctag, std::function<void(error e)> completion);
    // temporarily stores v1 account password, to allow automatic upgrade to v2 after successful (full-)login
    unique_ptr<pair<string, SymmCipher>> mV1PswdVault;
// -------- end of Account upgrade to V2

public:
#ifdef DEBUG
    // queue a user attribute removal
    void delua(const char* an);

#endif
    // send dev command for testing
    void senddevcommand(const char* command,
                        const char* email,
                        long long q = 0,
                        int bs = 0,
                        int us = 0,
                        const char* abs_c = nullptr);

    // delete or block an existing contact
    error removecontact(const char*, visibility_t = HIDDEN, CommandRemoveContact::Completion completion = nullptr);

    // Migrate the account to start using the new ^!keys attr.
    void upgradeSecurity(std::function<void(Error)> completion);

    // Set the flag to enable/disable warnings when sharing with a non-verified contact.
    void setContactVerificationWarning(bool enabled, std::function<void(Error)> completion = nullptr);

    // Creates a new share key for the node if there is no share key already created.
    void openShareDialog(Node* n, std::function<void (Error)> completion);

    // add/remove/update outgoing share
    void setshare(std::shared_ptr<Node>, const char*, accesslevel_t, bool writable, const char*,
        int tag, std::function<void(Error, bool writable)> completion);

    void setShareCompletion(Node*, User*, accesslevel_t, bool writable, const char*,
        int tag, std::function<void(Error, bool writable)> completion);

    // Add/delete/remind outgoing pending contact request
    void setpcr(const char*, opcactions_t, const char* = NULL, const char* = NULL, handle = UNDEF, CommandSetPendingContact::Completion completion = nullptr);
    void updatepcr(handle, ipcactions_t, CommandUpdatePendingContact::Completion completion = nullptr);

    // export node link or remove existing exported link for this node
    error exportnode(std::shared_ptr<Node>,
                     int,
                     m_time_t,
                     bool writable,
                     bool megaHosted,
                     int tag,
                     std::function<void(Error, handle, handle, std::string&&)> completion);
    void requestPublicLink(Node* n,
                           int del,
                           m_time_t ets,
                           bool writable,
                           bool megaHosted,
                           int tag,
                           CommandSetPH::CompletionType completion); // auxiliar method to add req

    // add timer
    error addtimer(TimerWithBackoff *twb);

#ifdef ENABLE_SYNC
    /**
     * @brief Check if a given node is syncable
     * @param remotenode We want to validate if a sync can be enabled using this node as remote
     * root.
     * @param excludeSelf if true, we will asume the given node is already the root of a sync and we
     * want to validate if it can still be synced. In other words, while iterating active syncs we
     * will scape the one having remotenode as root.
     * @return A std::pair with information about the API and Sync errors. API_OK, NO_SYNC_ERROR if
     * the node is syncable.
     */
    std::pair<error, SyncError> isnodesyncable(std::shared_ptr<Node> remotenode,
                                               const bool excludeSelf = false);

    /**
     * @brief Checks if the given remotenode is related with any active sync
     *
     * @param remotenode The node to check
     * @param excludeSelf if true, we will asume the given node is already the root of an active
     * sync. This sync will be skipped during the evaluation.
     * @return NO_SYNC_ERROR if the given node is not related with any of the active syncs.
     * Otherwise:
     *   - ACTIVE_SYNC_SAME_PATH: If the given node is already the root of an active sync
     *   - ACTIVE_SYNC_BELOW_PATH: If the given node contains an active sync
     *   - ACTIVE_SYNC_ABOVE_PATH: If the given node is contained in an active sync
     */
    SyncError anyActiveSyncAboveOrBelow(const Node& remotenode, const bool excludeSelf);

    /**
     * @brief Checks whether the current user has full access to the given node.
     *
     * This will be always the case for nodes that are not part of an inbound shared node. If the
     * node is or is contained in an inbound shared node, this function checks that the user has
     * full access to the node being shared.
     *
     * @param remotenode The node to check
     * @return true if the current user has full access to the node, false otherwise
     */
    bool userHasRestrictedAccessToNode(const Node& remotenode);

    /**
     * @brief Checks if a local path is suitable for synchronization without conflicting with
     * existing syncs.
     *
     * This method determines whether the provided `newPath` can be used for a new synchronization
     * task. It ensures that the path does not overlap with any existing active sync configurations,
     * thereby preventing conflicts arising from nested or overlapping sync directories.
     *
     * @param newPath The local path to be checked for sync compatibility.
     * @param excludeBackupId A backup ID to exclude from the check. This is useful when
     * updating an existing sync, allowing it to bypass itself during the validation. Pass `UNDEF`
     * if you don't want to skip any sync during the validation.
     *
     * @return A `std::pair` containing:
     * - `error`: An error code indicating the result of the check.
     * - `SyncError`: A detailed sync error code providing more context.
     *
     * The possible returned error combinations are:
     *   - `API_OK`:
     *      + `NO_SYNC_ERROR` if the path is syncable and does not conflict with existing syncs.
     *   - `API_EARGS`:
     *      + `LOCAL_PATH_UNAVAILABLE` if the provided `newPath` is empty.
     *      + `LOCAL_PATH_SYNC_COLLISION` if the path conflicts with an existing sync path.
     *
     * @warning
     * - The function does not verify the existence or accessibility of the `newPath`; it only
     * checks for path conflicts.
     * - Symbolic links are not resolved; the check is purely lexical based on the expanded paths.
     */
    std::pair<error, SyncError> isLocalPathSyncable(const LocalPath& newPath,
                                                    const handle excludeBackupId) const;

    /**
     * @brief Validates if the local path in a SyncConfig is suitable as a synchronization root.
     *
     * It performs several validations, including:
     * - Ensuring the path is not above or below a FUSE mount point.
     * - Verifying that the filesystem supports synchronization.
     * - Confirming the path exists, is accessible, and is a directory.
     * - Checking for conflicts with existing sync configurations to prevent overlapping sync areas.
     *
     * @param rootPath The path to validate
     * @param excludeBackupId A backup ID to exclude from the check done by the
     * `isLocalPathSyncable` method. This is useful when updating an existing sync, allowing it to
     * bypass itself during the validation. Pass `UNDEF` if you don't want to skip any sync during
     * the validation.
     *
     * @return A SyncErrorInfo, i.e., a `std::tuple` containing:
     * - `error`: An error code indicating the result of the check.
     * - `SyncError`: A detailed sync error code providing more context.
     * - `SyncWarning`: A sync warning code returned by `FileSystemAccess::issyncsupported`.
     *
     * The possible returned error combinations are:
     *   - `API_OK`:
     *      + `NO_SYNC_ERROR` if the path is a valid root for a sync.
     *   - `API_EARGS`:
     *      + `NO_SYNC_ERROR` if the given path is not absolute.
     *      + `LOCAL_PATH_UNAVAILABLE` if the provided `newPath` is empty.
     *      + `LOCAL_PATH_SYNC_COLLISION` if the path conflicts with an existing sync path.
     * - `API_EFAILED`:
     *      + `LOCAL_PATH_MOUNTED` if the path is above or below a FUSE mount point
     *      + `UNSUPPORTED_FILE_SYSTEM` if the filesystem is unsupported.
     * - `API_ETEMPUNAVAIL`:
     *      + `LOCAL_PATH_TEMPORARY_UNAVAILABLE` if the local path is temporarily unavailable.
     * - `API_ENOENT`:
     *      + `LOCAL_PATH_UNAVAILABLE` if the local path cannot be accessed.
     * - `API_EACCESS`:
     *      + `INVALID_LOCAL_TYPE` if the path is not a directory.
     */
    SyncErrorInfo isValidLocalSyncRoot(const LocalPath& rootPath,
                                       const handle backupIdToExclude) const;

    /**
     * @brief Check if a SyncConfig could be used to create a sync
     *
     * This function performs a series of checks to determine whether the provided `SyncConfig` is
     * valid and can be used to initiate synchronization.
     *
     * The validation includes:
     * - Ensuring the remote node exists and is suitable for syncing. This includes checks for
     *   nested-syncs, which are not allowed.
     * - Verifying the local path is accessible, is a directory, and is on a supported filesystem.
     *   Also nested syncs checks are carried out.
     * - Checking for account-related issues such as over-storage, expiration, or blockage.
     * - Handling specific conditions for backups, external drives, and periodic scanning modes.
     *
     * @param syncConfig The `SyncConfig` to validate. May be modified with error or warning
     * details.
     *
     * @return A SyncErrorInfo, i.e., a `std::tuple` containing:
     * - `error`: An error code indicating the result of the check.
     * - `SyncError`: A detailed sync error code providing more context.
     * - `SyncWarning`: A sync warning code returned by `FileSystemAccess::issyncsupported`.
     *
     * Possible error codes include:
     * - `API_ENOENT`: Remote node does not exist.
     * - `API_EARGS`: Invalid arguments, such as an invalid scan interval, mismatched drive paths or
     *   given directory involved in other syncs.
     * - `API_EEXIST`: If there is an active sync already initiated with the given node, containing
     *   the node or the given node contains a another sync's root.
     * - `API_EFAILED`: General failure due to account status or unsupported filesystem.
     * - `API_EACCESS`: Local path is not a directory or cannot be accessed.
     * - `API_ETEMPUNAVAIL`: Local path is temporarily unavailable.
     * - `API_EINTERNAL`: Internal error requiring account reload or restart.
     *
     * @note
     * - Symbolic links and mount points are considered during validation to prevent syncing
     * unsupported paths.
     */
    SyncErrorInfo checkSyncConfig(const SyncConfig& syncConfig);

    /**
     * @brief add sync. Will fill syncError/syncWarning in the SyncConfig in case there are any.
     * It will persist the sync configuration if its call to checkSyncConfig succeeds
     * @param syncConfig the Config to attempt to add (takes ownership)
     * @param completion Completion function
     * @exludedPath: in sync rework, use this to specify a folder within the sync to exclude (eg, working folder with sync db in it)
     * @return API_OK if added to active syncs. (regular) error otherwise (with detail in syncConfig's SyncError field).
     * Completion is used to signal success/failure.  That may occur during this call, or in future (after server request/reply etc)
     */
    void addsync(SyncConfig&& syncConfig, std::function<void(error, SyncError, handle)> completion, const string& logname, const string& excludedPath);

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

    /**
     * @brief Used to migrate all the sync's management data from MEGAsync to the SDK
     *
     * @deprecated This function is deprecated. Please don't use it in new code.
     */
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
     * @brief Changes the local/remote root of a sync to a different path/node
     *
     * @note This function only allows changing the local or the remote roots at once. To decide
     * which one would be changes, one of newRemoteRootNodeHandle or newLocalRootPath must be
     * UNDEF or nullptr respectively. Otherwise an API_EARGS will be returned
     *
     * @param backupId The backup id of the sync to be modified
     * @param newRemoteRootNodeHandle The handle of the node that will be used for the new root on
     * the cloud
     * @param newLocalRootPath The path to the new root of the sync locally
     * @param completion A completion function to be called once the logic is done, with the
     * corresponding error.
     */
    void changeSyncRoot(const handle backupId,
                        const handle newRemoteRootNodeHandle,
                        const char* const newLocalRootPath,
                        std::function<void(error, SyncError)>&& completion);

private:
    static constexpr std::chrono::seconds TIMEOUT_TO_SET_SYNC_UPLOAD_THROTTLE_PARAMS_FROM_API{
        86400};

    std::chrono::steady_clock::time_point mSetSyncUploadThrottleParamsFromAPILastTime;

    std::chrono::seconds timeSinceLastSetSyncUploadThrottleParamsFromAPI() const
    {
        return std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - mSetSyncUploadThrottleParamsFromAPILastTime);
    }

    void setSyncUploadThrottleParamsFromAPIAfterTimeout();

    void handleSetThrottleResult(const CommandSetThrottlingParams::ResultVariant& result);

public:
    /**
     * @brief Sets the upload throttling configurable values retrieved directly from API.
     *
     * @see Syncs::setThrottleUpdateRate()
     * @see Syncs::setMaxUploadBeforeThrottle()
     */
    void setSyncUploadThrottleParamsFromAPI();

    /**
     * @brief Sets the upload throttling configurable value throttleUpdateRate.
     *
     * @see Syncs::setThrottleUpdateRate()
     */
    void setSyncUploadThrottleUpdateRate(std::chrono::seconds throttleUpdateRate,
                                         std::function<void(const error)>&& completion);

    /**
     * @brief Sets the upload throttling configurable value maxUploadsBeforeThrottle.
     *
     * @see Syncs::setMaxUploadsBeforeThrottle()
     */
    void setSyncMaxUploadsBeforeThrottle(const unsigned maxUploadsBeforeThrottle,
                                         std::function<void(const error)>&& completion);

    /**
     * @brief Retrieves the upload throttling configurable values.
     */
    void syncUploadThrottleValues(
        std::function<void(const std::chrono::seconds /* updateRateInSeconds */,
                           const unsigned /* maxUploadsBeforeThrottle */)>&& completion);

    /**
     * @brief Retrieves the lower and upper upload throttling configurable values.
     */
    void syncUploadThrottleValuesLimits(std::function<void(ThrottleValueLimits&&)>&& completion);

    /**
     * @brief Checks whether there are delayed uploads pending to be processed.
     */
    void checkSyncUploadsThrottled(std::function<void(const bool)>&& completion);

    /**
     * @brief Sets the IUploadThrottlingManager for Syncs.
     *
     * The Syncs object already constructs a IUploadThrottlingManager type object by default.
     * However, this method allows to change the object if needed.
     *
     * @param uploadThrottlingManager The shared_ptr to the IUploadThrottlingManager type object.
     * @param completion The completion function to be called after the operations
     * finishes.
     */
    void setSyncUploadThrottlingManager(
        std::shared_ptr<IUploadThrottlingManager> uploadThrottlingManager,
        std::function<void(const error)>&& completion);

public:
#else
    void setSyncUploadThrottleParamsFromAPI() {}
#endif // ENABLE_SYNC

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
    void sendchatlogs(const char*, mega::handle forUserID, mega::handle callid, int port);

    // send a HTTP request
    void httprequest(const char*, int, bool = false, const char* = NULL, int = 1);

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
    void creditcardcancelsubscriptions(
        const CommandCreditCardCancelSubscriptions::CancelSubscription& cancelSubscription);

    // get payment methods
    void getpaymentmethods();

    // store user feedback
    void userfeedbackstore(const char *);

    // send event
    void sendevent(int, const char *, const char* viewId = nullptr, bool addJourneyId = false);
    void sendevent(int, const char *, int tag, const char* viewId = nullptr, bool addJourneyId = false);

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
    void createChat(bool group, bool publicchat, const userpriv_vector* userpriv = NULL, const string_map* userkeymap = NULL, const char* title = NULL, bool meetingRoom = false, int chatOptions = ChatOptions::kEmpty, const ScheduledMeeting* schedMeeting = nullptr);

    // invite a user to a chat
    void inviteToChat(handle chatid, handle uh, int priv, const char *unifiedkey = NULL, const char *title = NULL);

    // remove a user from a chat
    void removeFromChat(handle chatid, handle uh);

    // get the URL of a chat
    void getUrlChat(handle chatid);

    // set chat mode (public/private)
    void setChatMode(TextChat* chat, bool pubChat);

    // process object arrays by the API server (users + privileges)
    userpriv_vector * readuserpriv(JSON* j);

    // grant access to a chat peer to one specific node
    void grantAccessInChat(handle chatid, handle h, const char* peer);

    // revoke access to a chat peer to one specific node
    void removeAccessInChat(handle chatid, handle h, const char* peer);

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

    // parse scheduled meeting or scheduled meeting occurrences
    error parseScheduledMeetings(std::vector<std::unique_ptr<ScheduledMeeting> > &schedMeetings,
                                 bool parsingOccurrences, JSON *j, bool parseOnce = false,
                                 handle* originatingUser = nullptr,
                                 UserAlert::UpdatedScheduledMeeting::Changeset* cs = nullptr,
                                 handle_set* childMeetingsDeleted = nullptr);

    // report invalid scheduled meeting by sending an event to stats server
    void reportInvalidSchedMeeting(const ScheduledMeeting* sched = nullptr);
#endif

    // get mega achievements
    void getaccountachievements(AchievementsDetails *details);

    // get mega achievements list (for advertising for unregistered users)
    void getmegaachievements(AchievementsDetails *details);

    // get welcome pdf
    void importOrDelayWelcomePdf();
    bool wasWelcomePdfImportDelayed();
    void importWelcomePdfIfDelayed();
    void setWelcomePdfNeedsDelayedImport(bool requestImport);

private:
    void getwelcomepdf();

public:
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
    std::shared_ptr<Node> getovnode(Node *parent, string *name);

    // Load from db node children at first level
    sharedNode_list getChildren(const Node *parent, CancelToken cancelToken = CancelToken());

    // Get number of children from a node
    size_t getNumberOfChildren(NodeHandle parentHandle);

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

private:
    // The current request's status in millis.
    //
    // This member is maintained by procreqstat(...) whether request
    // monitoring is enabled or not, the idea being that we want an
    // application to be able to show a progress bar once any request
    // starts, without any delay.
    //
    // When the SDK starts a request, it checks to see if this member is
    // valid. If it is, it immediately notifies the application.
    //
    // If this member is not valid when a request is started, the SDK delays
    // notifying the application until progress information is received.
    std::optional<int> mRequestProgress;

    // Tracks whether we've notified the application of request progress.
    bool mRequestProgressNotified = false;

    // flag to start / stop the request status monitor
    bool mReqStatEnabled = false;
public:
    bool requestStatusMonitorEnabled() { return mReqStatEnabled; }
    void startRequestStatusMonitor() { mReqStatEnabled = true; }
    void stopRequestStatusMonitor() { mReqStatEnabled = false; }

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

    // root URL for reqstat requests
    static const string REQSTATURL;

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
    BackoffTimer btreqstat;

    vector<TimerWithBackoff *> bttimers;

    // server-client command trigger connection
    std::unique_ptr<HttpReq> pendingsc;
    std::unique_ptr<HttpReq> pendingscUserAlerts;
    BackoffTimer btsc;

    int mPendingCatchUps = 0;
    bool mReceivingCatchUp = false;

    // account is blocked: stops querying for action packets, pauses transfer & removes transfer slot availability
    bool mBlocked = false;
    bool mBlockedSet = false; //value set in current execution

    bool pendingscTimedOut = false;

    // badhost report
    HttpReq* badhostcs;

    // Working lock
    unique_ptr<HttpReq> workinglockcs;

    // Request status monitor
    unique_ptr<HttpReq> mReqStatCS;

    // List of Notification IDs that should show in Notification Center
    std::vector<uint32_t> mEnabledNotifications;

public:
    // notify URL for new server-client commands
    string scnotifyurl;

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

    // minimum maximum number of concurrent transfers for dynamic calculation
    static const unsigned MIN_MAXTRANSFERS;

    // maximum number of concurrent raided transfers for mobile
    static const unsigned MAX_RAIDTRANSFERS_FOR_MOBILE;

    // meaningful portion of the maximum transfer queue size to consider raid representation
    // i.e., there must be at least this number of raid transfers to let us predict whether the next download transfer will be raided or non-raided
    static const unsigned MEANINGFUL_PORTION_OF_MAXTRANSFERS_QUEUE_FOR_RAID_PREDICTIVE_SYSTEM;

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
    bool sc_checkSequenceTag(const string& tag);
    bool sc_checkActionPacket(Node* lastAPDeletedNode);

    void sc_updatenode();
    std::shared_ptr<Node> sc_deltree();
    handle sc_newnodes(Node* priorActionpacketDeletedNode, bool& firstHandleMismatchedDelete);
    void sc_contacts();
    void sc_keys();
    void sc_fileattr();
    void sc_userattr();
    bool sc_shares();
    bool sc_upgrade(nameid paymentType);
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
    void sc_scheduledmeetings();
    void sc_delscheduledmeeting();
    void createNewSMAlert(const handle&, handle chatid, handle schedId, handle parentSchedId, m_time_t startDateTime);
    void createDeletedSMAlert(const handle&, handle chatid, handle schedId);
    void createUpdatedSMAlert(const handle&, handle chatid, handle schedId, handle parentSchedId,
                               m_time_t startDateTime, UserAlert::UpdatedScheduledMeeting::Changeset&& cs);
    static error parseScheduledMeetingChangeset(JSON*, UserAlert::UpdatedScheduledMeeting::Changeset*);
    void clearSchedOccurrences(TextChat& chat);
#endif
    void sc_uac();
    void sc_uec();
    void sc_la();
    void sc_ub();
    void sc_sqac();
    void sc_pk();
    void sc_cce();

    void init();

    // remove caches
    void removeCaches();

    // add node to vector and return index
    unsigned addnode(sharedNode_vector* v, std::shared_ptr<Node> n) const;

    // crypto request response
    void cr_response(sharedNode_vector*, sharedNode_vector*, JSON*);

    // read node tree from JSON object
    void readtree(JSON*, Node* priorActionpacketDeletedNode, bool& firstHandleMatchedDelete);

    // converts UTF-8 to 32-bit word array
    static char* utf8_to_a32forjs(const char*, int*);

    // was the app notified of a retrying CS request?
    bool csretrying;

    // encode/query handle type
    void encodehandletype(handle*, bool);
    bool isprivatehandle(handle*);

    // add direct read
    void queueread(handle,
                   bool,
                   SymmCipher*,
                   int64_t,
                   m_off_t,
                   m_off_t,
                   DirectRead::Callback&& callback,
                   const char* = NULL,
                   const char* = NULL,
                   const char* = NULL);

    void queueread(handle, bool, SymmCipher*, int64_t, m_off_t, m_off_t, void*, const char* = NULL, const char* = NULL, const char* = NULL);

    // execute pending direct reads
    bool execdirectreads();

    // maximum number parallel connections for the direct read subsystem
    static const int MAXDRSLOTS = 16;

    // abort queued direct read(s)
    void abortreads(handle, bool, m_off_t, m_off_t);

    static const char PAYMENT_PUBKEY[];

    void dodiscarduser(User* u, bool discardnotified);

    void enabletransferresumption(const char *loggedoutid = NULL);
    void disabletransferresumption(const char *loggedoutid = NULL);

    // application callbacks
    struct MegaApp* app;

    // event waiter
    shared_ptr<Waiter> waiter;

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

    // DbTable iface to handle "statecache" for logged in user (implemented at SqliteAccountState object)
    unique_ptr<DbTable> sctable;

    // NodeManager instance to wrap all access to Node objects
    NodeManager mNodeManager;

    recursive_mutex nodeTreeMutex;

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

    void handleDbError(DBError error);

    // notify the app about a fatal error (ie. DB critical error like disk is full)
    void fatalError(ErrorReason errorReason);

    // This method returns true when fatal failure has been detected
    // None actions has been taken yet (reload, restart app, ...)
    bool accountShouldBeReloadedOrRestarted() const;

    // This flag keeps the last error detected. It's overwritten by new errors and reset upon logout.It's cleaned after reload or other error is generated
    ErrorReason mLastErrorDetected = ErrorReason::REASON_ERROR_NO_ERROR;

    // initial state load in progress?  initial state can come from the database cache or via an 'f' command to the API.
    // Either way there can still be a lot of historic actionpackets to follow since that snaphot, especially if the user has not been online for a long time.
    bool fetchingnodes;
    int fetchnodestag;

    // set true after fetchnodes and catching up on actionpackets, stays true after that.
    std::atomic<bool> statecurrent;

    // actionpackets are up to date (similar to statecurrent but false if in the middle of spoonfeeding etc)
    std::atomic<bool> actionpacketsCurrent;

    // This flag is used to ensure we load Syncs just once per user session, even if a fetchnodes reload occurs after the first one
    bool syncsAlreadyLoadedOnStatecurrent = false;

    // subsequent fetchnodes should use the 'nocache' flag, so that we don't have difficulties with actionpackets getting to a later SCSN than we had before
    bool fetchnodesAlreadyCompletedThisSession = false;

    // File Attribute upload system.  These can come from:
    //  - upload transfers
    //  - app requests to attach a thumbnail/preview to a node
    //  - app requests for media upload (which return the fa handle)
    // initially added to queuedfa, and up to 10 moved to activefa.
    list<shared_ptr<HttpReqFA>> queuedfa;
    list<shared_ptr<HttpReqFA>> activefa;

    // API request queue double buffering:
    // reqs[r] is open for adding commands
    // reqs[r^1] is being processed on the API server
    HttpReq* pendingcs;

    // When triggering an API Hashcash challenge, the HTTP response will contain
    // X-Hashcash header, with relevant data to be saved and used for the next retry.
    string mReqHashcashToken;
    uint8_t mReqHashcashEasiness{};

    // Only queue the "Server busy" event once, until the current cs completes, otherwise we may DDOS
    // ourselves in cases where many clients get 500s for a while and then recover at the same time
    bool pendingcs_serverBusySent = false;

    // pending HTTP requests
    pendinghttp_map pendinghttp;

    // record type indicator for sctable
    // allways add new ones at the end of the enum, otherwise it will mess up the db!
    enum { CACHEDSCSN, CACHEDNODE, CACHEDUSER, CACHEDLOCALNODE, CACHEDPCR, CACHEDTRANSFER, CACHEDFILE, CACHEDCHAT, CACHEDSET, CACHEDSETELEMENT, CACHEDDBSTATE, CACHEDALERT } sctablerectype;

    void persistAlert(UserAlert::Base* a);

    // record type indicator for statusTable
    enum StatusTableRecType { CACHEDSTATUS };

    // open/create "statecache" and "nodes" tables in DB
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

    // actionpacket sequence tags (current refers to the one expected by the Requests)
    string mCurrentSeqtag;
    string mPriorSeqTag;
    bool mCurrentSeqtagSeen = false;
    int mCurrentSeqtagCmdtag = 0;

    // sc received seqtags to report to app (not tied to requests in this client)
    string mLastReceivedScSeqTag;
    string mLargestEverSeenScSeqTag;

    // records last seqTag, with allowance for future fields also
    ScDbStateRecord mScDbStateRecord;

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

    // maps the handle of the root of shares with their corresponding share key
    // out-shares: populated from 'ok0' element from `f` command
    // in-shares: populated from readnodes() for `f` command
    // map is cleared upon call to mergenewshares(), and used only temporary during `f` command.
    std::map<NodeHandle, std::vector<byte>> mNewKeyRepository;

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
    void removeAppData(void* t); // remove appdata (usually a MegaTransfer*) from every DirectRead

    // merge newly received share into nodes
    void mergenewshares(bool notify, bool skipWriteInDb = false);
    void mergenewshare(NewShare *s, bool notify, bool skipWriteInDb);    // merge only the given share

    // return the list of incoming shared folder (only top level, nested inshares are skipped)
    sharedNode_vector getInShares();

    template<typename Function>
    using IsIncomingShareIterator =
        std::conjunction<std::is_invocable<Function, std::shared_ptr<Node>>,
                         std::is_same<std::invoke_result_t<Function, std::shared_ptr<Node>>, void>>;

    // Call function on each incoming share.
    template<typename Function>
    auto forEachIncomingShare(Function&& function)
        -> std::enable_if_t<IsIncomingShareIterator<Function>::value, void>
    {
        // Iterate over each user.
        for (const auto& user: users)
        {
            // And each node handle shared by that user.
            for (const auto& handle: user.second.sharing)
            {
                // Get our hands on the incoming shared node.
                if (auto node = nodebyhandle(handle); node && node->parent == nullptr)
                {
                    // Pass the node to our iterator function.
                    std::invoke(function, std::move(node));
                }
            }
        }
    }

    // return the list of verified incoming shared folders (only top level, nested inshares are skipped)
    sharedNode_vector getVerifiedInShares();

    // return the list of unverified incoming shared folders (only top level, nested inshares are skipped)
    sharedNode_vector getUnverifiedInShares();

    // transfer queues (PUT/GET)
    transfer_multimap multi_transfers[2];
    BackoffTimerGroupTracker transferRetryBackoffs[2];
    uint32_t lastKnownCancelCount = 0;
#ifdef ENABLE_SYNC
    // track puts that may need finishing if sync abandoned before putnodes happens
    TransferBackstop transferBackstop;
#endif

    // transfer list to manage the priority of transfers
    TransferList transferlist;

    // cached transfers (PUT/GET)
    transfer_multimap multi_cachedtransfers[2];

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

    // raid transfers counter
    unsigned raidTransfersCounter{};

    // keep track of next transfer slot timeout
    BackoffTimerGroupTracker transferSlotsBackoff;

    // next TransferSlot to doio() on
    transferslot_list::iterator slotit;

    // transfer statistics manager
    stats::TransferStatsManager mTransferStatsManager;

    // send updates to app when the storage size changes
    int64_t mNotifiedSumSize = 0;

    // asymmetric to symmetric key rewriting
    handle_vector nodekeyrewrite;

    static const char* const EXPORTEDLINK;

    // default number of seconds to wait after a bandwidth overquota
    static dstime DEFAULT_BW_OVERQUOTA_BACKOFF_SECS;

    // number of seconds to invalidate the cached user data
    static dstime USER_DATA_EXPIRATION_BACKOFF_SECS;

    // total number of Node objects
    std::atomic_ullong totalNodes;

    // tracks how many nodes have had a successful applykey()
    std::atomic<long long> mAppliedKeyNodeCount;

    // server-client request sequence number
    SCSN scsn;

    // process an array of users from the API server
    bool readusers(JSON*, bool actionpackets);

    // process a JSON user object
    // possible results:
    // 0 -> no object found
    // 1 -> successful parsing
    // any other number -> parsing error
    int readuser(JSON*, bool actionpackets);

    user_vector usernotify;
    void notifyuser(User*);

    pcr_vector pcrnotify;
    void notifypcr(PendingContactRequest*);

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

    // process mcsm array at fetchnodes
    void procmcsm(JSON*);
#endif

#ifdef USE_MEDIAINFO
    MediaFileInfo mediaFileInfo;
#endif

    // write changed/added/deleted users to the DB cache and notify the
    // application
    void notifypurge();

    // If it's necessary, load nodes from data base
    shared_ptr<Node> nodeByHandle(NodeHandle);
    shared_ptr<Node> nodebyhandle(handle);

    shared_ptr<Node> nodeByPath(const char* path, std::shared_ptr<Node> node = nullptr, nodetype_t type = TYPE_UNKNOWN);

    using TotpTokenResult = std::pair<int, std::pair<std::string, unsigned>>;
    TotpTokenResult generateTotpTokenFromNode(const handle h);

#if ENABLE_SYNC
    std::shared_ptr<Node> nodebyfingerprint(LocalNode*);
#endif /* ENABLE_SYNC */

private:
    // Private helper method for getRecentActions
    recentactions_vector getRecentActionsFromSharedNodeVector(sharedNode_vector&& v);
public:
    // get a vector of recent actions in the account
    recentactions_vector getRecentActions(unsigned maxcount,
                                          m_time_t since,
                                          bool excludeSensitives = true);

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

    // determine if the file is a PDF.
    bool nodeIsPdf(const Node *n) const;

    // determine if the file is a presentation.
    bool nodeIsPresentation(const Node *n) const;

    // determine if the file is an archive.
    bool nodeIsArchive(const Node* n) const;

    // determine if the file is a program.
    bool nodeIsProgram(const Node* n) const;

    // determine if the file is miscellaneous.
    bool nodeIsMiscellaneous(const Node* n) const;

    // determine if the file is a spreadsheet.
    bool nodeIsSpreadsheet(const Node* n) const;

    // determine if the file is not in any of the other file types.
    bool nodeIsOtherType(const Node* n) const;

    // functions for determining whether we can clone a node instead of upload
    // or whether two files are the same so we can just upload/download the data once
    bool treatAsIfFileDataEqual(const FileFingerprint& nodeFingerprint,
                                const LocalPath& file2,
                                const std::string& filenameExtensionLowercaseNoDot) const;
    bool treatAsIfFileDataEqual(const FileFingerprint& fp1,
                                const std::string& filenameExtensionLowercaseNoDot1,
                                const FileFingerprint& fp2,
                                const std::string& filenameExtensionLowercaseNoDot2) const;

#ifdef ENABLE_SYNC

    // one unified structure for SyncConfigs, the Syncs that are running, and heartbeat data
    Syncs syncs;

    // indicates whether all startup syncs have been fully scanned
    bool syncsup;

    // sync debris folder name in //bin
    static const char* const SYNCDEBRISFOLDERNAME;

    // we are adding the //bin/SyncDebris/yyyy-mm-dd subfolder(s)
    bool syncdebrisadding;

    // minute of the last created folder in SyncDebris (don't attempt creation more frequently than once per minute)
    m_time_t syncdebrisminute;

    // move nodes to //bin/SyncDebris/yyyy-mm-dd/ or copy to //bin/SyncDebris/yyyy-mm-dd/ folder and unlink
    void movetosyncdebris(Node*, bool inshare, std::function<void(NodeHandle, Error)>&& completion, bool canChangeVault);

    // move queued nodes to SyncDebris (for syncing into the user's own cloud drive)
    void execmovetosyncdebris(Node* n, std::function<void(NodeHandle, Error)>&& completion, bool canChangeVault, bool isInshare);

    std::shared_ptr<Node> getOrCreateSyncdebrisFolder();
    struct pendingDebrisRecord {
        NodeHandle nodeHandle;
        std::function<void(NodeHandle, Error)> completion;
        bool mIsInshare = false;
        bool mCanChangeVault = false;
        pendingDebrisRecord(NodeHandle h, std::function<void(NodeHandle, Error)> c, bool inshare, bool changeVault)
            : nodeHandle(h), completion(c), mIsInshare(inshare), mCanChangeVault(changeVault) {}
    };
    list<pendingDebrisRecord> pendingDebris;

#endif
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

#ifdef ENABLE_CHAT
    // SFU id to specify the SFU server where all chat calls will be started
    int mSfuid = sfu_invalid_id;
#endif

    // process object arrays by the API server
    int readnodes(JSON*, int, putsource_t, vector<NewNode>*, bool modifiedByThisClient, bool applykeys, Node* priorActionpacketDeletedNode, bool* firstHandleMismatchedDelete);

    // process a JSON node object
    // possible results:
    // 0 -> no object found
    // 1 -> successful parsing
    // any other number -> parsing error
    int readnode(JSON*, int, putsource_t, vector<NewNode>*, bool modifiedByThisClient, bool applykeys,
                 NodeManager::MissingParentNodes& missingParentNodes, handle &previousHandleForAlert, set<NodeHandle> *allParents,
                 Node *priorActionpacketDeletedNode, bool *firstHandleMatchesDelete);

    void readok(JSON*);
    void readokelement(JSON*);
    void readoutshares(JSON*);
    void readoutshareelement(JSON*);

    void readipc(JSON*);
    void readopc(JSON*);

    error readmiscflags(JSON*);

    bool procph(JSON*);

    // process a JSON ph object
    // possible results:
    // 0 -> no object found
    // 1 -> successful parsing
    // any other number -> parsing error
    int procphelement(JSON*);

    void procsnk(JSON*);
    void procsuk(JSON*);

    void procmcf(JSON*);
    void procmcna(JSON*);

    void setkey(SymmCipher*, const char*);
    bool decryptkey(const char*, byte*, int, SymmCipher*, int, handle);

    void handleauth(handle, byte*);

    bool procsc();
    size_t procreqstat();

    // API warnings
    void warn(const char*);
    bool warnlevel();

    std::shared_ptr<Node> childnodebyname(const Node *parent, const char* name, bool skipFolders = false);
    sharedNode_vector childnodesbyname(Node* parent, const char* name, bool skipFolders = false);
    std::shared_ptr<Node> childnodebynametype(Node* parent, const char* name, nodetype_t mustBeType);
    std::shared_ptr<Node> childnodebyattribute(Node* parent, nameid attrId, const char* attrValue);

    static void honorPreviousVersionAttrs(Node *previousNode, AttrMap &attrs);

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
    static const int PUBLICSETHANDLE = Set::PUBLICHANDLESIZE;
    static const int SURVEYHANDLE = 8;

    // max new nodes per request
    static const int MAX_NEWNODES = 2000;

    // session ID length (binary)
    static const unsigned SIDLEN = 2 * SymmCipher::KEYLENGTH + USERHANDLE * 4 / 3 + 1;

    void proccr(JSON*);
    void procsr(JSON*);

    KeyManager mKeyManager;

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

    // Pending contact keys during initialization
    std::map<attr_t, set<handle>> mPendingContactKeys;

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
    User* getUserForSharing(const char* userID);
    void queuepubkeyreq(User*, std::unique_ptr<PubKeyAction>);
    void queuepubkeyreq(const char*, std::unique_ptr<PubKeyAction>);

    // rewrite foreign keys of the node (tree)
    void rewriteforeignkeys(std::shared_ptr<Node> n);

    // simple string hash
    static void stringhash(const char*, byte*, SymmCipher*);
    static uint64_t stringhash64(string*, SymmCipher*);

    // builds the authentication URI to be sent in POST requests
    string getAuthURI(bool supressSID = false, bool supressAuthKey = false);

    bool setlang(string *code);

    // create a new folder with given name and stores its node's handle into the user's attribute ^!bak
    error setbackupfolder(const char* foldername, int tag, std::function<void(Error)> addua_completion);

    // fetch backups and syncs from BC, search bkpId among them, pause/resume the backup or sync, update sds attribute
    void updateStateInBC(handle bkpId, CommandBackupPut::SPState newState, std::function<void(const Error&)> f);

    // fetch backups and syncs from BC, search bkpId among them, disable the backup or sync, update sds attribute, for a backup move or delete its contents
    void removeFromBC(handle bkpId, handle bkpDest, std::function<void(const Error&)> f);

    // fetch backups and syncs from BC
    void getBackupInfo(std::function<void(const Error&, const vector<CommandBackupSyncFetch::Data>&)> f);

    // sets the auth token to be used when logged into a folder link
    void setFolderLinkAccountAuth(const char *auth);

    // returns the public handle of the folder link if the account is logged into a public folder, otherwise UNDEF.
    handle getFolderLinkPublicHandle();

    // check if end call reason is valid
    bool isValidEndCallReason(int reason);

    // check if there is a valid folder link (rootnode received and the valid key)
    bool isValidFolderLink();

    //returns the top-level node for a node
    shared_ptr<Node> getrootnode(shared_ptr<Node>);

    //returns true if the node referenced by the handle belongs to the logged-in account
    bool isPrivateNode(NodeHandle h);

    //returns true if the node referenced by the handle belongs to other account than the logged-in account
    bool isForeignNode(NodeHandle h);

    // process node subtree
    void proctree(std::shared_ptr<Node>, TreeProc*, bool skipinshares = false, bool skipversions = false);

    // hash password
    error pw_key(const char*, byte*) const;

    // returns a pointer to tmptransfercipher, setting its key to the one provided;
    // tmptransfercipher key will change to be used right away; this is not a dedicated SymmCipher
    // for the transfer!
    SymmCipher* getRecycledTemporaryTransferCipher(const byte* newKey, int type = 1);

    // returns a pointer to tmpnodecipher, setting its key to the one provided;
    // tmpnodecipher key will change to be used right away; this is not a dedicated SymmCipher for
    // the node!
    SymmCipher* getRecycledTemporaryNodeCipher(const string* newKey);
    SymmCipher* getRecycledTemporaryNodeCipher(const byte* newKey);

    // request a link to recover account
    void getrecoverylink(const char *email, bool hasMasterkey);

    // query information about recovery link
    void queryrecoverylink(const char *link);

    // request private key for integrity checking the masterkey
    void getprivatekey(const char *code);

    // confirm a recovery link to restore the account
    void confirmrecoverylink(const char* code,
                             const char* email,
                             const char* password,
                             const byte* masterkey = NULL,
                             int ownAccountVersion = 1);

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
#ifdef ENABLE_SYNC
        CodeCounter::ScopeStats recursiveSyncTime = { "recursiveSync" };
        CodeCounter::ScopeStats computeSyncTripletsTime = { "computeSyncTriplets" };
        CodeCounter::ScopeStats inferSyncTripletsTime = { "inferSyncTriplets" };
        CodeCounter::ScopeStats syncItem = { "syncItem" };
        CodeCounter::ScopeStats syncItemCheckMove = { "syncItemCheckMove" };
        CodeCounter::ScopeStats syncItemXXX = { "syncItemXXX" };
        CodeCounter::ScopeStats syncItemXXF = { "syncItemXXF" };
        CodeCounter::ScopeStats syncItemXSX = { "syncItemXSX" };
        CodeCounter::ScopeStats syncItemXSF = { "syncItemXSF" };
        CodeCounter::ScopeStats syncItemCXX = { "syncItemCXX" };
        CodeCounter::ScopeStats syncItemCXF = { "syncItemCXF" };
        CodeCounter::ScopeStats syncItemCSX = { "syncItemCSX" };
        CodeCounter::ScopeStats syncItemCSF = { "syncItemCSF" };
        CodeCounter::ScopeStats clientThreadActions = { "clientThreadActions" };
#endif
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

    enum class ClientType
    {
        DEFAULT = 0,        // same as MegaApi::CLIENT_TYPE_DEFAULT
        VPN,
        PASSWORD_MANAGER,
    };

    MegaClient(MegaApp*, shared_ptr<Waiter>, HttpIO*, DbAccess*, GfxProc*, const char*, const char*, unsigned workerThreadCount, ClientType clientType = ClientType::DEFAULT);
    ~MegaClient();

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

// JourneyID for cs API requests and log events. Populated from "ug"/"gmf" commands response.
// It is kept in memory and persisted in disk until a full logout.
struct JourneyID
{
private:
    // The JourneyID value - a 16-char hex string (or an empty string if it hasn't been retrieved yet)
    string mJidValue;
    // The tracking flag: used to attach the JourneyID to cs requests
    bool mTrackValue;
    // Local cache file
    unique_ptr<FileSystemAccess>& mClientFsaccess;
    LocalPath mCacheFilePath;
    bool storeValuesToCache(bool storeJidValue, bool storeTrackValue) const;

public:
    static constexpr size_t HEX_STRING_SIZE = 16;
    JourneyID(unique_ptr<FileSystemAccess>& clientFsaccess, const LocalPath& rootPath);
    // Updates the JourneyID and the tracking flag based on the provided jidValue, which must be a 16-char hex string.
    // When jidValue is not empty:
    // - Sets mJidValue to jidValue only if it is currently unset (empty).
    // - Sets mTrackValue if it is currently unset (false).
    // When jidValue is empty:
    // - Keeps mJidValue unchanged.
    // - Unsets mTrackValue if it is currently set (true).
    // Returns true if either the JourneyID (mJidValue) or the tracking flag (mTrackValue) have been updated.
    bool setValue(const string& jidValue);
    // Get the JourneyID (empty if still unset)
    string getValue() const;
    // Check if the tracking flag is set, i.e.: the JourneyID must be tracked (used in cs API reqs)
    bool isTrackingOn() const;
    // Load the JourneyID and the tracking flag stored in the cache file.
    bool loadValuesFromCache();
    // Remove local cache file and reset the JourneyID so a new one can be set from the next "ug"/"gmf" command.
    bool resetCacheAndValues();
};

    ClientType getClientType() const { return mClientType; }
    bool isClientType(const ClientType& t) const { return mClientType == t; }

private:
    ClientType mClientType;

    // Since it's quite expensive to create a SymmCipher, this are provided to use for quick operations - just set the key and use.
    SymmCipher tmpnodecipher;

    // Since it's quite expensive to create a SymmCipher, this is provided to use for quick operation - just set the key and use.
    SymmCipher tmptransfercipher;

    error changePasswordV1(User* u, const char* password, const char* pin);
    error changePasswordV2(const char* password, const char* pin);
    void fillCypheredAccountDataV2(const char* password, vector<byte>& clientRandomValue, vector<byte>& encmasterkey,
                                   string& hashedauthkey, string& salt);

    static vector<byte> deriveKey(const char* password, const string& salt, size_t derivedKeySize);

//
// JourneyID and ViewID
//
    // JourneyID for cs API requests and log events
    std::unique_ptr<JourneyID> mJourneyId;

public:

    // Checks if there is a valid JourneyID and tracking flag is set
    bool trackJourneyId() const;

    // Retrieves the JourneyID value, which is a 16-character hexadecimal string (for submission to the API)
    // If the JourneyID is still unset, it returns an empty string.
    string getJourneyId() const;

    // Load the JourneyID values from the local cache.
    bool loadJourneyIdCacheValues();

    // Set the JourneyID value from a 16-character hexadecimal string (obtained from API commands "ug"/"gmf")
    // See JourneyID::setValue() for full doc
    bool setJourneyId(const string& jid);

    // Generates a unique ViewID that the caller should store and can optionally use in subsequent sendevent() calls.
    // ViewID is employed by apps for event logging. It is generated by the SDK to ensure consistent and shared logic across applications.
    static string generateViewId(PrnGen& rng);

//
// Sets and Elements
//

    // generate "asp" command
    void putSet(Set&& s, std::function<void(Error, const Set*)> completion);

    // generate "asr" command
    void removeSet(handle setID, std::function<void(Error)> completion);

    // generate "aft" command
    void fetchSetInPreviewMode(std::function<void(Error, Set*, elementsmap_t*)> completion);

    // generate "aepb" command
    void putSetElements(vector<SetElement>&& els, std::function<void(Error, const vector<const SetElement*>*, const vector<int64_t>*)> completion);

    // generate "aep" command
    void putSetElement(SetElement&& el, std::function<void(Error, const SetElement*)> completion);

    // generate "aerb" command
    void removeSetElements(handle setID,
                           vector<handle>&& eids,
                           std::function<void(Error, const vector<int64_t>*)> completion);

    // generate "aer" command
    void removeSetElement(handle setID, handle eid, std::function<void(Error)> completion);

    // handle "aesp" parameter, part of 'f'/ "fetch nodes" response
    bool procaesp(JSON& j);

    // load Sets and Elements from json
    error readSetsAndElements(JSON& j, map<handle, Set>& newSets, map<handle, elementsmap_t>& newElements);

    // return Set with given setID or nullptr if it was not found
    const Set* getSet(handle setID) const;

    // return all available Sets, indexed by id
    const map<handle, Set>& getSets() const { return mSets; }

    // add new Set or replace exisiting one
    const Set* addSet(Set&& a);

    // search for Set with the same id, and update its members
    bool updateSet(Set&& s);

    // delete Set with setID from local memory; return true if found and deleted
    bool deleteSet(handle setID);

    // return Element count for Set with setID, or 0 if not found
    unsigned getSetElementCount(handle setID) const;

    // return Element with given eid from Set with setID, or nullptr if not found
    const SetElement* getSetElement(handle setID, handle eid) const;

    // return all available Elements in a Set, indexed by eid
    const elementsmap_t* getSetElements(handle setID) const;

    // add new SetElement or replace exisiting one
    const SetElement* addOrUpdateSetElement(SetElement&& el);

    // delete Element with eid from Set with setID in local memory; return true if found and deleted
    bool deleteSetElement(handle setID, handle eid);

    // return true if Set with given setID was exported (has a public link)
    bool isExportedSet(handle setID) const;

    void exportSet(handle setID, bool makePublic, std::function<void(Error)> completion);

    // returns result of the operation and the link created
    pair<error, string> getPublicSetLink(handle setID) const;

    // returns error code and public handle for the link provided as a param
    error fetchPublicSet(const char* publicSetLink, std::function<void(Error, Set*, elementsmap_t*)>);

    void stopSetPreview() { if (mPreviewSet) mPreviewSet.reset(); }

    bool inPublicSetPreview() const { return !!mPreviewSet; }

    const SetElement* getPreviewSetElement(handle eid) const
    { return isElementInPreviewSet(eid) ? &mPreviewSet->mElements[eid] : nullptr; }

    const Set* getPreviewSet() const { return inPublicSetPreview() ? &mPreviewSet->mSet : nullptr; }
    const elementsmap_t* getPreviewSetElements() const
    { return inPublicSetPreview() ? &mPreviewSet->mElements : nullptr; }

private:
    error readSets(JSON& j, map<handle, Set>& sets);
    error readSet(JSON& j, Set& s);
    error readElements(JSON& j, map<handle, elementsmap_t>& elements);
    error readElement(JSON& j, SetElement& el);
    error readAllNodeMetadata(JSON& j, map<handle, SetElement::NodeMetadata>& nodes);
    error readSingleNodeMetadata(JSON& j, SetElement::NodeMetadata& node);
    bool decryptNodeMetadata(SetElement::NodeMetadata& nodeMeta, const string& encryptionKey);
    error readExportedSet(JSON& j, Set& s, pair<bool, m_off_t>& exportRemoved);
    error readSetsPublicHandles(JSON& j, map<handle, Set>& sets);
    error readSetPublicHandle(JSON& j, map<handle, Set>& sets);
    void fixSetElementWithWrongKey(const Set& set);
    size_t decryptAllSets(map<handle, Set>& newSets, map<handle, elementsmap_t>& newElements, map<handle, SetElement::NodeMetadata>* nodeData);
    error decryptSetData(Set& s);
    error decryptElementData(SetElement& el, const string& setKey);
    string decryptKey(const string& encryptedKey, SymmCipher& cipher) const;
    bool decryptAttrs(const string& attrs, const string& decrKey, string_map& output);
    string encryptAttrs(const string_map& attrs, const string& encryptionKey);

    void sc_asp(); // AP after new or updated Set
    void sc_asr(); // AP after removed Set
    void sc_aep(); // AP after new or updated Set Element
    void sc_aer(); // AP after removed Set Element
    void sc_ass(); // AP after exported set update

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
    void clearsetelementnotify(handle setID);
    vector<SetElement*> setelementnotify;
    map<handle, elementsmap_t> mSetElements; // indexed by Set id, then Element id

    struct SetLink
    {
        handle mPublicId = UNDEF; // same as mSet.mPublicId once fetched
        string mPublicKey;
        string mPublicLink;
        Set mSet;
        elementsmap_t mElements;
    };
    unique_ptr<SetLink> mPreviewSet;

    bool isElementInPreviewSet(handle eid) const
    { return mPreviewSet && (mPreviewSet->mElements.find(eid) != end(mPreviewSet->mElements)); }
// -------- end of Sets and Elements

    // Generates a key pair (x25519 (Cu) key pair) to use for Vpn Credentials (MegaClient::putVpnCredential)
    StringKeyPair generateVpnKeyPair();

    /**
     * @brief Stores json serialized `data` into the  `attrs` `NODE_ATTR_PASSWORD_MANAGER` field
     *
     * @param attrs The node attribute map to write the serialized password data
     * @param data The attribute map containing password-related information.
     */
    void preparePasswordManagerNodeData(attr_map& attrs, const AttrMap& data) const;

    /**
     * @brief Checks preconditions for node rename operations on the given node.
     *
     * @return  Possible returned error codes:
     * - API_EPAYWALL: If over disk quota paywall
     * - API_EARGS: If the given node is nullptr
     * - API_EACCESS: If the node is not full accessible
     * - API_OK: No problems to rename the node
     */
    error checkRenameNodePrecons(std::shared_ptr<Node> n);

    // Get a string to complete sc/wsc url and receive partial action packages
    // It's used from clients of type PWD and VPN
    std::string getPartialAPs();

public:
    /* Mega VPN methods */

    // Call "vpnr" command.
    void getVpnRegions(CommandGetVpnRegions::Cb&& = nullptr /* Completion */);

    // Call "vpng" command.
    void getVpnCredentials(CommandGetVpnCredentials::Cb&& = nullptr /* Completion */);

    // Call "vpnp" command.
    void putVpnCredential(std::string&& /* VPN Region */, CommandPutVpnCredential::Cb&& = nullptr /* Completion */);

    // Call "vpnd" command.
    void delVpnCredential(int /* SlotID */, CommandDelVpnCredential::Cb&& = nullptr /* Completion */);

    // Call "vpnc" command.
    void checkVpnCredential(std::string&& /* User Public Key */, CommandCheckVpnCredential::Cb&& = nullptr /* Completion */);

    /**
     * @brief Generates a VPN credential string equivalent to the .conf file generated by the
     * webclient. This method is meant to be called with the credential details obtained from
     * CommandPutVpnCredential.
     *
     * Content:
     *    [Interface]
     *    PrivateKey = User Private Key
     *    Address = IPv4, IPv6 of the Region
     *    DNS = DNS of the Cluster
     *
     *    [Peer]
     *    PublicKey = Cluster Public Key
     *    AllowedIPs = 0.0.0.0/0, ::/0
     *    Endpoint = host of the Cluster
     *
     * @note These VPN credential details are not the same than the data obtained from
     * MegaClient::getVpnCredentials()
     * @see MegaClient::putVpnCredential()
     * @return The string with the VPN credentials specified above.
     */
    string generateVpnCredentialString(
        const std::string& /* host */,
        const std::vector<std::string>& /* DNS */,
        std::string&& /* IPv4 */,
        std::string&& /* IPv6 */,
        StringKeyPair&& /* Peer Key Pair <User Private, Cluster Public> */);

    void getNetworkConnectivityTestServerInfo(
        CommandGetNetworkConnectivityTestServerInfo::Completion&& completion);

    void runNetworkConnectivityTest(
        std::function<void(const Error&, NetworkConnectivityTestResults&&)>&& completion);
    void sendNetworkConnectivityTestEvent(const NetworkConnectivityTestResults& results);
    /* Mega VPN methods END */

    void fetchCreditCardInfo(CommandFetchCreditCardCompletion completion);
    void setProFlexi(bool newProFlexi);

    // Password Manager
    static const char* const NODE_ATTR_PASSWORD_MANAGER;
    static constexpr std::string_view PWM_ATTR_NODE_TYPE{"t"};
    static constexpr std::string_view PWM_ATTR_NODE_TYPE_CREDIT_CARD{"c"};
    static constexpr std::string_view PWM_ATTR_CREDIT_CARD_NUMBER{"nu"};
    static constexpr std::string_view PWM_ATTR_CREDIT_NOTES{"n"};
    static constexpr std::string_view PWM_ATTR_CREDIT_CARD_HOLDER{"u"};
    static constexpr std::string_view PWM_ATTR_CREDIT_CVV{"cvv"};
    static constexpr std::string_view PWM_ATTR_CREDIT_EXP_DATE{"exp"};
    static constexpr std::string_view PWM_ATTR_PASSWORD_NOTES{"n"};
    static constexpr std::string_view PWM_ATTR_PASSWORD_URL{"url"};
    static constexpr std::string_view PWM_ATTR_PASSWORD_USERNAME{"u"};
    static constexpr std::string_view PWM_ATTR_PASSWORD_PWD{"pwd"};
    static constexpr std::string_view PWM_ATTR_PASSWORD_TOTP{"totp"};
    static constexpr std::string_view PWM_ATTR_PASSWORD_TOTP_SHSE{"shse"};
    static constexpr std::string_view PWM_ATTR_PASSWORD_TOTP_EXPT{"t"};
    static constexpr std::string_view PWM_ATTR_PASSWORD_TOTP_HASH_ALG{"alg"};
    static constexpr std::string_view PWM_ATTR_PASSWORD_TOTP_NDIGITS{"nd"};

    enum class PwmEntryType
    {
        PASSWORD,
        CREDIT_CARD,
    };

    /**
     * @brief This method converts the value in the field PWM_ATTR_NODE_TYPE into a PwmEntryType. If
     * the value is not expected this method returns nullopt
     */
    static std::optional<PwmEntryType> toPwmEntryType(const std::optional<std::string_view>& t)
    {
        if (!t) // Password entries have no PWM_ATTR_NODE_TYPE field
            return PwmEntryType::PASSWORD;

        if (*t == PWM_ATTR_NODE_TYPE_CREDIT_CARD)
            return PwmEntryType::CREDIT_CARD;
        return std::nullopt;
    }

    /**
     * @brief Checks if the given `data` stores the provided `type` of PWM node
     * @note `data` must be an AttrMap obtained from reading the NODE_ATTR_PASSWORD_MANAGER node
     * attribute.
     */
    static bool isPwmDataOfType(const AttrMap& data, const PwmEntryType type)
    {
        return toPwmEntryType(data.getStringView(MegaClient::PWM_ATTR_NODE_TYPE)) == type;
    }

    NodeHandle getPasswordManagerBase();
    void createPasswordManagerBase(int rtag,
                                   CommandCreatePasswordManagerBase::Completion cbRequest);

    /** @brief The same as updatePasswordNode but for updating a Credit Card Node */
    error updateCreditCardNode(const NodeHandle nh,
                               std::unique_ptr<AttrMap> newData,
                               CommandSetAttr::Completion&& cb);

    /**
     * @brief Updates the password node data stored in the node with the given `nh` with the
     * provided `newData`
     *
     * @param nh Handle of the node to update
     * @param newData Map with the new data. Notice that fields set to empty strings will cause the
     * data of that field to be removed.
     * @param cb The callback to forward to the `setattr` call.
     * @return An error code that can be:
     * - API_EARGS, if:
     *   + newData is empty or nullptr
     *   + The node to update is not a password node
     *   + The updates leave the node in an invalid state
     * - All the possible codes returned by MegaClient::checkRenameNodePrecons
     * - All the possible codes returned by MegaClient::setattr
     */
    error updatePasswordNode(const NodeHandle nh,
                             std::unique_ptr<AttrMap> newData,
                             CommandSetAttr::Completion&& cb);

    // Data type to call putnodes and create password nodes
    using ValidPasswordData = std::map<std::string, std::unique_ptr<AttrMap>>;
    // Data type to handle wrongly formatted password info. Key: info, val: ErrCode
    using BadPasswordData = std::map<std::string, PasswordEntryError>;

    // Validator for data of a new PasswordManagerNode
    using PasswordDataValidator = std::function<PasswordEntryError(const AttrMap&)>;

    // Check createPasswordEntries
    error createPasswordEntry(const char* name,
                              std::unique_ptr<AttrMap> data,
                              MegaClient::PasswordDataValidator dataValidator,
                              std::shared_ptr<Node> nParent,
                              int rtag);

    /**
     * @brief Creates multiple PasswordManagerNode instances with a single putnodes call
     *
     * @note API_EARGS will be returned if:
     *     - nParent is not a PasswordManagerNode folder
     *     - If any of the given values in data is invalid, e.g., the password field is missing
     *
     * @param data A map with the name of the password entry to create as key and the information of
     * the password (AttrMap) as values.
     * @param dataValidator a std::function to validate the given data before creating a new
     * PasswordManagerNode. Note: The same validator will apply to all the entries in the data
     * parameter.
     * @param nParent The parent node that will contain the nodes to be created
     * @param rTag tag parameter for putnodes call
     * @return error code (API_OK if succeeded)
     */
    error createPasswordEntries(ValidPasswordData&& data,
                                PasswordDataValidator dataValidator,
                                std::shared_ptr<Node> nParent,
                                int rTag);

    using ImportPaswordResult = std::tuple<error, MegaClient::BadPasswordData, std::size_t>;

    /**
     * @brief Import password nodes from a file
     *
     * @param filePath Path to the file with the entries to import
     * @param source Specifies the source of the input file (e.g. `FileSource::GOOGLE_PASSWORD`)
     * @param parentHandle The handle of the parent node that will contain the imported entries
     * @param rTag tag parameter for putnodes call
     * @return std::tuple with
     * - error: An error code indicating that the whole import operation failed.
     *   + API_EARGS:
     *      * The given parent handle doesn't map to a node
     *      * Invalid file format for the given source
     *      * No valid entries in the file
     *   + API_EREAD:
     *      * The input file cannot be opened
     *   + API_EACCESS:
     *      * Problem loading the file once opened, e.g. invalid header
     *   + Errors returned by `MegaClient::createPasswordNodes`
     * - BadPasswordData: A vector of pairs<string, PasswordEntryError> with the lines that were not
     *   successfully parsed (first) and an error code (second) to specify the reason.
     * - size_t: The number of successfully parsed entries that were sent to putnodes
     *   @note This number can be 0, in that case, no call to putnodes will be done.
     */
    ImportPaswordResult importPasswordsFromFile(const std::string& filePath,
                                                const pwm::import::FileSource source,
                                                const NodeHandle parentHandle,
                                                const int rTag);

    /**
     * @brief Validates the format of TOTP data stored in the given attribute map.
     *
     * This function checks the validity of essential TOTP fields:
     * - Shared secret (`PWM_ATTR_PASSWORD_TOTP_SHSE`)
     * - Number of digits (`PWM_ATTR_PASSWORD_TOTP_NDIGITS`)
     * - Expiry time (`PWM_ATTR_PASSWORD_TOTP_EXPT`)
     * - Hash algorithm (`PWM_ATTR_PASSWORD_TOTP_HASH`)
     *
     * If any field is ill-formed, an appropriate error code is returned.
     *
     * @param data The attribute map containing TOTP-related information.
     * @return PasswordEntryError An error code indicating validation status:
     * - `OK` if all fields are correctly formed.
     * - `INVALID_TOTP_SHARED_SECRET` if the shared secret is invalid.
     * - `INVALID_TOTP_NDIGITS` if the number of digits is invalid.
     * - `INVALID_TOTP_EXPT` if the expiration time is invalid.
     * - `INVALID_TOTP_ALG` if the hash algorithm is invalid.
     *
     * @note Uses `totp::validateFields` to check field validity.
     */
    static PasswordEntryError validateTotpDataFormat(const AttrMap& data);

    /**
     * @brief Validates the TOTP data for a new node.
     *
     * This function checks if the TOTP shared secret (`PWM_ATTR_PASSWORD_TOTP_SHSE`)
     * is present in the provided attribute map. If it is missing, an error is returned.
     * Otherwise, it validates the rest of the fields by calling `validateTotpDataFormat`.
     *
     * @param data The attribute map containing TOTP data attr map.
     * @return PasswordEntryError An error code indicating validation status:
     * - `MISSING_TOTP_SHARED_SECRET` if the shared secret is missing.
     * - `MISSING_TOTP_NDIGITS` if the number of digits is missing.
     * - `MISSING_TOTP_EXPT` if the expiration time is missing.
     * - `MISSING_TOTP_HASH_ALG` if the hashing algorithm is missing.
     * - The result of `validateTotpDataFormat(data)`, which checks other fields.
     */
    static PasswordEntryError validateNewNodeTotpData(const AttrMap& data);

    /**
     * @brief Ensures the given data can be used to create a new password node.
     *
     * For instance, the password field is mandatory and must be present.
     *
     * @param data The map with the password data
     * @return The error code for the validation
     */
    static PasswordEntryError validateNewPasswordNodeData(const AttrMap& data);

    /**
     * @brief validateNewCreditCardNodeData
     * @param data
     * @return
     */
    static PasswordEntryError validateNewCreditCardNodeData(const AttrMap& data);

    /**
     * @brief Processes the input password entries and splits them into two containers (bad, good).
     *
     * This method is designed to process the input entries from the mResults member of the
     * pwm::import::PassFileParseResult class.
     *
     * Entries that have any issues (e.g., problems parsing them, missing mandatory fields) are
     * returned in the .first member of the return value. This is a map with the original content
     * that caused the problem as the key and the error code as the value.
     *
     * Entries that are ready to create a new password node are placed in the .second container.
     * This is again a map with the final name of the node (ensuring no name conflicts) as the keys
     * and the data as the values.
     *
     * @param entries A container such as the mResults member of the
     * pwm::import::PassFileParseResult class.
     * @param nameValidator The functor that knows the state of the target parent node and handles
     * name clashing issues. Note: This can be generalized to any object that defines operator()
     * and takes and returns a std::string.
     * @return A pair of [badEntries, goodEntries].
     */
    static std::pair<BadPasswordData, ValidPasswordData>
        validatePasswordEntries(std::vector<pwm::import::PassEntryParseResult>&& entries,
                                ncoll::NameCollisionSolver& nameValidator);

    static std::string generatePasswordChars(const bool useUpper,
                                             const bool useDigits,
                                             const bool useSymbols,
                                             const unsigned int length);

    void setEnabledNotifications(std::vector<uint32_t>&& notifs) { mEnabledNotifications = std::move(notifs); }
    const std::vector<uint32_t>& getEnabledNotifications() const { return mEnabledNotifications; }
    void getNotifications(CommandGetNotifications::ResultFunc onResult);
    std::pair<uint32_t, uint32_t> getFlag(const char* flagName);

    void getActiveSurveyTriggerActions(
        CommandGetActiveSurveyTriggerActions::Completion&& completion);

    void getSurvey(unsigned int triggerActionId, CommandGetSurvey::Completion&& completion);

    void answerSurvey(const CommandAnswerSurvey::Answer& answer,
                      CommandAnswerSurvey::Completion&& completion);

    using GetJSCDataCallback = std::function<void(JSCData, Error)>;

    /**
     * @brief
     * This function will retrieve the user's JSCD user attributes and pass
     * them to the provided callback.
     *
     * If the user does not have any JSCD user attributes, this function
     * will create them and pass them to the provided callback.
     *
     * @param callback
     * The function that should receive the user's JSCD user attributes.
     */
    void getJSCData(GetJSCDataCallback callback);

    void getMyIp(CommandGetMyIP::Cb&& completion);

    // FUSE client adapter.
    fuse::ClientAdapter mFuseClientAdapter;

    // FUSE service.
    fuse::Service mFuseService;

private:
    /**
     * @brief
     * This function will create the user's JSCD user attributes. If
     * successful, the new attributes will be forwarded to the provided
     * callback. If not, the reason why the attributes couldn't be created
     * will be forwarded to the provided callback.
     *
     * @param callback
     * The function that should receive the user's JSCD user attributes or
     * the reason why those attributes couldn't be created.
     */
    void createJSCData(GetJSCDataCallback callback);

#ifdef ENABLE_SYNC
    /**
     * @brief
     * The purpose of this function is to execute before the user's provided
     * login callback so that we can perform some administrative functions
     * necessary to bootstrap the sync engine.
     *
     * One of these duties is to ensure that the user has a set of JSCD user
     * attributes, another is to safely inject these attributes along with
     * the client's master key into the sync engine.
     *
     * @param callback
     * The function that should be called when login has completed.
     *
     * @param result
     * The result of our attempt to log the user in.
     */
    void injectSyncSensitiveData(CommandLogin::Completion callback,
                                 Error result);
#endif // ENABLE_SYNC

    /**
     * @brief
     * This function is called after the user's JSCD user attributes have
     * been created. If the attributes were created successfully, their
     * content is passed to the provided callback. If not, the error
     * received by this function is passed to the provided callback.
     *
     * @param callback
     * The function that should receive the user's JSCD user attributes or
     * the reason why those attributes couldn't be created.
     *
     * @param result
     * The result of our attempt to create the user's JSCD user attributes.
     */
    void JSCDataCreated(GetJSCDataCallback& callback,
                        Error result);

    /**
     * @brief
     * This function is called when the user's JSCD user attributes have
     * been retrieved. If successful, the JSCD user attributes are
     * destructured and passwed to the provided callback. If not, the reason
     * why the attributes could not be retrieved is passed to the provided
     * callback.
     *
     * @param callback
     * The function that receive the user's JSCD user attributes or the
     * reason why those attributes could not be retrieved.
     *
     * @param result
     * The result of our attempt to retrieve the user's JSCD user
     * attributes.
     *
     * @param store
     * The TLV store containing the user's JSCD user attributes.
     */
    void JSCDataRetrieved(GetJSCDataCallback& callback,
                          Error result,
                          std::unique_ptr<string_map> store);

    // Last known capacity retrieved from the cloud.
    m_off_t mLastKnownCapacity = -1;
};

} // namespace

#define char_is_not_digit [](unsigned char c) { return !::mega::is_digit(c); }
#define char_is_not_space [](unsigned char c) { return !::mega::is_space(c); }

#endif
