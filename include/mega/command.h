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

#include "types.h"
#include "node.h"
#include "account.h"
#include "http.h"
#include "json.h"
#include "textchat.h"

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

    bool loadIpsFromJson(std::vector<string>& ips);
    bool cacheresolvedurls(const std::vector<string>& urls, std::vector<string>&& ips);

public:
    MegaClient* client; // non-owning

    int tag;

    bool persistent;

    // some commands can only succeed if they are in their own batch.  eg. smss, when the account is blocked pending validation
    bool batchSeparately;

    // some commands are guaranteed to work if we query without specifying a SID (eg. gmf)
    bool suppressSID;

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

    enum Outcome {  CmdError,            // The reply was an error, already extracted from the JSON.  The error code may have been 0 (API_OK)
                    //CmdActionpacket,     // The reply was a cmdseq string, and we have processed the corresponding actionpackets
                    CmdArray,            // The reply was an array, and we have already entered it
                    CmdObject,           // the reply was an object, and we have already entered it
                    CmdItem };           // The reply was none of the above - so a string

    struct Result
    {
        Outcome mOutcome = CmdError;
        Error mError = API_OK;
        Result(Outcome o, Error e = API_OK) : mOutcome(o), mError(e) {}

        bool succeeded()
        {
            return mOutcome != CmdError || error(mError) == API_OK;
        }

        bool hasJsonArray()
        {
            // true if there is JSON Array to process (and we have already entered it) (note some commands that respond with cmdseq plus JSON, so this can happen for actionpacket results)
            return mOutcome == CmdArray;
        }

        bool hasJsonObject()
        {
            // true if there is JSON Object to process (and we have already entered it) (note some commands that respond with cmdseq plus JSON, so this can happen for actionpacket results)
            return mOutcome == CmdObject;
        }

        bool hasJsonItem()
        {
            // true if there is JSON to process but it's not an object or array (note some commands that respond with cmdseq plus JSON, so this can happen for actionpacket results)
            return mOutcome == CmdItem;
        }

        Error errorOrOK()
        {
            assert(mOutcome == CmdError);
            return mOutcome == CmdError ? mError : Error(API_EINTERNAL);
        }

        bool wasErrorOrOK()
        {
            return mOutcome == CmdError;
        }

        bool wasError(error e)
        {
            return mOutcome == CmdError && error(mError) == e;
        }

        bool wasStrictlyError()
        {
            return mOutcome == CmdError && error(mError) != API_OK;
        }

    };

    virtual bool procresult(Result) = 0;

    const char* getstring();

    Command();
    virtual ~Command();

    bool checkError(Error &errorDetails, JSON &json);

    MEGA_DEFAULT_COPY_MOVE(Command)
};

// list of new file attributes to write
// file attribute put
struct MEGA_API HttpReqCommandPutFA : public HttpReq, public Command
{
    // For this command, the completion is exectued after the API response.
    // If you supply a completion, that will short-circuit the upload process
    using Cb = std::function<void(Error, const std::string &/*url*/, const vector<std::string> &/*ips*/)>;
    Cb mCompletion;

    NodeOrUploadHandle th;    // if th is UNDEF, just report the handle back to the client app rather than attaching to a node
    fatype type;
    m_off_t progressreported;

    bool procresult(Result) override;

    // progress information
    virtual m_off_t transferred(MegaClient*) override;

    // either supply only size (to just get the URL) or supply only the data for auto-upload (but not both)
    HttpReqCommandPutFA(NodeOrUploadHandle, fatype, bool usehttps, int tag, size_t size_only,
                        std::unique_ptr<string> faData, bool getIP = true, Cb &&completion = nullptr);

private:
    std::unique_ptr<string> data;
};

class MEGA_API CommandGetFA : public Command
{
    int part;

public:
    bool procresult(Result) override;

    CommandGetFA(MegaClient *client, int, handle);
};

