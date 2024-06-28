/**
 * @file mega/command.h
 * @brief Request command component
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

#ifndef MEGA_COMMAND_H
#define MEGA_COMMAND_H 1

#include <memory>

#include "types.h"
#include "node.h"
#include "account.h"
#include "http.h"
#include "json.h"
#include "textchat.h"
#include "nodemanager.h"

namespace mega {

struct JSON;
struct MegaApp;
// request command component

class MEGA_API Command
{
    error result;

protected:
    bool canceled;

    JSONWriter jsonWriter;
    bool mRead = false;// if json has already been read

    bool loadIpsFromJson(std::vector<string>& ips, JSON& json);
    bool cacheresolvedurls(const std::vector<string>& urls, std::vector<string>&& ips);

public:
    MegaClient* client; // non-owning

    int tag;
    string commandStr;

    // some commands can only succeed if they are in their own batch.  eg. smss, when the account is blocked pending validation
    bool batchSeparately;

    // true if the command processing has been updated to use the URI v3 system, where successful state updates arrive via actionpackets.
    bool mV3 = true;

    // true if the command returns strings, arrays or objects, but a seqtag is (optionally) also required. In example: ["seqtag"/error, <JSON from before v3>]
    bool mSeqtagArray = false;

    // filters for JSON parsing in streaming
    std::map<std::string, std::function<bool(JSON *)>> mFilters;

    void cmd(const char*);
    void notself(MegaClient*);
    virtual void cancel(void);

    void arg(const char*, const char*, int = 1);
    void arg(const char*, const byte*, int);
    void arg(const char*, NodeHandle);
    void arg(const char*, m_off_t);
    void addcomma();
    void appendraw(const char*);
    void appendraw(const char*, int);
    void beginarray();
    void beginarray(const char*);
    void endarray();
    void beginobject();
    void beginobject(const char*);
    void endobject();
    void element(int);
    void element(handle, int = sizeof(handle));
    void element(const byte*, int);
    void element(const char*);

    void openobject();
    void closeobject();

    // `st` seqtags are always extracted before the command's procresult() is called
    enum Outcome {  CmdError,            // The reply was an error, already extracted from the JSON.  The error code may have been 0 (API_OK)
                    CmdArray,            // The reply was an array, and we have already entered it
                    CmdObject,           // the reply was an object, and we have already entered it
                    CmdItem };           // The reply was none of the above - so a string

    struct Result
    {
        Outcome mOutcome = CmdError;
        Error mError = API_OK;
        Result(Outcome o, Error e = API_OK) : mOutcome(o), mError(e) {}

        bool succeeded() const
        {
            return mOutcome != CmdError || error(mError) == API_OK;
        }

        bool hasJsonArray() const
        {
            // true if there is JSON Array to process (and we have already entered it) (note some commands that respond with cmdseq plus JSON, so this can happen for actionpacket results)
            return mOutcome == CmdArray;
        }

        bool hasJsonObject() const
        {
            // true if there is JSON Object to process (and we have already entered it) (note some commands that respond with cmdseq plus JSON, so this can happen for actionpacket results)
            return mOutcome == CmdObject;
        }

        bool hasJsonItem() const
        {
            // true if there is JSON to process but it's not an object or array (note some commands that respond with cmdseq plus JSON, so this can happen for actionpacket results)
            return mOutcome == CmdItem;
        }

        Error errorOrOK() const
        {
            assert(mOutcome == CmdError);
            return mOutcome == CmdError ? mError : Error(API_EINTERNAL);
        }

        bool wasErrorOrOK() const
        {
            return mOutcome == CmdError;
        }

        bool wasError(error e) const
        {
            return mOutcome == CmdError && error(mError) == e;
        }

        bool wasStrictlyError() const
        {
            return mOutcome == CmdError && error(mError) != API_OK;
        }

    };

    virtual bool procresult(Result, JSON&) = 0;

    // json for the command is usually pre-generated but can be calculated just before sending, by overriding this function
    virtual const char* getJSON(MegaClient* client);

    Command();
    virtual ~Command();

    bool checkError(Error &errorDetails, JSON &json);

    void addToNodePendingCommands(Node* n);
    void removeFromNodePendingCommands(NodeHandle h, MegaClient* client);

#ifdef ENABLE_CHAT
    // create json structure for scheduled meetings (mcsmp command)
    void createSchedMeetingJson(const ScheduledMeeting* schedMeeting);
#endif

    MEGA_DEFAULT_COPY_MOVE(Command)
};

// list of new file attributes to write
// file attribute put

struct MEGA_API CommandPutFA : public Command
{
    using Cb = std::function<void(Error, const std::string &/*url*/, const vector<std::string> &/*ips*/)>;

private:
    Cb mCompletion;
    NodeOrUploadHandle th;    // if th is UNDEF, just report the handle back to the client app rather than attaching to a node

public:
    bool procresult(Result, JSON&) override;


    CommandPutFA(NodeOrUploadHandle, fatype, bool usehttps, int tag, size_t size_only,
                 bool getIP = true, Cb &&completion = nullptr);
};

struct MEGA_API HttpReqFA : public HttpReq, public std::enable_shared_from_this<HttpReqFA>
{
    NodeOrUploadHandle th;    // if th is UNDEF, just report the handle back to the client app rather than attaching to a node
    fatype type;
    m_off_t progressreported;

    // progress information
    virtual m_off_t transferred(MegaClient*) override;

    // either supply only size (to just get the URL) or supply only the data for auto-upload (but not both)
    HttpReqFA(NodeOrUploadHandle, fatype, bool usehttps, int tag,
                        std::unique_ptr<string> faData, bool getIP, MegaClient* client);

    // generator function because the code allows for retries
    std::function<CommandPutFA*()> getURLForFACmd;
    int tag = 0;

private:
    std::unique_ptr<string> data;
};

class MEGA_API CommandGetFA : public Command
{
    int part;

public:
    bool procresult(Result, JSON&) override;

    CommandGetFA(MegaClient *client, int, handle);
};

class MEGA_API CommandPrelogin : public Command
{
public:
    using Completion = std::function<void(int, string*, string*, error)>;

    Completion mCompletion;
    string email;

public:
    bool procresult(Result, JSON&) override;

    CommandPrelogin(MegaClient* client, Completion completion, const char* email);
};

class MEGA_API CommandLogin : public Command
{
public:
    using Completion = std::function<void(error)>;

private:
    Completion mCompletion;
    bool checksession;
    int sessionversion;

public:
    bool procresult(Result, JSON&) override;

    CommandLogin(MegaClient* client,
                 Completion completion,
                 const char* email,
                 const byte* emailhash,
                 int emailhashsize,
                 const byte* sessionkey = NULL,
                 int csessionversion = 0,
                 const char* pin = NULL);
};

class MEGA_API CommandSetMasterKey : public Command
{
    byte newkey[SymmCipher::KEYLENGTH];
    string salt;

public:
    bool procresult(Result, JSON&) override;

    CommandSetMasterKey(MegaClient*, const byte*, const byte *, int, const byte* clientrandomvalue = NULL, const char* = NULL, string* = NULL);
};

class MEGA_API CommandAccountVersionUpgrade : public Command
{
    vector<byte> mEncryptedMasterKey;
    string mSalt;
    std::function<void(error e)> mCompletion;

public:
    bool procresult(Result, JSON&) override;

    CommandAccountVersionUpgrade(vector<byte>&& clRandValue, vector<byte>&&encMKey, string&& hashedAuthKey, string&& salt, int ctag,
        std::function<void(error e)> completion);
};

class MEGA_API CommandCreateEphemeralSession : public Command
{
    byte pw[SymmCipher::KEYLENGTH];

public:
    bool procresult(Result, JSON&) override;

    CommandCreateEphemeralSession(MegaClient*, const byte*, const byte*, const byte*);
};

class MEGA_API CommandResumeEphemeralSession : public Command
{
    byte pw[SymmCipher::KEYLENGTH];
    handle uh;

public:
    bool procresult(Result, JSON&) override;

    CommandResumeEphemeralSession(MegaClient*, handle, const byte*, int);
};

class MEGA_API CommandCancelSignup : public Command
{
public:
    bool procresult(Result, JSON&) override;

    CommandCancelSignup(MegaClient*);
};

class MEGA_API CommandWhyAmIblocked : public Command
{
public:
    bool procresult(Result, JSON&) override;

    CommandWhyAmIblocked(MegaClient*);
};

class MEGA_API CommandSendSignupLink2 : public Command
{
public:
    bool procresult(Result, JSON&) override;

    CommandSendSignupLink2(MegaClient*, const char*, const char*);
    CommandSendSignupLink2(MegaClient*, const char*, const char*, byte *, byte*, byte*, int ctag);
};

class MEGA_API CommandConfirmSignupLink2 : public Command
{
public:
    bool procresult(Result, JSON&) override;

    CommandConfirmSignupLink2(MegaClient*, const byte*, unsigned);
};

class MEGA_API CommandSetKeyPair : public Command
{
public:
    bool procresult(Result, JSON&) override;

    CommandSetKeyPair(MegaClient*, const byte*, unsigned, const byte*, unsigned);

private:
    std::unique_ptr<byte[]> privkBuffer;
    unsigned len;
};

// set visibility
class MEGA_API CommandRemoveContact : public Command
{
    string email;
    visibility_t v;

public:
    using Completion = std::function<void(error)>;

    bool procresult(Result, JSON&) override;

    CommandRemoveContact(MegaClient*, const char*, visibility_t, Completion completion = nullptr);

private:
    void doComplete(error result);

    Completion mCompletion;
};

// set user attributes with version
class MEGA_API CommandPutMultipleUAVer : public Command
{
    userattr_map attrs;  // attribute values

    std::function<void(Error)> mCompletion;

public:
    CommandPutMultipleUAVer(MegaClient*, const userattr_map *attrs, int,
                            std::function<void(Error)> completion = nullptr);

    bool procresult(Result, JSON&) override;
};

// set user attributes with version
class MEGA_API CommandPutUAVer : public Command
{
    attr_t at;  // attribute type
    string av;  // attribute value

    std::function<void(Error)> mCompletion;
public:
    CommandPutUAVer(MegaClient*, attr_t, const byte*, unsigned, int,
                    std::function<void(Error)> completion = nullptr);

    bool procresult(Result, JSON&) override;
};

// set user attributes
class MEGA_API CommandPutUA : public Command
{
    attr_t at;  // attribute type
    string av;  // attribute value

    std::function<void(Error)> mCompletion;
public:
    CommandPutUA(MegaClient*, attr_t at, const byte*, unsigned, int, handle = UNDEF, int = 0, int64_t = 0,
                 std::function<void(Error)> completion = nullptr);

    bool procresult(Result, JSON&) override;
};

class MEGA_API CommandGetUA : public Command
{
    string uid;
    attr_t at;  // attribute type
    string ph;  // public handle for preview mode, in B64



    std::function<void(byte*, unsigned, attr_t)> mCompletion;

    bool isFromChatPreview() { return !ph.empty(); }

public:

    typedef std::function<void(error)> CompletionErr;
    typedef std::function<void(byte*, unsigned, attr_t)> CompletionBytes;
    typedef std::function<void(TLVstore*, attr_t)> CompletionTLV;

    CommandGetUA(MegaClient*, const char*, attr_t, const char *, int,
        CompletionErr completionErr, CompletionBytes completionBytes, CompletionTLV compltionTLV);

    bool procresult(Result, JSON&) override;

private:
    CompletionErr mCompletionErr;
    CompletionBytes mCompletionBytes;
    CompletionTLV mCompletionTLV;
};

#ifdef DEBUG
class MEGA_API CommandDelUA : public Command
{
    string an;

public:
    CommandDelUA(MegaClient*, const char*);

    bool procresult(Result, JSON&) override;
};

class MEGA_API CommandSendDevCommand : public Command
{
public:
    bool procresult(Result, JSON&) override;

    CommandSendDevCommand(MegaClient*,
                          const char* command,
                          const char* email = NULL,
                          long long = 0,
                          int = 0,
                          int = 0,
                          const char* = nullptr);
};
#endif

class MEGA_API CommandGetUserEmail : public Command
{
public:
    bool procresult(Result, JSON&) override;

    CommandGetUserEmail(MegaClient*, const char *uid);
};

// reload nodes/shares/contacts
class MEGA_API CommandFetchNodes : public Command
{
    bool mLoadSyncs = false;

    const char* getJSON(MegaClient* client) override;

public:
    bool procresult(Result, JSON&) override;
    bool parsingFinished();

    CommandFetchNodes(MegaClient*,
                      int tag,
                      bool nocache,
                      bool loadSyncs,
                      const NodeHandle partialFetchRoot = NodeHandle{});
    ~CommandFetchNodes();

protected:
    handle mPreviousHandleForAlert = UNDEF;
    NodeManager::MissingParentNodes mMissingParentNodes;

    // Field to temporarily save the received scsn
    handle mScsn;
    // sequence-tag, saved temporary while processing the response (it's received before nodes)
    string mSt;

    std::unique_lock<mutex> mNodeTreeIsChanging;
    bool mFirstChunkProcessed = false;
};

// update own node keys
class MEGA_API CommandNodeKeyUpdate : public Command
{
public:
    CommandNodeKeyUpdate(MegaClient*, handle_vector*);

    bool procresult(Result, JSON&) override { return true; }
};

class MEGA_API CommandKeyCR : public Command
{
    bool procresult(Result, JSON&) override { return true; }
public:
    CommandKeyCR(MegaClient*, sharedNode_vector*, sharedNode_vector*, const char*);
};

class MEGA_API CommandMoveNode : public Command
{
public:
    using Completion = std::function<void(NodeHandle, Error)>;

private:
    NodeHandle h;
    NodeHandle pp;  // previous parent
    NodeHandle np;  // new parent
    bool syncop;
    bool mCanChangeVault;
    syncdel_t syncdel;
    Completion completion;

public:
    bool procresult(Result, JSON&) override;