class MEGA_API CommandPrelogin : public Command
{
    string email;

public:
    bool procresult(Result) override;

    CommandPrelogin(MegaClient*, const char*);
};

class MEGA_API CommandLogin : public Command
{
    bool checksession;
    int sessionversion;

public:
    bool procresult(Result) override;

    CommandLogin(MegaClient*, const char*, const byte *, int, const byte* = NULL,  int = 0, const char* = NULL);
};

class MEGA_API CommandSetMasterKey : public Command
{
    byte newkey[SymmCipher::KEYLENGTH];
    string salt;

public:
    bool procresult(Result) override;

    CommandSetMasterKey(MegaClient*, const byte*, const byte *, int, const byte* clientrandomvalue = NULL, const char* = NULL, string* = NULL);
};

class MEGA_API CommandCreateEphemeralSession : public Command
{
    byte pw[SymmCipher::KEYLENGTH];

public:
    bool procresult(Result) override;

    CommandCreateEphemeralSession(MegaClient*, const byte*, const byte*, const byte*);
};

class MEGA_API CommandResumeEphemeralSession : public Command
{
    byte pw[SymmCipher::KEYLENGTH];
    handle uh;

public:
    bool procresult(Result) override;

    CommandResumeEphemeralSession(MegaClient*, handle, const byte*, int);
};

class MEGA_API CommandCancelSignup : public Command
{
public:
    bool procresult(Result) override;

    CommandCancelSignup(MegaClient*);
};

class MEGA_API CommandWhyAmIblocked : public Command
{
public:
    bool procresult(Result) override;

    CommandWhyAmIblocked(MegaClient*);
};

class MEGA_API CommandSendSignupLink2 : public Command
{
public:
    bool procresult(Result) override;

    CommandSendSignupLink2(MegaClient*, const char*, const char*);
    CommandSendSignupLink2(MegaClient*, const char*, const char*, byte *, byte*, byte*);
};

class MEGA_API CommandConfirmSignupLink2 : public Command
{
public:
    bool procresult(Result) override;

    CommandConfirmSignupLink2(MegaClient*, const byte*, unsigned);
};

class MEGA_API CommandSetKeyPair : public Command
{
public:
    bool procresult(Result) override;

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

    bool procresult(Result) override;

    CommandRemoveContact(MegaClient*, const char*, visibility_t, Completion completion = nullptr);

private:
    void doComplete(error result);

    Completion mCompletion;
};

// set user attributes with version
class MEGA_API CommandPutMultipleUAVer : public Command
{
    userattr_map attrs;  // attribute values

public:
    CommandPutMultipleUAVer(MegaClient*, const userattr_map *attrs, int);

    bool procresult(Result) override;
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

    bool procresult(Result) override;
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

    bool procresult(Result) override;
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

    bool procresult(Result) override;

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

    bool procresult(Result) override;
};

class MEGA_API CommandSendDevCommand : public Command
{
public:
    bool procresult(Result) override;

    CommandSendDevCommand(MegaClient*, const char* command, const char* email = NULL, long long = 0, int = 0, int = 0);
};
#endif

class MEGA_API CommandGetUserEmail : public Command
{
public:
    bool procresult(Result) override;

    CommandGetUserEmail(MegaClient*, const char *uid);
};

// reload nodes/shares/contacts
class MEGA_API CommandFetchNodes : public Command
{
public:
    bool procresult(Result) override;

    CommandFetchNodes(MegaClient*, int tag, bool nocache);
};

// update own node keys
class MEGA_API CommandNodeKeyUpdate : public Command
{
public:
    CommandNodeKeyUpdate(MegaClient*, handle_vector*);

    bool procresult(Result) override { return true; }
};

class MEGA_API CommandShareKeyUpdate : public Command
{
public:
    CommandShareKeyUpdate(MegaClient*, handle, const char*, const byte*, int);
    CommandShareKeyUpdate(MegaClient*, handle_vector*);

    bool procresult(Result) override { return true; }
};