    CommandMoveNode(MegaClient*, std::shared_ptr<Node>, std::shared_ptr<Node>, syncdel_t, NodeHandle prevParent, Completion&& c, bool canChangeVault = false);
};

class MEGA_API CommandSingleKeyCR : public Command
{
public:
    CommandSingleKeyCR(handle, handle, const byte*, size_t);
    bool procresult(Result, JSON&) override { return true; }
};

class MEGA_API CommandDelNode : public Command
{
    NodeHandle h;
    NodeHandle parent;
    std::function<void(NodeHandle, Error)> mResultFunction;

public:
    bool procresult(Result, JSON&) override;

    CommandDelNode(MegaClient*, NodeHandle, bool keepversions, int tag, std::function<void(NodeHandle, Error)>&&, bool canChangeVault = false);
};

class MEGA_API CommandDelVersions : public Command
{
public:
    bool procresult(Result, JSON&) override;

    CommandDelVersions(MegaClient*);
};

class MEGA_API CommandKillSessions : public Command
{
    handle h;

public:
    bool procresult(Result, JSON&) override;

    CommandKillSessions(MegaClient*, handle);
    CommandKillSessions(MegaClient*);
};

class MEGA_API CommandLogout : public Command
{
    bool incrementedCount = false;
    const char* getJSON(MegaClient* client) override;
public:
    using Completion = std::function<void(error)>;

    bool procresult(Result, JSON&) override;

    CommandLogout(MegaClient* client, Completion completion, bool keepSyncConfigsFile);

private:
    Completion mCompletion;
    bool mKeepSyncConfigsFile;
};

class MEGA_API CommandPubKeyRequest : public Command
{
    User* u;

public:
    bool procresult(Result, JSON&) override;
    void invalidateUser();

    CommandPubKeyRequest(MegaClient*, User*);
};

class MEGA_API CommandDirectRead : public Command
{
    DirectReadNode* drn;

public:
    void cancel() override;
    bool procresult(Result, JSON&) override;

    CommandDirectRead(MegaClient *client, DirectReadNode*);
};

class MEGA_API CommandGetFile : public Command
{
    using Cb = std::function<bool(const Error &/*e*/, m_off_t /*size*/,
    dstime /*timeleft*/, std::string* /*filename*/, std::string* /*fingerprint*/, std::string* /*fileattrstring*/,
    const std::vector<std::string> &/*urls*/, const std::vector<std::string> &/*ips*/)>;
    Cb mCompletion;

    void callFailedCompletion (const Error& e);

    byte filekey[FILENODEKEYLENGTH];
    int mFileKeyType; // as expected by SymmCipher::setKey

public:
    // notice: cancelation will entail that mCompletion will not be called
    void cancel() override;
    bool procresult(Result, JSON&) override;

    CommandGetFile(MegaClient *client, const byte* key, size_t keySize, bool undelete,
                       handle h, bool p, const char *privateauth = nullptr,
                       const char *publicauth = nullptr, const char *chatauth = nullptr,
                       bool singleUrl = false, Cb &&completion = nullptr);
};

class MEGA_API CommandPutFile : public Command
{
    TransferSlot* tslot;

public:
    void cancel() override;
    bool procresult(Result, JSON&) override;

    CommandPutFile(MegaClient *client, TransferSlot*, int);
};

class MEGA_API CommandGetPutUrl : public Command
{
    using Cb = std::function<void(Error, const std::string &/*url*/, const vector<std::string> &/*ips*/)>;
    Cb mCompletion;

public:
    bool procresult(Result, JSON&) override;

    CommandGetPutUrl(m_off_t size, int putmbpscap, bool forceSSL, bool getIP, Cb completion);
};


class MEGA_API CommandAttachFA : public Command
{
    handle h;
    fatype type;

public:
    bool procresult(Result, JSON&) override;

    // use this one for attribute blobs
    CommandAttachFA(MegaClient*, handle, fatype, handle, int);

    // use this one for numeric 64 bit attributes (which must be pre-encrypted with XXTEA)
    // multiple attributes can be added at once, encryptedAttributes format "<N>*<attrib>/<M>*<attrib>"
    // only the fatype specified will be notified back to the app
    CommandAttachFA(MegaClient*, handle, fatype, const std::string& encryptedAttributes, int);
};


class MEGA_API CommandPutNodes : public Command
{
public:
    using Completion = std::function<void(const Error&, targettype_t, vector<NewNode>&, bool targetOverride, int tag)>;

private:
    friend class MegaClient;
    vector<NewNode> nn;
    targettype_t type;
    putsource_t source;
    bool emptyResponse = false;
    NodeHandle targethandle;
    Completion mResultFunction;

    void removePendingDBRecordsAndTempFiles();
    void performAppCallback(Error e, vector<NewNode>&, bool targetOverride = false);

public:

    bool procresult(Result, JSON&) override;

    CommandPutNodes(MegaClient*, NodeHandle, const char*, VersioningOption, vector<NewNode>&&, int, putsource_t, const char *cauth, Completion&&, bool canChangeVault);
};

class MEGA_API CommandSetAttr : public Command
{
public:
    using Completion = std::function<void(NodeHandle, Error)>;

private:
    NodeHandle h;
    // It's defined here to avoid node will be destroyed and Node::mPendingChanges will be missed
    std::shared_ptr<Node> mNode;
    attr_map mAttrMapUpdates;
    error generationError;
    bool mCanChangeVault;

    const char* getJSON(MegaClient* client) override;

    Completion completion;

public:
    bool procresult(Result, JSON&) override;
    // Apply the internal attr_map updates to the provided attrMap
    void applyUpdatesTo(AttrMap& attrMap) const;

    CommandSetAttr(MegaClient*, std::shared_ptr<Node>, attr_map&& attrMapUpdates, Completion&& c, bool canChangeVault);
};

class MEGA_API CommandSetShare : public Command
{
    handle sh;
    accesslevel_t access;
    string msg;
    string personal_representation;
    bool mWritable = false;


    std::function<void(Error, bool writable)> completion;

    bool procuserresult(MegaClient*, JSON&);

public:
    bool procresult(Result, JSON&) override;

    CommandSetShare(MegaClient*, std::shared_ptr<Node>, User*, accesslevel_t, bool, const char*, bool writable, const char*,
        int tag, std::function<void(Error, bool writable)> f);
};

using CommandPendingKeysReadCompletion = std::function<void(Error, std::string, std::shared_ptr<std::map<handle, std::map<handle, std::string>>>)>;
class MEGA_API CommandPendingKeys : public Command
{
public:
    bool procresult(Result, JSON&) override;

    // Read pending keys
    CommandPendingKeys(MegaClient*, CommandPendingKeysReadCompletion);

    // Delete pending keys
    CommandPendingKeys(MegaClient*, std::string, std::function<void(Error)>);

    // Send key
    CommandPendingKeys(MegaClient*, handle user, handle share, byte *key, std::function<void(Error)>);

protected:
    std::function<void(Error)> mCompletion;
    CommandPendingKeysReadCompletion mReadCompletion;
};

class MEGA_API CommandGetUserData : public Command
{
public:
    bool procresult(Result, JSON&) override;

    CommandGetUserData(MegaClient*, int tag, std::function<void(string*, string*, string*, error)>);

protected:
    void parseUserAttribute(JSON& json, std::string& value, std::string &version, bool asciiToBinary = true);
    std::function<void(string*, string*, string*, error)> mCompletion;
};

class MEGA_API CommandGetMiscFlags : public Command
{
public:
    bool procresult(Result, JSON&) override;

    CommandGetMiscFlags(MegaClient*);
};

class MEGA_API CommandABTestActive : public Command
{
public:
    using Completion = std::function<void(error)>;

    bool procresult(Result, JSON&) override;

    CommandABTestActive(MegaClient*, const string& tag, Completion completion);

private:
    Completion mCompletion;
};

class MEGA_API CommandSetPendingContact : public Command
{
    opcactions_t action;
    string temail;  // target email

public:
    using Completion = std::function<void(handle, error, opcactions_t)>;

    bool procresult(Result, JSON&) override;

    CommandSetPendingContact(MegaClient*, const char*, opcactions_t, const char* = NULL, const char* = NULL, handle = UNDEF, Completion completion = nullptr);

private:
    void doComplete(handle handle, error result, opcactions_t actions);

    Completion mCompletion;
};

class MEGA_API CommandUpdatePendingContact : public Command
{
    ipcactions_t action;

public:
    using Completion = std::function<void(error, ipcactions_t)>;

    bool procresult(Result, JSON&) override;

    CommandUpdatePendingContact(MegaClient*, handle, ipcactions_t, Completion completion = nullptr);

private:
    void doComplete(error result, ipcactions_t actions);

    Completion mCompletion;
};

class MEGA_API CommandGetUserQuota : public Command
{
    std::shared_ptr<AccountDetails> details;
    bool mStorage;
    bool mTransfer;
    bool mPro;
    std::function<void(std::shared_ptr<AccountDetails>, Error)> mCompletion;

public:
    bool procresult(Result, JSON&) override;

    CommandGetUserQuota(MegaClient*, std::shared_ptr<AccountDetails>, bool, bool, bool, int, std::function<void(std::shared_ptr<AccountDetails>, Error)> = {});

private:
    bool readSubscriptions(JSON* j);
    bool readPlans(JSON* j);
    void processPlans();
};

class MEGA_API CommandQueryTransferQuota : public Command
{
public:
    bool procresult(Result, JSON&) override;

    CommandQueryTransferQuota(MegaClient*, m_off_t size);
};

class MEGA_API CommandGetUserTransactions : public Command
{
    std::shared_ptr<AccountDetails> details;

public:
    bool procresult(Result, JSON&) override;

    CommandGetUserTransactions(MegaClient*, std::shared_ptr<AccountDetails>);
};

class MEGA_API CommandGetUserPurchases : public Command
{
    std::shared_ptr<AccountDetails> details;

public:
    bool procresult(Result, JSON&) override;

    CommandGetUserPurchases(MegaClient*, std::shared_ptr<AccountDetails>);
};

class MEGA_API CommandGetUserSessions : public Command
{
    std::shared_ptr<AccountDetails> details;

public:
    bool procresult(Result, JSON&) override;

    CommandGetUserSessions(MegaClient*, std::shared_ptr<AccountDetails>);
};

class MEGA_API CommandSetPH : public Command
{
public:
    using CompletionType = std::function<void(Error,
                                              handle /*Node handle*/,
                                              handle /*publicHandle*/,
                                              std::string&& /*mEncryptionKeyForShareKey*/)>;

private:
    handle h;
    m_time_t ets;
    bool mWritable = false;
    bool mDeleting = false;
    std::string mEncryptionKeyForShareKey; // Base64 string
    CompletionType mCompletion;

    void completion(Error, handle nodhandle, handle);

public:
    bool procresult(Result, JSON&) override;

    CommandSetPH(MegaClient*,
                 Node*,
                 int,
                 m_time_t,
                 bool writable,
                 bool megaHosted,
                 int ctag,
                 CompletionType f);
};

class MEGA_API CommandGetPH : public Command
{
    handle ph;
    byte key[FILENODEKEYLENGTH];
    int op; //  (op=0 -> download, op=1 fetch data, op=2 import welcomePDF)
    bool havekey;

public:
    bool procresult(Result, JSON&) override;

    CommandGetPH(MegaClient*, handle, const byte*, int);
};

class MEGA_API CommandPurchaseAddItem : public Command
{
public:
    bool procresult(Result, JSON&) override;

    CommandPurchaseAddItem(MegaClient*, int, handle, unsigned, const char*, unsigned, const char*, handle = UNDEF, int = 0, int64_t = 0);
};

class MEGA_API CommandPurchaseCheckout : public Command
{
public:
    bool procresult(Result, JSON&) override;

    CommandPurchaseCheckout(MegaClient*, int);
};

class MEGA_API CommandEnumerateQuotaItems : public Command
{
    static constexpr unsigned int INVALID_TEST_CATEGORY = 0;
public:
    bool procresult(Result, JSON&) override;

    CommandEnumerateQuotaItems(MegaClient*);
};

class MEGA_API CommandSubmitPurchaseReceipt : public Command
{
public:
    bool procresult(Result, JSON&) override;

    CommandSubmitPurchaseReceipt(MegaClient*, int, const char*, handle = UNDEF, int = 0, int64_t = 0);
};

class MEGA_API CommandCreditCardStore : public Command
{

    /*
        'a':'ccs',  // credit card store
        'cc':<encrypted CC data of the required json format>,
        'last4':<last four digits of the credit card number, plain text>,
        'expm':<expiry month in the form "02">,
        'expy':<expiry year in the form "2017">,
        'hash':<sha256 hash of the card details in hex format>
    */

public:
    bool procresult(Result, JSON&) override;

    CommandCreditCardStore(MegaClient*, const char *, const char *, const char *, const char *, const char *);
};

class MEGA_API CommandCreditCardQuerySubscriptions : public Command
{
public:
    bool procresult(Result, JSON&) override;

    CommandCreditCardQuerySubscriptions(MegaClient*);
};

class MEGA_API CommandCreditCardCancelSubscriptions : public Command
{
public:
    enum class CanContact
    {
        No = 0,
        Yes = 1
    };

    class CancelSubscription
    {
    public:
        CancelSubscription(const char* reason, const char* id, int canContact);

    private:
        friend CommandCreditCardCancelSubscriptions;

        // Can be empty
        std::string mReason;
        // Can be empty which means all subscriptions
        std::string mId;

        CanContact mCanContact{CanContact::No};
    };

    bool procresult(Result, JSON&) override;

    CommandCreditCardCancelSubscriptions(MegaClient*, const CancelSubscription& cancelSubscription);
};

class MEGA_API CommandCopySession : public Command
{
public:
    bool procresult(Result, JSON&) override;

    CommandCopySession(MegaClient*);
};

class MEGA_API CommandGetPaymentMethods : public Command
{
public:
    bool procresult(Result, JSON&) override;

    CommandGetPaymentMethods(MegaClient*);
};

class MEGA_API CommandSendReport : public Command
{
public:
    bool procresult(Result, JSON&) override;