class MEGA_API CommandKeyCR : public Command
{
    bool procresult(Result) override { return true; }
public:
    CommandKeyCR(MegaClient*, node_vector*, node_vector*, const char*);
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
    syncdel_t syncdel;
    Completion completion;

public:
    bool procresult(Result) override;

    CommandMoveNode(MegaClient*, Node*, Node*, syncdel_t, NodeHandle prevParent, Completion&& c);
};

class MEGA_API CommandSingleKeyCR : public Command
{
public:
    CommandSingleKeyCR(handle, handle, const byte*, size_t);
    bool procresult(Result) override { return true; }
};

class MEGA_API CommandDelNode : public Command
{
    NodeHandle h;
    NodeHandle parent;
    std::function<void(NodeHandle, Error)> mResultFunction;

public:
    bool procresult(Result) override;

    CommandDelNode(MegaClient*, NodeHandle, bool keepversions, int tag, std::function<void(NodeHandle, Error)>&&);
};

class MEGA_API CommandDelVersions : public Command
{
public:
    bool procresult(Result) override;

    CommandDelVersions(MegaClient*);
};

class MEGA_API CommandKillSessions : public Command
{
    handle h;

public:
    bool procresult(Result) override;

    CommandKillSessions(MegaClient*, handle);
    CommandKillSessions(MegaClient*);
};

class MEGA_API CommandLogout : public Command
{
public:
    using Completion = std::function<void(error)>;

    bool procresult(Result) override;

    CommandLogout(MegaClient* client, Completion completion, bool keepSyncConfigsFile);

private:
    Completion mCompletion;
    bool mKeepSyncConfigsFile;
};

class MEGA_API CommandPubKeyRequest : public Command
{
    User* u;

public:
    bool procresult(Result) override;
    void invalidateUser();

    CommandPubKeyRequest(MegaClient*, User*);
};

class MEGA_API CommandDirectRead : public Command
{
    DirectReadNode* drn;

public:
    void cancel() override;
    bool procresult(Result) override;

    CommandDirectRead(MegaClient *client, DirectReadNode*);
};

class MEGA_API CommandGetFile : public Command
{
    using Cb = std::function<bool(const Error &/*e*/, m_off_t /*size*/, m_time_t /*ts*/, m_time_t /*tm*/,
    dstime /*timeleft*/, std::string* /*filename*/, std::string* /*fingerprint*/, std::string* /*fileattrstring*/,
    const std::vector<std::string> &/*urls*/, const std::vector<std::string> &/*ips*/)>;
    Cb mCompletion;

    void callFailedCompletion (const Error& e);

    byte filekey[FILENODEKEYLENGTH];
    int mFileKeyType; // as expected by SymmCipher::setKey

public:
    // notice: cancelation will entail that mCompletion will not be called
    void cancel() override;
    bool procresult(Result) override;

    CommandGetFile(MegaClient *client, const byte* key, size_t keySize,
                       handle h, bool p, const char *privateauth = nullptr,
                       const char *publicauth = nullptr, const char *chatauth = nullptr,
                       bool singleUrl = false, Cb &&completion = nullptr);
};

class MEGA_API CommandPutFile : public Command
{
    TransferSlot* tslot;

public:
    void cancel() override;
    bool procresult(Result) override;

    CommandPutFile(MegaClient *client, TransferSlot*, int);
};

class MEGA_API CommandGetPutUrl : public Command
{
    using Cb = std::function<void(Error, const std::string &/*url*/, const vector<std::string> &/*ips*/)>;
    Cb mCompletion;

    string* result;

public:
    bool procresult(Result) override;

    CommandGetPutUrl(m_off_t size, int putmbpscap, bool forceSSL, bool getIP, Cb completion);
};


class MEGA_API CommandAttachFA : public Command
{
    handle h;
    fatype type;

public:
    bool procresult(Result) override;

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
    using Completion = std::function<void(const Error&, targettype_t, vector<NewNode>&, bool targetOverride)>;

private:
    friend class MegaClient;
    vector<NewNode> nn;
    targettype_t type;
    putsource_t source;
    bool emptyResponse = false;
    NodeHandle targethandle;
    Completion mResultFunction;

    void removePendingDBRecordsAndTempFiles();

public:

    bool procresult(Result) override;

    CommandPutNodes(MegaClient*, NodeHandle, const char*, VersioningOption, vector<NewNode>&&, int, putsource_t, const char *cauth, Completion&&);
};

class MEGA_API CommandSetAttr : public Command
{
public:
    using Completion = std::function<void(NodeHandle, Error)>;

private:
    NodeHandle h;
    string pa;
    bool syncop;

    Completion completion;
public:
    bool procresult(Result) override;

    CommandSetAttr(MegaClient*, Node*, SymmCipher*, const char*, Completion&& c);
};

class MEGA_API CommandSetShare : public Command
{
    handle sh;
    User* user;
    accesslevel_t access;
    string msg;
    string personal_representation;
    bool mWritable = false;


    std::function<void(Error, bool writable)> completion;

    bool procuserresult(MegaClient*);

public:
    bool procresult(Result) override;

    CommandSetShare(MegaClient*, Node*, User*, accesslevel_t, bool, const char*, bool writable, const char*,
        int tag, std::function<void(Error, bool writable)> f);
};

class MEGA_API CommandGetUserData : public Command
{
public:
    bool procresult(Result) override;

    CommandGetUserData(MegaClient*, int tag, std::function<void(string*, string*, string*, error)>);

protected:
    void parseUserAttribute(std::string& value, std::string &version, bool asciiToBinary = true);
    std::function<void(string*, string*, string*, error)> mCompletion;
};

class MEGA_API CommandGetMiscFlags : public Command
{
public:
    bool procresult(Result) override;

    CommandGetMiscFlags(MegaClient*);
};

class MEGA_API CommandSetPendingContact : public Command
{
    opcactions_t action;
    string temail;  // target email

public:
    using Completion = std::function<void(handle, error, opcactions_t)>;

    bool procresult(Result) override;

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

    bool procresult(Result) override;

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

public:
    bool procresult(Result) override;

    CommandGetUserQuota(MegaClient*, std::shared_ptr<AccountDetails>, bool, bool, bool, int source);
};

class MEGA_API CommandQueryTransferQuota : public Command
{
public:
    bool procresult(Result) override;

    CommandQueryTransferQuota(MegaClient*, m_off_t size);
};

class MEGA_API CommandGetUserTransactions : public Command
{
    std::shared_ptr<AccountDetails> details;

public:
    bool procresult(Result) override;

    CommandGetUserTransactions(MegaClient*, std::shared_ptr<AccountDetails>);
};

class MEGA_API CommandGetUserPurchases : public Command
{
    std::shared_ptr<AccountDetails> details;

public:
    bool procresult(Result) override;

    CommandGetUserPurchases(MegaClient*, std::shared_ptr<AccountDetails>);
};

class MEGA_API CommandGetUserSessions : public Command
{
    std::shared_ptr<AccountDetails> details;

public:
    bool procresult(Result) override;

    CommandGetUserSessions(MegaClient*, std::shared_ptr<AccountDetails>);
};

class MEGA_API CommandSetPH : public Command
{
    handle h;
    m_time_t ets;
    bool mWritable = false;
    std::function<void(Error, handle, handle)> completion;

public:
    bool procresult(Result) override;

    CommandSetPH(MegaClient*, Node*, int, m_time_t, bool writable, bool megaHosted,
        int ctag, std::function<void(Error, handle, handle)> f);
};

class MEGA_API CommandGetPH : public Command
{
    handle ph;
    byte key[FILENODEKEYLENGTH];
    int op; //  (op=0 -> download, op=1 fetch data, op=2 import welcomePDF)
    bool havekey;

public:
    bool procresult(Result) override;