    CommandSendReport(MegaClient*, const char *, const char *, const char *);
};

class MEGA_API CommandSendEvent : public Command
{
public:
    bool procresult(Result, JSON&) override;

    CommandSendEvent(MegaClient*, int, const char *, bool = false, const char * = nullptr);
};

class MEGA_API CommandSupportTicket : public Command
{
public:
    bool procresult(Result, JSON&) override;

    CommandSupportTicket(MegaClient*, const char *message, int type = 1);   // by default, 1:technical_issue
};

class MEGA_API CommandCleanRubbishBin : public Command
{
public:
    bool procresult(Result, JSON&) override;

    CommandCleanRubbishBin(MegaClient*);
};

class MEGA_API CommandGetRecoveryLink : public Command
{
public:
    bool procresult(Result, JSON&) override;

    CommandGetRecoveryLink(MegaClient*, const char *, int, const char* = NULL);
};

class MEGA_API CommandQueryRecoveryLink : public Command
{
public:
    bool procresult(Result, JSON&) override;

    CommandQueryRecoveryLink(MegaClient*, const char*);
};

class MEGA_API CommandGetPrivateKey : public Command
{
public:
    bool procresult(Result, JSON&) override;

    CommandGetPrivateKey(MegaClient*, const char*);
};

class MEGA_API CommandConfirmRecoveryLink : public Command
{
public:
    bool procresult(Result, JSON&) override;

    CommandConfirmRecoveryLink(MegaClient*, const char*, const byte*, int, const byte*, const byte*, const byte*);
};

class MEGA_API CommandConfirmCancelLink : public Command
{
public:
    bool procresult(Result, JSON&) override;

    CommandConfirmCancelLink(MegaClient *, const char *);
};

class MEGA_API CommandResendVerificationEmail : public Command
{
public:
    bool procresult(Result, JSON&) override;

    CommandResendVerificationEmail(MegaClient *);
};

class MEGA_API CommandResetSmsVerifiedPhoneNumber : public Command
{
public:
    bool procresult(Result, JSON&) override;

    CommandResetSmsVerifiedPhoneNumber(MegaClient *);
};

class MEGA_API CommandValidatePassword : public Command
{
public:
    bool procresult(Result, JSON&) override;

    CommandValidatePassword(MegaClient*, const char*, const vector<byte>&);
};

class MEGA_API CommandGetEmailLink : public Command
{
public:
    bool procresult(Result, JSON&) override;

    CommandGetEmailLink(MegaClient*, const char*, int, const char *pin = NULL);
};

class MEGA_API CommandConfirmEmailLink : public Command
{
    string email;
    bool replace;
public:
    bool procresult(Result, JSON&) override;

    CommandConfirmEmailLink(MegaClient*, const char*, const char *, const byte *, bool);
};

class MEGA_API CommandGetVersion : public Command
{
public:
    bool procresult(Result, JSON&) override;

    CommandGetVersion(MegaClient*, const char*);
};

class MEGA_API CommandGetLocalSSLCertificate : public Command
{
public:
    bool procresult(Result, JSON&) override;

    CommandGetLocalSSLCertificate(MegaClient*);
};

#ifdef ENABLE_CHAT
class MEGA_API CommandChatCreate : public Command
{
    userpriv_vector *chatPeers;
    bool mPublicChat;
    string mTitle;
    string mUnifiedKey;
    bool mMeeting;
    ChatOptions mChatOptions;
    std::unique_ptr<ScheduledMeeting> mSchedMeeting;
public:
    bool procresult(Result, JSON&) override;

    CommandChatCreate(MegaClient*, bool group, bool publicchat, const userpriv_vector*, const string_map* ukm = NULL, const char* title = NULL, bool meetingRoom = false, int chatOptions = ChatOptions::kEmpty, const ScheduledMeeting* schedMeeting = nullptr);
};

typedef std::function<void(Error)> CommandSetChatOptionsCompletion;
class MEGA_API CommandSetChatOptions : public Command
{
    handle mChatid;
    int mOption;
    bool mEnabled;
    CommandSetChatOptionsCompletion mCompletion;

public:
    bool procresult(Result, JSON&) override;
    CommandSetChatOptions(MegaClient*, handle, int option, bool enabled, CommandSetChatOptionsCompletion completion);
};

class MEGA_API CommandChatInvite : public Command
{
    handle chatid;
    handle uh;
    privilege_t priv;
    string title;

public:
    bool procresult(Result, JSON&) override;

    CommandChatInvite(MegaClient*, handle, handle uh, privilege_t, const char *unifiedkey = NULL, const char *title = NULL);
};

class MEGA_API CommandChatRemove : public Command
{
    handle chatid;
    handle uh;

public:
    bool procresult(Result, JSON&) override;

    CommandChatRemove(MegaClient*, handle, handle uh);
};

class MEGA_API CommandChatURL : public Command
{
public:
    bool procresult(Result, JSON&) override;

    CommandChatURL(MegaClient*, handle);
};

class MEGA_API CommandChatGrantAccess : public Command
{
    handle chatid;
    handle h;
    handle uh;

public:
    bool procresult(Result, JSON&) override;

    CommandChatGrantAccess(MegaClient*, handle, handle, const char *);
};

class MEGA_API CommandChatRemoveAccess : public Command
{
    handle chatid;
    handle h;
    handle uh;

public:
    bool procresult(Result, JSON&) override;

    CommandChatRemoveAccess(MegaClient*, handle, handle, const char *);
};

class MEGA_API CommandChatUpdatePermissions : public Command
{
    handle chatid;
    handle uh;
    privilege_t priv;

public:
    bool procresult(Result, JSON&) override;

    CommandChatUpdatePermissions(MegaClient*, handle, handle, privilege_t);
};

class MEGA_API CommandChatTruncate : public Command
{
    handle chatid;

public:
    bool procresult(Result, JSON&) override;

    CommandChatTruncate(MegaClient*, handle, handle);
};

class MEGA_API CommandChatSetTitle : public Command
{
    handle chatid;
    string title;

public:
    bool procresult(Result, JSON&) override;

    CommandChatSetTitle(MegaClient*, handle, const char *);
};

class MEGA_API CommandChatPresenceURL : public Command
{

public:
    bool procresult(Result, JSON&) override;

    CommandChatPresenceURL(MegaClient*);
};

class MEGA_API CommandRegisterPushNotification : public Command
{
public:
    bool procresult(Result, JSON&) override;

    CommandRegisterPushNotification(MegaClient*, int, const char*);
};

class MEGA_API CommandArchiveChat : public Command
{
public:
    bool procresult(Result, JSON&) override;

    CommandArchiveChat(MegaClient*, handle chatid, bool archive);

protected:
    handle mChatid;
    bool mArchive;
};

class MEGA_API CommandSetChatRetentionTime : public Command
{
public:
    bool procresult(Result, JSON&) override;

    CommandSetChatRetentionTime(MegaClient*, handle , unsigned);

protected:
    handle mChatid;
};

class MEGA_API CommandRichLink : public Command
{
public:
    bool procresult(Result, JSON&) override;

    CommandRichLink(MegaClient *client, const char *url);
};

class MEGA_API CommandChatLink : public Command
{
public:
    bool procresult(Result, JSON&) override;

    CommandChatLink(MegaClient*, handle chatid, bool del, bool createifmissing);

protected:
    bool mDelete;
};

class MEGA_API CommandChatLinkURL : public Command
{
public:
    bool procresult(Result, JSON&) override;

    CommandChatLinkURL(MegaClient*, handle publichandle);
};

class MEGA_API CommandChatLinkClose : public Command
{
public:
    bool procresult(Result, JSON&) override;

    CommandChatLinkClose(MegaClient*, handle chatid, const char *title);

protected:
    handle mChatid;
    string mTitle;
};

class MEGA_API CommandChatLinkJoin : public Command
{
public:
    bool procresult(Result, JSON&) override;

    CommandChatLinkJoin(MegaClient*, handle publichandle, const char *unifiedkey);
};

#endif

class MEGA_API CommandGetMegaAchievements : public Command
{
    AchievementsDetails* details;
public:
    bool procresult(Result, JSON&) override;

    CommandGetMegaAchievements(MegaClient*, AchievementsDetails *details, bool registered_user = true);
};

class MEGA_API CommandGetWelcomePDF : public Command
{
public:
    bool procresult(Result, JSON&) override;

    CommandGetWelcomePDF(MegaClient*);
};


class MEGA_API CommandMediaCodecs : public Command
{
public:
    typedef void(*Callback)(MegaClient* client, JSON& json, int codecListVersion);
    bool procresult(Result, JSON&) override;

    CommandMediaCodecs(MegaClient*, Callback );

private:
    Callback callback;
};

class MEGA_API CommandContactLinkCreate : public Command
{
public:
    bool procresult(Result, JSON&) override;

    CommandContactLinkCreate(MegaClient*, bool);
};

class MEGA_API CommandContactLinkQuery : public Command
{
public:
    bool procresult(Result, JSON&) override;

    CommandContactLinkQuery(MegaClient*, handle);
};

class MEGA_API CommandContactLinkDelete : public Command
{
public:
    bool procresult(Result, JSON&) override;

    CommandContactLinkDelete(MegaClient*, handle);
};

class MEGA_API CommandKeepMeAlive : public Command
{
public:
    bool procresult(Result, JSON&) override;

    CommandKeepMeAlive(MegaClient*, int, bool = true);
};

class MEGA_API CommandMultiFactorAuthSetup : public Command
{
public:
    bool procresult(Result, JSON&) override;

    CommandMultiFactorAuthSetup(MegaClient*, const char* = NULL);
};

class MEGA_API CommandMultiFactorAuthCheck : public Command
{
public:
    bool procresult(Result, JSON&) override;

    CommandMultiFactorAuthCheck(MegaClient*, const char*);
};

class MEGA_API CommandMultiFactorAuthDisable : public Command
{
public:
    bool procresult(Result, JSON&) override;

    CommandMultiFactorAuthDisable(MegaClient*, const char*);
};

class MEGA_API CommandGetPSA : public Command
{
public:
    bool procresult(Result, JSON&) override;

    CommandGetPSA(bool urlSupport, MegaClient*);
};

class MEGA_API CommandFetchTimeZone : public Command
{
public:
    bool procresult(Result, JSON&) override;

    CommandFetchTimeZone(MegaClient*, const char *timezone, const char *timeoffset);
};

class MEGA_API CommandSetLastAcknowledged: public Command
{
public:
    bool procresult(Result, JSON&) override;

    CommandSetLastAcknowledged(MegaClient*);
};

class MEGA_API CommandSMSVerificationSend : public Command
{
public:
    bool procresult(Result, JSON&) override;

    // don't request if it's definitely not a phone number
    static bool isPhoneNumber(const string& s);

    CommandSMSVerificationSend(MegaClient*, const string& phoneNumber, bool reVerifyingWhitelisted);
};

class MEGA_API CommandSMSVerificationCheck : public Command
{
public:
    bool procresult(Result, JSON&) override;

    // don't request if it's definitely not a verification code
    static bool isVerificationCode(const string& s);

    CommandSMSVerificationCheck(MegaClient*, const string& code);
};

class MEGA_API CommandGetCountryCallingCodes : public Command
{
public:
    bool procresult(Result, JSON&) override;

    explicit
    CommandGetCountryCallingCodes(MegaClient* client);
};

class MEGA_API CommandFolderLinkInfo: public Command
{
    handle ph = UNDEF;
public:
    bool procresult(Result, JSON&) override;

    CommandFolderLinkInfo(MegaClient*, handle);
};

class MEGA_API CommandBackupPut : public Command
{
    std::function<void(Error, handle /*backup id*/)> mCompletion;

public:
    bool procresult(Result, JSON&) override;

    enum SPState
    {
        STATE_NOT_INITIALIZED,
        ACTIVE = 1,             // Working fine (enabled)
        FAILED = 2,             // Failed (permanently disabled)
        TEMPORARY_DISABLED = 3, // Temporarily disabled due to a transient situation (e.g: account blocked). Will be resumed when the condition passes
        DISABLED = 4,           // Disabled by the user
        PAUSE_UP = 5,           // Active but upload transfers paused in the SDK
        PAUSE_DOWN = 6,         // Active but download transfers paused in the SDK
        PAUSE_FULL = 7,         // Active but transfers paused in the SDK
        DELETED = 8,            // Sync needs to be deleted, as required by sync-desired-state received from BackupCenter (WebClient)
    };

    struct BackupInfo
    {
        // if left as UNDEF, you are registering a new Sync/Backup
        handle backupId = UNDEF;
        handle driveId = UNDEF;

        // if registering a new Sync/Backup, these must be set
        // otherwise, leave as is to not send an update for that field.
        BackupType type = BackupType::INVALID;
        string backupName = "";
        NodeHandle nodeHandle; // undef by default
        LocalPath localFolder; // empty
        string deviceId = "";
        SPState state = STATE_NOT_INITIALIZED;
        int subState = -1;
    };

    CommandBackupPut(MegaClient* client, const BackupInfo&, std::function<void(Error, handle /*backup id*/)> completion);
};

class MEGA_API CommandBackupRemove : public Command
{
    handle mBackupId;
    std::function<void(const Error&)> mCompletion;

public:
    bool procresult(Result, JSON&) override;

    CommandBackupRemove(MegaClient* client, handle backupId, std::function<void(Error)> completion);
};

class MEGA_API CommandBackupPutHeartBeat : public Command
{
    std::function<void(Error)> mCompletion;
public:
    bool procresult(Result, JSON&) override;

    enum SPHBStatus
    {
        STATE_NOT_INITIALIZED,
        UPTODATE = 1, // Up to date: local and remote paths are in sync
        SYNCING = 2, // The sync engine is working, transfers are in progress
        PENDING = 3, // The sync engine is working, e.g: scanning local folders
        INACTIVE = 4, // Sync is not active. A state != ACTIVE should have been sent through '''sp'''
        UNKNOWN = 5, // Unknown status
        STALLED = 6, // a folder is scan-blocked, or some contradictory changes occured between local and remote folders, user must pick one
    };