    CommandGetPH(MegaClient*, handle, const byte*, int);
};

class MEGA_API CommandPurchaseAddItem : public Command
{
public:
    bool procresult(Result) override;

    CommandPurchaseAddItem(MegaClient*, int, handle, unsigned, const char*, unsigned, const char*, handle = UNDEF, int = 0, int64_t = 0);
};

class MEGA_API CommandPurchaseCheckout : public Command
{
public:
    bool procresult(Result) override;

    CommandPurchaseCheckout(MegaClient*, int);
};

class MEGA_API CommandEnumerateQuotaItems : public Command
{
public:
    bool procresult(Result) override;

    CommandEnumerateQuotaItems(MegaClient*);
};

class MEGA_API CommandReportEvent : public Command
{
public:
    bool procresult(Result) override;

    CommandReportEvent(MegaClient*, const char*, const char*);
};

class MEGA_API CommandSubmitPurchaseReceipt : public Command
{
public:
    bool procresult(Result) override;

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
    bool procresult(Result) override;

    CommandCreditCardStore(MegaClient*, const char *, const char *, const char *, const char *, const char *);
};

class MEGA_API CommandCreditCardQuerySubscriptions : public Command
{
public:
    bool procresult(Result) override;

    CommandCreditCardQuerySubscriptions(MegaClient*);
};

class MEGA_API CommandCreditCardCancelSubscriptions : public Command
{
public:
    bool procresult(Result) override;

    CommandCreditCardCancelSubscriptions(MegaClient*, const char* = NULL);
};

class MEGA_API CommandCopySession : public Command
{
public:
    bool procresult(Result) override;

    CommandCopySession(MegaClient*);
};

class MEGA_API CommandGetPaymentMethods : public Command
{
public:
    bool procresult(Result) override;

    CommandGetPaymentMethods(MegaClient*);
};

class MEGA_API CommandUserFeedbackStore : public Command
{
public:
    bool procresult(Result) override;

    CommandUserFeedbackStore(MegaClient*, const char *, const char *, const char *);
};

class MEGA_API CommandSendEvent : public Command
{
public:
    bool procresult(Result) override;

    CommandSendEvent(MegaClient*, int, const char *);
};

class MEGA_API CommandSupportTicket : public Command
{
public:
    bool procresult(Result) override;

    CommandSupportTicket(MegaClient*, const char *message, int type = 1);   // by default, 1:technical_issue
};

class MEGA_API CommandCleanRubbishBin : public Command
{
public:
    bool procresult(Result) override;

    CommandCleanRubbishBin(MegaClient*);
};

class MEGA_API CommandGetRecoveryLink : public Command
{
public:
    bool procresult(Result) override;

    CommandGetRecoveryLink(MegaClient*, const char *, int, const char* = NULL);
};

class MEGA_API CommandQueryRecoveryLink : public Command
{
public:
    bool procresult(Result) override;

    CommandQueryRecoveryLink(MegaClient*, const char*);
};

class MEGA_API CommandGetPrivateKey : public Command
{
public:
    bool procresult(Result) override;

    CommandGetPrivateKey(MegaClient*, const char*);
};

class MEGA_API CommandConfirmRecoveryLink : public Command
{
public:
    bool procresult(Result) override;

    CommandConfirmRecoveryLink(MegaClient*, const char*, const byte*, int, const byte*, const byte*, const byte*);
};

class MEGA_API CommandConfirmCancelLink : public Command
{
public:
    bool procresult(Result) override;

    CommandConfirmCancelLink(MegaClient *, const char *);
};

class MEGA_API CommandResendVerificationEmail : public Command
{
public:
    bool procresult(Result) override;

    CommandResendVerificationEmail(MegaClient *);
};

class MEGA_API CommandResetSmsVerifiedPhoneNumber : public Command
{
public:
    bool procresult(Result) override;