    CommandBackupPutHeartBeat(MegaClient* client, handle backupId, SPHBStatus status, int8_t progress, uint32_t uploads, uint32_t downloads, m_time_t ts, handle lastNode, std::function<void(Error)>);
};

class MEGA_API CommandBackupSyncFetch : public Command
{
public:
    struct Data
    {
        handle backupId = UNDEF;
        BackupType backupType = BackupType::INVALID;
        handle rootNode = UNDEF;
        string localFolder;
        string deviceId;
        int syncState = 0;
        int syncSubstate = 0;
        string extra;
        string backupName;
        string deviceUserAgent;
        uint64_t hbTimestamp = 0;
        int hbStatus = 0;
        int hbProgress = 0;
        int uploads = 0;
        int downloads = 0;
        uint64_t lastActivityTs = 0;
        handle lastSyncedNodeHandle = UNDEF;
    };

    bool procresult(Result, JSON&) override;

    CommandBackupSyncFetch(std::function<void(const Error&, const vector<Data>&)>);

private:
    std::function<void(const Error&, const vector<Data>&)> completion;
};


class MEGA_API CommandGetBanners : public Command
{
public:
    bool procresult(Result, JSON&) override;

    CommandGetBanners(MegaClient*);
};

class MEGA_API CommandDismissBanner : public Command
{
public:
    bool procresult(Result, JSON&) override;

    CommandDismissBanner(MegaClient*, int id, m_time_t ts);
};


//
// Sets and Elements
//

class CommandSE : public Command // intermediary class to avoid code duplication
{
public:
    CommandSE() { mV3 = false; }
protected:
    bool procjsonobject(JSON& json, handle& id, m_time_t& ts, handle* u, m_time_t* cts = nullptr,
                        handle* s = nullptr, int64_t* o = nullptr, handle* ph = nullptr,
                        uint8_t* setType = nullptr) const;
    bool procresultid(JSON& json, const Result& r, handle& id, m_time_t& ts, handle* u,
                      m_time_t* cts = nullptr, handle* s = nullptr, int64_t* o = nullptr,
                      handle* ph = nullptr, uint8_t* setType = nullptr) const;
    bool procerrorcode(const Result& r, Error& e) const;
    bool procExtendedError(JSON& json, int64_t& errCode, handle& eid) const;
};

class Set;

class MEGA_API CommandPutSet : public CommandSE
{
public:
    CommandPutSet(MegaClient*, Set&& s, unique_ptr<string> encrAttrs, string&& encrKey,
                  std::function<void(Error, const Set*)> completion);
    bool procresult(Result, JSON&) override;

private:
    unique_ptr<Set> mSet; // use a pointer to avoid defining Set in this header
    std::function<void(Error, const Set*)> mCompletion;
};

class MEGA_API CommandRemoveSet : public CommandSE
{
public:
    CommandRemoveSet(MegaClient*, handle id, std::function<void(Error)> completion);
    bool procresult(Result, JSON&) override;

private:
    handle mSetId = UNDEF;
    std::function<void(Error)> mCompletion;
};

class SetElement;

class MEGA_API CommandFetchSet : public CommandSE
{
public:
    CommandFetchSet(MegaClient*, std::function<void(Error, Set*, map<handle, SetElement>*)> completion);
    bool procresult(Result, JSON&) override;

private:
    std::function<void(Error, Set*, map<handle, SetElement>*)> mCompletion;
};

class MEGA_API CommandPutSetElements : public CommandSE
{
public:
    CommandPutSetElements(MegaClient*, vector<SetElement>&& el, vector<StringPair>&& encrDetails,
                         std::function<void(Error, const vector<const SetElement*>*, const vector<int64_t>*)> completion);
    bool procresult(Result, JSON&) override;

private:
    unique_ptr<vector<SetElement>> mElements; // use a pointer to avoid defining SetElement in this header
    std::function<void(Error, const vector<const SetElement*>*, const vector<int64_t>*)> mCompletion;
};

class MEGA_API CommandPutSetElement : public CommandSE
{
public:
    CommandPutSetElement(MegaClient*, SetElement&& el, unique_ptr<string> encrAttrs, string&& encrKey,
                         std::function<void(Error, const SetElement*)> completion);
    bool procresult(Result, JSON&) override;

private:
    unique_ptr<SetElement> mElement; // use a pointer to avoid defining SetElement in this header
    std::function<void(Error, const SetElement*)> mCompletion;
};

class MEGA_API CommandRemoveSetElements : public CommandSE
{
public:
    CommandRemoveSetElements(MegaClient*, handle sid, vector<handle>&& eids, std::function<void(Error, const vector<int64_t>*)> completion);
    bool procresult(Result, JSON&) override;

private:
    handle mSetId = UNDEF;
    handle_vector mElemIds;
    std::function<void(Error, const vector<int64_t>*)> mCompletion;
};

class MEGA_API CommandRemoveSetElement : public CommandSE
{
public:
    CommandRemoveSetElement(MegaClient*, handle sid, handle eid, std::function<void(Error)> completion);
    bool procresult(Result, JSON&) override;

private:
    handle mSetId = UNDEF;
    handle mElementId = UNDEF;
    std::function<void(Error)> mCompletion;
};

class MEGA_API CommandExportSet : public CommandSE
{
public:
    CommandExportSet(MegaClient*, Set&& s, bool makePublic, std::function<void(Error)> completion);
    bool procresult(Result, JSON&) override;

private:
    unique_ptr<Set> mSet;
    std::function<void(Error)> mCompletion;
};

// -------- end of Sets and Elements


#ifdef ENABLE_CHAT
typedef std::function<void(Error, std::string, handle)> CommandMeetingStartCompletion;
class MEGA_API CommandMeetingStart : public Command
{
    CommandMeetingStartCompletion mCompletion;
public:
    bool procresult(Result, JSON&) override;

    CommandMeetingStart(MegaClient*, const handle chatid, const bool notRinging, CommandMeetingStartCompletion completion);
};

typedef std::function<void(Error, std::string)> CommandMeetingJoinCompletion;
class MEGA_API CommandMeetingJoin : public Command
{
    CommandMeetingJoinCompletion mCompletion;
public:
    bool procresult(Result, JSON&) override;

    CommandMeetingJoin(MegaClient*, handle chatid, handle callid, CommandMeetingJoinCompletion completion);
};

typedef std::function<void(Error)> CommandMeetingEndCompletion;
class MEGA_API CommandMeetingEnd : public Command
{
    CommandMeetingEndCompletion mCompletion;
public:
    bool procresult(Result, JSON&) override;

    CommandMeetingEnd(MegaClient*, handle chatid, handle callid, int reason, CommandMeetingEndCompletion completion);
};

typedef std::function<void(Error)> CommandRingUserCompletion;
class MEGA_API CommandRingUser : public Command
{
    CommandRingUserCompletion mCompletion;
public:
    bool procresult(Result, JSON&) override;

    CommandRingUser(MegaClient*, handle chatid, handle userid, CommandRingUserCompletion completion);
};

typedef std::function<void(Error, const ScheduledMeeting*)> CommandScheduledMeetingAddOrUpdateCompletion;
class MEGA_API CommandScheduledMeetingAddOrUpdate : public Command
{
    std::string mChatTitle;
    std::unique_ptr<ScheduledMeeting> mScheduledMeeting;
    CommandScheduledMeetingAddOrUpdateCompletion mCompletion;

public:
    bool procresult(Result, JSON&) override;
    CommandScheduledMeetingAddOrUpdate(MegaClient *, const ScheduledMeeting*, const char*, CommandScheduledMeetingAddOrUpdateCompletion);
};

typedef std::function<void(Error)> CommandScheduledMeetingRemoveCompletion;
class MEGA_API CommandScheduledMeetingRemove : public Command
{
    handle mChatId;
    handle mSchedId;
    CommandScheduledMeetingRemoveCompletion mCompletion;

public:
    bool procresult(Result, JSON&) override;
    CommandScheduledMeetingRemove(MegaClient *, handle, handle, CommandScheduledMeetingRemoveCompletion completion);
};

typedef std::function<void(Error, const std::vector<std::unique_ptr<ScheduledMeeting>>*)> CommandScheduledMeetingFetchCompletion;
class MEGA_API CommandScheduledMeetingFetch : public Command
{
    handle mChatId;
    CommandScheduledMeetingFetchCompletion mCompletion;

public:
    bool procresult(Result, JSON&) override;
    CommandScheduledMeetingFetch(MegaClient *, handle, handle, CommandScheduledMeetingFetchCompletion completion);
};

typedef std::function<void(Error, const std::vector<std::unique_ptr<ScheduledMeeting>>*)> CommandScheduledMeetingFetchEventsCompletion;
class MEGA_API CommandScheduledMeetingFetchEvents : public Command
{
    handle mChatId;
    bool mByDemand;
    CommandScheduledMeetingFetchEventsCompletion mCompletion;

public:
    bool procresult(Result, JSON&) override;
    CommandScheduledMeetingFetchEvents(MegaClient *, handle, m_time_t, m_time_t, unsigned int, bool, CommandScheduledMeetingFetchEventsCompletion completion);
};
#endif

typedef std::function<void(Error, string_map)> CommandFetchAdsCompletion;
class MEGA_API CommandFetchAds : public Command
{
    CommandFetchAdsCompletion mCompletion;
    std::vector<std::string> mAdUnits;
public:
    bool procresult(Result, JSON&) override;

    CommandFetchAds(MegaClient*, int adFlags, const std::vector<std::string>& adUnits, handle publicHandle, CommandFetchAdsCompletion completion);
};

typedef std::function<void(Error, int)> CommandQueryAdsCompletion;
class MEGA_API CommandQueryAds : public Command
{
    CommandQueryAdsCompletion mCompletion;
public:
    bool procresult(Result, JSON&) override;

    CommandQueryAds(MegaClient*, int adFlags, handle publicHandle, CommandQueryAdsCompletion completion);
};

/* MegaVPN Commands BEGIN */
class MEGA_API CommandGetVpnRegions : public Command
{
public:
    using Cb = std::function<void(const Error& /* API error */,
                                std::vector<std::string>&& /* VPN regions */)>;
    CommandGetVpnRegions(MegaClient*, Cb&& completion = nullptr);
    bool procresult(Result, JSON&) override;
    static void parseregions(JSON& json, std::vector<std::string>*);

private:
    Cb mCompletion;
};

class MEGA_API CommandGetVpnCredentials : public Command
{
public:
    struct CredentialInfo
    {
        int clusterID;
        std::string ipv4;
        std::string ipv6;
        std::string deviceID;
    };
    using MapSlotIDToCredentialInfo = std::map<int /* SlotID */, CredentialInfo>;
    using MapClusterPublicKeys = std::map<int /* ClusterID */, std::string /* Cluster Public Key */ >;
    using Cb = std::function<void(const Error& /* API error */,
                                MapSlotIDToCredentialInfo&& /* Map of SlotID: { ClusterID, IPv4, IPv6, DeviceID } */,
                                MapClusterPublicKeys&& /* Map of ClusterID: Cluster Public Key */,
                                std::vector<std::string>&& /* VPN Regions */)>;
    CommandGetVpnCredentials(MegaClient*, Cb&& completion = nullptr);
    bool procresult(Result, JSON&) override;

private:
    Cb mCompletion;
};

class MEGA_API CommandPutVpnCredential : public Command
{
public:
    using Cb = std::function<void(const Error&  /* API error */,
                                int             /* SlotID */,
                                std::string&&   /* User Public Key */,
                                std::string&&   /* New Credential */)>;
    CommandPutVpnCredential(MegaClient*,
                            std::string&& /* VPN Region */,
                            StringKeyPair&& /* User Key Pair <Private, Public> */,
                            Cb&& completion = nullptr);
    bool procresult(Result, JSON&) override;

private:
    std::string mRegion;
    StringKeyPair mUserKeyPair;
    Cb mCompletion;
};

class MEGA_API CommandDelVpnCredential : public Command
{
public:
    using Cb = std::function<void(const Error& /*e*/)>;
    CommandDelVpnCredential(MegaClient*, int /* SlotID */, Cb&& completion = nullptr);
    bool procresult(Result, JSON&) override;

private:
    Cb mCompletion;
};

class MEGA_API CommandCheckVpnCredential : public Command
{
public:
    using Cb = std::function<void(const Error& /*e*/)>;
    CommandCheckVpnCredential(MegaClient*, std::string&& /* User Public Key */, Cb&& completion = nullptr);
    bool procresult(Result, JSON&) override;

private:
    Cb mCompletion;
};
/* MegaVPN Commands END*/

typedef std::function<void(const Error&, const std::map<std::string, std::string>& creditCardInfo)> CommandFetchCreditCardCompletion;
class MEGA_API CommandFetchCreditCard : public Command
{
public:
    CommandFetchCreditCard(MegaClient* client, CommandFetchCreditCardCompletion completion);
    bool procresult(Result r, JSON&json) override;

private:
    CommandFetchCreditCardCompletion mCompletion;
};

class MEGA_API CommandCreatePasswordManagerBase : public Command
{
public:
    using Completion = std::function<void(Error, std::unique_ptr<NewNode>)>;

    CommandCreatePasswordManagerBase(MegaClient* cl, std::unique_ptr<NewNode>, int ctag, Completion&& cb = nullptr);
    bool procresult(Result, JSON&) override;

private:
    std::unique_ptr<NewNode> mNewNode;
    Completion mCompletion;
};


struct DynamicMessageNotification;

class MEGA_API CommandGetNotifications : public Command
{
public:
    bool procresult(Result, JSON&) override;

    using ResultFunc = std::function<void(const Error& error, vector<DynamicMessageNotification>&& notifications)>;
    CommandGetNotifications(MegaClient*, ResultFunc onResult);

private:
    bool readCallToAction(JSON& json, std::map<std::string, std::string>& action);

    ResultFunc mOnResult;
};

} // namespace

#endif