    CommandResetSmsVerifiedPhoneNumber(MegaClient *);
};

class MEGA_API CommandValidatePassword : public Command
{
public:
    bool procresult(Result) override;

    CommandValidatePassword(MegaClient*, const char*, uint64_t);
};

class MEGA_API CommandGetEmailLink : public Command
{
public:
    bool procresult(Result) override;

    CommandGetEmailLink(MegaClient*, const char*, int, const char *pin = NULL);
};

class MEGA_API CommandConfirmEmailLink : public Command
{
    string email;
    bool replace;
public:
    bool procresult(Result) override;

    CommandConfirmEmailLink(MegaClient*, const char*, const char *, const byte *, bool);
};

class MEGA_API CommandGetVersion : public Command
{
public:
    bool procresult(Result) override;

    CommandGetVersion(MegaClient*, const char*);
};

class MEGA_API CommandGetLocalSSLCertificate : public Command
{
public:
    bool procresult(Result) override;

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
public:
    bool procresult(Result) override;

    CommandChatCreate(MegaClient*, bool group, bool publicchat, const userpriv_vector*, const string_map* ukm = NULL, const char* title = NULL, bool meetingRoom = false, int chatOptions = ChatOptions::kEmpty);
};

typedef std::function<void(Error)> CommandSetChatOptionsCompletion;
class MEGA_API CommandSetChatOptions : public Command
{
    handle mChatid;
    int mOption;
    bool mEnabled;
    CommandSetChatOptionsCompletion mCompletion;

public:
    bool procresult(Result) override;
    CommandSetChatOptions(MegaClient*, handle, int option, bool enabled, CommandSetChatOptionsCompletion completion);
};

class MEGA_API CommandChatInvite : public Command
{
    handle chatid;
    handle uh;
    privilege_t priv;
    string title;

public:
    bool procresult(Result) override;

    CommandChatInvite(MegaClient*, handle, handle uh, privilege_t, const char *unifiedkey = NULL, const char *title = NULL);
};

class MEGA_API CommandChatRemove : public Command
{
    handle chatid;
    handle uh;

public:
    bool procresult(Result) override;

    CommandChatRemove(MegaClient*, handle, handle uh);
};

class MEGA_API CommandChatURL : public Command
{
public:
    bool procresult(Result) override;

    CommandChatURL(MegaClient*, handle);
};

class MEGA_API CommandChatGrantAccess : public Command
{
    handle chatid;
    handle h;
    handle uh;

public:
    bool procresult(Result) override;

    CommandChatGrantAccess(MegaClient*, handle, handle, const char *);
};

class MEGA_API CommandChatRemoveAccess : public Command
{
    handle chatid;
    handle h;
    handle uh;

public:
    bool procresult(Result) override;

    CommandChatRemoveAccess(MegaClient*, handle, handle, const char *);
};

class MEGA_API CommandChatUpdatePermissions : public Command
{
    handle chatid;
    handle uh;
    privilege_t priv;

public:
    bool procresult(Result) override;

    CommandChatUpdatePermissions(MegaClient*, handle, handle, privilege_t);
};

class MEGA_API CommandChatTruncate : public Command
{
    handle chatid;

public:
    bool procresult(Result) override;

    CommandChatTruncate(MegaClient*, handle, handle);
};

class MEGA_API CommandChatSetTitle : public Command
{
    handle chatid;
    string title;

public:
    bool procresult(Result) override;

    CommandChatSetTitle(MegaClient*, handle, const char *);
};

class MEGA_API CommandChatPresenceURL : public Command
{

public:
    bool procresult(Result) override;

    CommandChatPresenceURL(MegaClient*);
};

class MEGA_API CommandRegisterPushNotification : public Command
{
public:
    bool procresult(Result) override;

    CommandRegisterPushNotification(MegaClient*, int, const char*);
};

class MEGA_API CommandArchiveChat : public Command
{
public:
    bool procresult(Result) override;

    CommandArchiveChat(MegaClient*, handle chatid, bool archive);

protected:
    handle mChatid;
    bool mArchive;
};

class MEGA_API CommandSetChatRetentionTime : public Command
{
public:
    bool procresult(Result) override;

    CommandSetChatRetentionTime(MegaClient*, handle , unsigned);

protected:
    handle mChatid;
};

class MEGA_API CommandRichLink : public Command
{
public:
    bool procresult(Result) override;

    CommandRichLink(MegaClient *client, const char *url);
};

class MEGA_API CommandChatLink : public Command
{
public:
    bool procresult(Result) override;

    CommandChatLink(MegaClient*, handle chatid, bool del, bool createifmissing);

protected:
    bool mDelete;
};

class MEGA_API CommandChatLinkURL : public Command
{
public:
    bool procresult(Result) override;

    CommandChatLinkURL(MegaClient*, handle publichandle);
};

class MEGA_API CommandChatLinkClose : public Command
{
public:
    bool procresult(Result) override;

    CommandChatLinkClose(MegaClient*, handle chatid, const char *title);

protected:
    handle mChatid;
    string mTitle;
};

class MEGA_API CommandChatLinkJoin : public Command
{
public:
    bool procresult(Result) override;

    CommandChatLinkJoin(MegaClient*, handle publichandle, const char *unifiedkey);
};

#endif

class MEGA_API CommandGetMegaAchievements : public Command
{
    AchievementsDetails* details;
public:
    bool procresult(Result) override;

    CommandGetMegaAchievements(MegaClient*, AchievementsDetails *details, bool registered_user = true);
};

class MEGA_API CommandGetWelcomePDF : public Command
{
public:
    bool procresult(Result) override;

    CommandGetWelcomePDF(MegaClient*);
};


class MEGA_API CommandMediaCodecs : public Command
{
public:
    typedef void(*Callback)(MegaClient* client, int codecListVersion);
    bool procresult(Result) override;

    CommandMediaCodecs(MegaClient*, Callback );

private:
    Callback callback;
};

class MEGA_API CommandContactLinkCreate : public Command
{
public:
    bool procresult(Result) override;

    CommandContactLinkCreate(MegaClient*, bool);
};

class MEGA_API CommandContactLinkQuery : public Command
{
public:
    bool procresult(Result) override;

    CommandContactLinkQuery(MegaClient*, handle);
};

class MEGA_API CommandContactLinkDelete : public Command
{
public:
    bool procresult(Result) override;

    CommandContactLinkDelete(MegaClient*, handle);
};

class MEGA_API CommandKeepMeAlive : public Command
{
public:
    bool procresult(Result) override;

    CommandKeepMeAlive(MegaClient*, int, bool = true);
};

class MEGA_API CommandMultiFactorAuthSetup : public Command
{
public:
    bool procresult(Result) override;

    CommandMultiFactorAuthSetup(MegaClient*, const char* = NULL);
};

class MEGA_API CommandMultiFactorAuthCheck : public Command
{
public:
    bool procresult(Result) override;

    CommandMultiFactorAuthCheck(MegaClient*, const char*);
};

class MEGA_API CommandMultiFactorAuthDisable : public Command
{
public:
    bool procresult(Result) override;

    CommandMultiFactorAuthDisable(MegaClient*, const char*);
};

class MEGA_API CommandGetPSA : public Command
{
public:
    bool procresult(Result) override;

    CommandGetPSA(bool urlSupport, MegaClient*);
};

class MEGA_API CommandFetchTimeZone : public Command
{
public:
    bool procresult(Result) override;

    CommandFetchTimeZone(MegaClient*, const char *timezone, const char *timeoffset);
};

class MEGA_API CommandSetLastAcknowledged: public Command
{
public:
    bool procresult(Result) override;

    CommandSetLastAcknowledged(MegaClient*);
};

class MEGA_API CommandSMSVerificationSend : public Command
{
public:
    bool procresult(Result) override;

    // don't request if it's definitely not a phone number
    static bool isPhoneNumber(const string& s);

    CommandSMSVerificationSend(MegaClient*, const string& phoneNumber, bool reVerifyingWhitelisted);
};

class MEGA_API CommandSMSVerificationCheck : public Command
{
public:
    bool procresult(Result) override;

    // don't request if it's definitely not a verification code
    static bool isVerificationCode(const string& s);

    CommandSMSVerificationCheck(MegaClient*, const string& code);
};

class MEGA_API CommandGetRegisteredContacts : public Command
{
public:
    bool procresult(Result) override;

    CommandGetRegisteredContacts(MegaClient* client, const map<const char*, const char*>& contacts);
};

class MEGA_API CommandGetCountryCallingCodes : public Command
{
public:
    bool procresult(Result) override;

    explicit
    CommandGetCountryCallingCodes(MegaClient* client);
};

class MEGA_API CommandFolderLinkInfo: public Command
{
    handle ph = UNDEF;
public:
    bool procresult(Result) override;

    CommandFolderLinkInfo(MegaClient*, handle);
};

class MEGA_API CommandBackupPut : public Command
{
    std::function<void(Error, handle /*backup id*/)> mCompletion;

public:
    bool procresult(Result) override;

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

public:
    bool procresult(Result) override;

    CommandBackupRemove(MegaClient* client, handle backupId);
};

class MEGA_API CommandBackupPutHeartBeat : public Command
{
    std::function<void(Error)> mCompletion;
public:
    bool procresult(Result) override;

    enum SPHBStatus
    {
        STATE_NOT_INITIALIZED,
        UPTODATE = 1, // Up to date: local and remote paths are in sync
        SYNCING = 2, // The sync engine is working, transfers are in progress
        PENDING = 3, // The sync engine is working, e.g: scanning local folders
        INACTIVE = 4, // Sync is not active. A state != ACTIVE should have been sent through '''sp'''
        UNKNOWN = 5, // Unknown status
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
        uint64_t hbTimestamp = 0;
        int hbStatus = 0;
        int hbProgress = 0;
        int uploads = 0;
        int downloads = 0;
        uint64_t lastActivityTs = 0;
        handle lastSyncedNodeHandle = UNDEF;
    };

    bool procresult(Result) override;

    CommandBackupSyncFetch(std::function<void(Error, vector<Data>&)>);

private:
    std::function<void(Error, vector<Data>&)> completion;
};


class MEGA_API CommandGetBanners : public Command
{
public:
    bool procresult(Result) override;

    CommandGetBanners(MegaClient*);
};

class MEGA_API CommandDismissBanner : public Command
{
public:
    bool procresult(Result) override;

    CommandDismissBanner(MegaClient*, int id, m_time_t ts);
};

#ifdef ENABLE_CHAT
typedef std::function<void(Error, std::string, handle)> CommandMeetingStartCompletion;
class MEGA_API CommandMeetingStart : public Command
{
    CommandMeetingStartCompletion mCompletion;
public:
    bool procresult(Result) override;

    CommandMeetingStart(MegaClient*, handle chatid, CommandMeetingStartCompletion completion);
};

typedef std::function<void(Error, std::string)> CommandMeetingJoinCompletion;
class MEGA_API CommandMeetingJoin : public Command
{
    CommandMeetingJoinCompletion mCompletion;
public:
    bool procresult(Result) override;

    CommandMeetingJoin(MegaClient*, handle chatid, handle callid, CommandMeetingJoinCompletion completion);
};

typedef std::function<void(Error)> CommandMeetingEndCompletion;
class MEGA_API CommandMeetingEnd : public Command
{
    CommandMeetingEndCompletion mCompletion;
public:
    bool procresult(Result) override;

    CommandMeetingEnd(MegaClient*, handle chatid, handle callid, int reason, CommandMeetingEndCompletion completion);
};

#endif

} // namespace

#endif
