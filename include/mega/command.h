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

namespace mega {

struct JSON;
struct MegaApp;

// request command component
class MEGA_API Command
{
    static const int MAXDEPTH = 8;

    char levels[MAXDEPTH];

    error result;

protected:
    bool canceled;

    string json;

public:
    MegaClient* client; // non-owning

    int tag;

    char level;
    bool persistent;

    // some commands can only succeed if they are in their own batch.  eg. smss, when the account is blocked pending validation
    bool batchSeparately;

    // true if the command processing has been updated to use the URI v3 system, where successful state updates arrive via actionpackets.
    bool mV3 = false;
    bool mStringIsNotSeqtag = false;
    bool mSeqtagArray = false;

    // some commands are guaranteed to work if we query without specifying a SID (eg. gmf)
    bool suppressSID;

    void cmd(const char*);
    void notself(MegaClient*);
    virtual void cancel(void);

    void arg(const char*, const char*, int = 1);
    void arg(const char*, const byte*, int);
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
    int elements();

    enum Outcome {  CmdNumeric,          // The reply was just a single number, already extracted from the JSON.  mError contains the number
                    CmdActionpacket,     // The reply was a cmdseq string, and we have processed the corresponding actionpackets
                    CmdArray,            // The reply was an array, and we have already entered it
                    CmdObject,           // the reply was an object, and we have already entered it
                    CmdItem };           // The reply was none of the above - so a string

    struct Result
    {
        Outcome mOutcome = CmdNumeric;
        error mError = API_OK;
        Result(Outcome o, error e = API_OK) : mOutcome(o), mError(e) {}

        error errorGeneral()
        {
            // for the general case- command could result in an error, actionpacket, or JSON to process
            return mOutcome == CmdNumeric ? mError : API_OK;
        }

        bool processJSON()
        {
            // true if there is JSON to process (ie, not an error or actionpacket (except putnodes))
            return mOutcome == CmdArray || mOutcome == CmdObject || mOutcome == CmdItem;
        }

        // convenience function for commands that should only return numeric or actionpacket:
        error errorNumericOrActionpacket()
        {
            return mOutcome == CmdNumeric ? mError : (mOutcome == CmdActionpacket ? API_OK : API_EINTERNAL);
        }

        error errorNumeric()
        {
            return mOutcome == CmdNumeric ? mError : API_EINTERNAL;
        }

        bool wasNumericOrActionpacket()
        {
            return mOutcome == CmdNumeric || mOutcome == CmdActionpacket;
        }

        bool wasNumeric()
        {
            return mOutcome == CmdNumeric;
        }

        bool wasActionpacket()
        {
            return mOutcome == CmdActionpacket;
        }

    };


    // procresult() handles pre-V3 or failure responses via cs json directly
    virtual void procresult();

    // v3 commands that succeed have procresultV3 called instead, after the command completes
    // and both the response and the actionpackets are received.
    virtual bool procresultV3(Result);

    // automatically select which version of procresult() is called
    inline bool callProcResult(Outcome outcome, error e = API_OK)
    {
        if (mV3)
        {
            return procresultV3(Command::Result(outcome, e));
        }
        else
        {
            // old style, assumed to always consume the right amount of JSON
            procresult();
            return true;
        }
    }
    
    // Helper function for processing non-success cases in v3
    error numericErrorJsonOrDiscard();

    const char* getstring() const;

    Command(bool isV3 = false, bool stringIsNotSeqtag = false);
    virtual ~Command() = default;

    MEGA_DEFAULT_COPY_MOVE(Command)
};

// list of new file attributes to write
// file attribute put
struct MEGA_API HttpReqCommandPutFA : public HttpReq, public Command
{
    handle th;    // if th is UNDEF, just report the handle back to the client app rather than attaching to a node
    fatype type;
    m_off_t progressreported;

    void procresult() override;

    // progress information
    virtual m_off_t transferred(MegaClient*);

    HttpReqCommandPutFA(MegaClient*, handle, fatype, std::unique_ptr<string> faData, bool);

private:
    std::unique_ptr<string> data;
};

class MEGA_API CommandGetFA : public Command
{
    int part;

public:
    void procresult() override;

    CommandGetFA(MegaClient *client, int, handle);
};

class MEGA_API CommandPrelogin : public Command
{
    string email;

public:
    bool procresultV3(Result) override;

    CommandPrelogin(MegaClient*, const char*);
};

class MEGA_API CommandLogin : public Command
{
    bool checksession;
    int sessionversion;

public:
    bool procresultV3(Result) override;

    CommandLogin(MegaClient*, const char*, const byte *, int, const byte* = NULL,  int = 0, const char* = NULL);
};

class MEGA_API CommandSetMasterKey : public Command
{
    byte newkey[SymmCipher::KEYLENGTH];
    string salt;

public:
    void procresult() override;

    CommandSetMasterKey(MegaClient*, const byte*, const byte *, int, const byte* clientrandomvalue = NULL, const char* = NULL, string* = NULL);
};

class MEGA_API CommandCreateEphemeralSession : public Command
{
    byte pw[SymmCipher::KEYLENGTH];

public:
    void procresult() override;

    CommandCreateEphemeralSession(MegaClient*, const byte*, const byte*, const byte*);
};

class MEGA_API CommandResumeEphemeralSession : public Command
{
    byte pw[SymmCipher::KEYLENGTH];
    handle uh;

public:
    void procresult() override;

    CommandResumeEphemeralSession(MegaClient*, handle, const byte*, int);
};

class MEGA_API CommandCancelSignup : public Command
{
public:
    void procresult() override;

    CommandCancelSignup(MegaClient*);
};

class MEGA_API CommandWhyAmIblocked : public Command
{
public:
    void procresult() override;

    CommandWhyAmIblocked(MegaClient*);
};

class MEGA_API CommandSendSignupLink : public Command
{
public:
    void procresult() override;

    CommandSendSignupLink(MegaClient*, const char*, const char*, byte*);
};

class MEGA_API CommandSendSignupLink2 : public Command
{
public:
    void procresult() override;

    CommandSendSignupLink2(MegaClient*, const char*, const char*);
    CommandSendSignupLink2(MegaClient*, const char*, const char*, byte *, byte*, byte*);
};

class MEGA_API CommandQuerySignupLink : public Command
{
    string confirmcode;

public:
    void procresult() override;

    CommandQuerySignupLink(MegaClient*, const byte*, unsigned);
};

class MEGA_API CommandConfirmSignupLink2 : public Command
{
public:
    void procresult() override;

    CommandConfirmSignupLink2(MegaClient*, const byte*, unsigned);
};

class MEGA_API CommandConfirmSignupLink : public Command
{
public:
    bool procresultV3(Result) override;

    CommandConfirmSignupLink(MegaClient*, const byte*, unsigned, uint64_t);
};

class MEGA_API CommandSetKeyPair : public Command
{
public:
    void procresult() override;

    CommandSetKeyPair(MegaClient*, const byte*, unsigned, const byte*, unsigned);

private:
    std::unique_ptr<byte> privkBuffer;
    unsigned len;
};

// set visibility
class MEGA_API CommandRemoveContact : public Command
{
    string email;
    visibility_t v;

public:
    bool procresultV3(Result) override;

    CommandRemoveContact(MegaClient*, const char*, visibility_t);
};

// set user attributes with version
class MEGA_API CommandPutMultipleUAVer : public Command
{
    userattr_map attrs;  // attribute values

public:
    CommandPutMultipleUAVer(MegaClient*, const userattr_map *attrs, int);

    void procresult() override;
};

// set user attributes with version
class MEGA_API CommandPutUAVer : public Command
{
    attr_t at;  // attribute type
    string av;  // attribute value

public:
    CommandPutUAVer(MegaClient*, attr_t, const byte*, unsigned, int);

    void procresult() override;
};

// set user attributes
class MEGA_API CommandPutUA : public Command
{
    attr_t at;  // attribute type
    string av;  // attribute value

public:
    CommandPutUA(MegaClient*, attr_t at, const byte*, unsigned, int, handle = UNDEF, int = 0, int64_t = 0);

    bool procresultV3(Result) override;
};

class MEGA_API CommandGetUA : public Command
{
    string uid;
    attr_t at;  // attribute type
    string ph;  // public handle for preview mode, in B64

    bool isFromChatPreview() { return !ph.empty(); }

public:
    CommandGetUA(MegaClient*, const char*, attr_t, const char *, int);

    bool procresultV3(Result) override;
};

#ifdef DEBUG
class MEGA_API CommandDelUA : public Command
{
    string an;

public:
    CommandDelUA(MegaClient*, const char*);

    void procresult() override;
};
#endif

class MEGA_API CommandGetUserEmail : public Command
{
public:
    void procresult() override;

    CommandGetUserEmail(MegaClient*, const char *uid);
};

// reload nodes/shares/contacts
class MEGA_API CommandFetchNodes : public Command
{
public:
    void procresult() override;

    CommandFetchNodes(MegaClient*, bool nocache = false);
};

// update own node keys
class MEGA_API CommandNodeKeyUpdate : public Command
{
public:
    CommandNodeKeyUpdate(MegaClient*, handle_vector*);

    bool procresultV3(Result) override { return true; }
};

class MEGA_API CommandShareKeyUpdate : public Command
{
public:
    CommandShareKeyUpdate(MegaClient*, handle, const char*, const byte*, int);
    CommandShareKeyUpdate(MegaClient*, handle_vector*);

    bool procresultV3(Result) override { return true; }
};

class MEGA_API CommandKeyCR : public Command
{
public:
    CommandKeyCR(MegaClient*, node_vector*, node_vector*, const char*);
};

class MEGA_API CommandMoveNode : public Command
{
    handle h;
    handle pp;  // previous parent
    handle np;  // new parent
    bool syncop;
    syncdel_t syncdel;

public:
    bool procresultV3(Result) override;

    CommandMoveNode(MegaClient*, Node*, Node*, syncdel_t, handle = UNDEF);
};

class MEGA_API CommandSingleKeyCR : public Command
{
public:
    CommandSingleKeyCR(handle, handle, const byte*, size_t);
};

class MEGA_API CommandDelNode : public Command
{
    handle h;
    std::function<void(handle, error)> mResultFunction;

public:
    bool procresultV3(Result) override;

    CommandDelNode(MegaClient*, handle, bool keepversions, int tag, std::function<void(handle, error)>);
};

class MEGA_API CommandDelVersions : public Command
{
public:
    bool procresultV3(Result) override;

    CommandDelVersions(MegaClient*);
};

class MEGA_API CommandKillSessions : public Command
{
    handle h;

public:
    void procresult() override;

    CommandKillSessions(MegaClient*, handle);
    CommandKillSessions(MegaClient*);
};

class MEGA_API CommandLogout : public Command
{
public:
    bool procresultV3(Result r) override;

    CommandLogout(MegaClient*);
};

class MEGA_API CommandPubKeyRequest : public Command
{
    User* u;

public:
    void procresult() override;
    void invalidateUser();

    CommandPubKeyRequest(MegaClient*, User*);
};

class MEGA_API CommandDirectRead : public Command
{
    DirectReadNode* drn;

public:
    void cancel() override;
    bool procresultV3(Result) override;

    CommandDirectRead(MegaClient *client, DirectReadNode*);
};

class MEGA_API CommandGetFile : public Command
{
    TransferSlot* tslot;
    handle ph;
    bool priv;
    byte filekey[FILENODEKEYLENGTH];

public:
    void cancel() override;
    bool procresultV3(Result) override;

    CommandGetFile(MegaClient *client, TransferSlot*, const byte*, handle, bool, const char* = NULL, const char* = NULL, const char *chatauth = NULL);
};

class MEGA_API CommandPutFile : public Command
{
    TransferSlot* tslot;

public:
    void cancel() override;
    bool procresultV3(Result) override;

    CommandPutFile(MegaClient *client, TransferSlot*, int);
};

class MEGA_API CommandPutFileBackgroundURL : public Command
{
    string* result;

public:
    bool procresultV3(Result) override;

    CommandPutFileBackgroundURL(m_off_t size, int putmbpscap, int ctag);
};


class MEGA_API CommandAttachFA : public Command
{
    handle h;
    fatype type;

public:
    bool procresultV3(Result) override;

    // use this one for attribute blobs 
    CommandAttachFA(MegaClient*, handle, fatype, handle, int);

    // use this one for numeric 64 bit attributes (which must be pre-encrypted with XXTEA)
    // multiple attributes can be added at once, encryptedAttributes format "<N>*<attrib>/<M>*<attrib>"
    // only the fatype specified will be notified back to the app
    CommandAttachFA(MegaClient*, handle, fatype, const std::string& encryptedAttributes, int);
};


class MEGA_API CommandPutNodes : public Command
{
    friend class MegaClient;
    vector<NewNode> nn;
    targettype_t type;
    putsource_t source;
    bool emptyResponse = false;
    handle targethandle;

    void removePendingDBRecordsAndTempFiles();

public:
    bool procresultV3(Result) override;

    CommandPutNodes(MegaClient*, handle, const char*, vector<NewNode>&&, int, putsource_t = PUTNODES_APP, const char *cauth = NULL);
};

class MEGA_API CommandSetAttr : public Command
{
    handle h;
    string pa;
    bool syncop;

public:
    bool procresultV3(Result) override;

    CommandSetAttr(MegaClient*, Node*, SymmCipher*, const char* = NULL);
};

class MEGA_API CommandSetShare : public Command
{
    handle sh;
    User* user;
    accesslevel_t access;
    string msg;
    string personal_representation;

    bool procuserresult(MegaClient*);

public:
    bool procresultV3(Result) override;

    CommandSetShare(MegaClient*, Node*, User*, accesslevel_t, int, const char*, const char* = NULL);
};

class MEGA_API CommandGetUserData : public Command
{
public:
    void procresult() override;

    CommandGetUserData(MegaClient*);

protected:
    void parseUserAttribute(std::string& value, std::string &version, bool asciiToBinary = true);
};

class MEGA_API CommandGetMiscFlags : public Command
{
public:
    void procresult() override;

    CommandGetMiscFlags(MegaClient*);
};

class MEGA_API CommandSetPendingContact : public Command
{
    opcactions_t action;
    string temail;  // target email

public:
    handle mActionpacketPcrHandle = UNDEF;
    string messageForActionpacket;

    void procresult() override;

    CommandSetPendingContact(MegaClient*, const char*, opcactions_t, const char* = NULL, const char* = NULL, handle = UNDEF);
};

class MEGA_API CommandUpdatePendingContact : public Command
{
    ipcactions_t action;

public:
    bool procresultV3(Result) override;

    CommandUpdatePendingContact(MegaClient*, handle, ipcactions_t);
};

class MEGA_API CommandGetUserQuota : public Command
{
    AccountDetails* details;
    bool mStorage;
    bool mTransfer;
    bool mPro;

public:
    void procresult() override;

    CommandGetUserQuota(MegaClient*, AccountDetails*, bool, bool, bool, int source);
};

class MEGA_API CommandQueryTransferQuota : public Command
{
public:
    void procresult() override;

    CommandQueryTransferQuota(MegaClient*, m_off_t size);
};

class MEGA_API CommandGetUserTransactions : public Command
{
    AccountDetails* details;

public:
    void procresult() override;

    CommandGetUserTransactions(MegaClient*, AccountDetails*);
};

class MEGA_API CommandGetUserPurchases : public Command
{
    AccountDetails* details;

public:
    void procresult() override;

    CommandGetUserPurchases(MegaClient*, AccountDetails*);
};

class MEGA_API CommandGetUserSessions : public Command
{
    AccountDetails* details;

public:
    void procresult() override;

    CommandGetUserSessions(MegaClient*, AccountDetails*);
};

class MEGA_API CommandSetPH : public Command
{
    handle h;
    m_time_t ets;

public:
    bool procresultV3(Result) override;

    CommandSetPH(MegaClient*, Node*, int, m_time_t);
};

class MEGA_API CommandGetPH : public Command
{
    handle ph;
    byte key[FILENODEKEYLENGTH];
    int op;
    bool havekey;

public:
    void procresult() override;

    CommandGetPH(MegaClient*, handle, const byte*, int);
};

class MEGA_API CommandPurchaseAddItem : public Command
{
public:
    void procresult() override;

    CommandPurchaseAddItem(MegaClient*, int, handle, unsigned, const char*, unsigned, const char*, handle = UNDEF, int = 0, int64_t = 0);
};

class MEGA_API CommandPurchaseCheckout : public Command
{
public:
    void procresult() override;

    CommandPurchaseCheckout(MegaClient*, int);
};

class MEGA_API CommandEnumerateQuotaItems : public Command
{
public:
    void procresult() override;

    CommandEnumerateQuotaItems(MegaClient*);
};

class MEGA_API CommandReportEvent : public Command
{
public:
    void procresult() override;

    CommandReportEvent(MegaClient*, const char*, const char*);
};

class MEGA_API CommandSubmitPurchaseReceipt : public Command
{
public:
    void procresult() override;

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
    void procresult() override;

    CommandCreditCardStore(MegaClient*, const char *, const char *, const char *, const char *, const char *);
};

class MEGA_API CommandCreditCardQuerySubscriptions : public Command
{
public:
    void procresult() override;

    CommandCreditCardQuerySubscriptions(MegaClient*);
};

class MEGA_API CommandCreditCardCancelSubscriptions : public Command
{
public:
    void procresult() override;

    CommandCreditCardCancelSubscriptions(MegaClient*, const char* = NULL);
};

class MEGA_API CommandCopySession : public Command
{
public:
    void procresult() override;

    CommandCopySession(MegaClient*);
};

class MEGA_API CommandGetPaymentMethods : public Command
{
public:
    void procresult() override;

    CommandGetPaymentMethods(MegaClient*);
};

class MEGA_API CommandUserFeedbackStore : public Command
{
public:
    void procresult() override;

    CommandUserFeedbackStore(MegaClient*, const char *, const char *, const char *);
};

class MEGA_API CommandSendEvent : public Command
{
public:
    void procresult() override;

    CommandSendEvent(MegaClient*, int, const char *);
};

class MEGA_API CommandSupportTicket : public Command
{
public:
    void procresult() override;

    CommandSupportTicket(MegaClient*, const char *message, int type = 1);   // by default, 1:technical_issue
};

class MEGA_API CommandCleanRubbishBin : public Command
{
public:
    void procresult() override;

    CommandCleanRubbishBin(MegaClient*);
};

class MEGA_API CommandGetRecoveryLink : public Command
{
public:
    void procresult() override;

    CommandGetRecoveryLink(MegaClient*, const char *, int, const char* = NULL);
};

class MEGA_API CommandQueryRecoveryLink : public Command
{
public:
    void procresult() override;

    CommandQueryRecoveryLink(MegaClient*, const char*);
};

class MEGA_API CommandGetPrivateKey : public Command
{
public:
    void procresult() override;

    CommandGetPrivateKey(MegaClient*, const char*);
};

class MEGA_API CommandConfirmRecoveryLink : public Command
{
public:
    void procresult() override;

    CommandConfirmRecoveryLink(MegaClient*, const char*, const byte*, int, const byte*, const byte*, const byte*);
};

class MEGA_API CommandConfirmCancelLink : public Command
{
public:
    void procresult() override;

    CommandConfirmCancelLink(MegaClient *, const char *);
};

class MEGA_API CommandResendVerificationEmail : public Command
{
public:
    void procresult() override;

    CommandResendVerificationEmail(MegaClient *);
};

class MEGA_API CommandValidatePassword : public Command
{
public:
    void procresult() override;

    CommandValidatePassword(MegaClient*, const char*, uint64_t);
};

class MEGA_API CommandGetEmailLink : public Command
{
public:
    bool procresultV3(Result) override;

    CommandGetEmailLink(MegaClient*, const char*, int, const char *pin = NULL);
};

class MEGA_API CommandConfirmEmailLink : public Command
{
    string email;
    bool replace;
public:
    void procresult() override;

    CommandConfirmEmailLink(MegaClient*, const char*, const char *, const byte *, bool);
};

class MEGA_API CommandGetVersion : public Command
{
public:
    void procresult() override;

    CommandGetVersion(MegaClient*, const char*);
};

class MEGA_API CommandGetLocalSSLCertificate : public Command
{
public:
    void procresult() override;

    CommandGetLocalSSLCertificate(MegaClient*);
};

#ifdef ENABLE_CHAT
class MEGA_API CommandChatCreate : public Command
{
    userpriv_vector *chatPeers;
    bool mPublicChat;
    string mTitle;
    string mUnifiedKey;
public:
    void procresult();

    CommandChatCreate(MegaClient*, bool group, bool publicchat, const userpriv_vector*, const string_map *ukm = NULL, const char *title = NULL);
};

class MEGA_API CommandChatInvite : public Command
{
    handle chatid;
    handle uh;
    privilege_t priv;
    string title;

public:
    void procresult();

    CommandChatInvite(MegaClient*, handle, handle uh, privilege_t, const char *unifiedkey = NULL, const char *title = NULL);
};

class MEGA_API CommandChatRemove : public Command
{
    handle chatid;
    handle uh;

public:
    void procresult();

    CommandChatRemove(MegaClient*, handle, handle uh);
};

class MEGA_API CommandChatURL : public Command
{
public:
    void procresult();

    CommandChatURL(MegaClient*, handle);
};

class MEGA_API CommandChatGrantAccess : public Command
{
    handle chatid;
    handle h;
    handle uh;

public:
    void procresult();

    CommandChatGrantAccess(MegaClient*, handle, handle, const char *);
};

class MEGA_API CommandChatRemoveAccess : public Command
{    
    handle chatid;
    handle h;
    handle uh;

public:
    void procresult();

    CommandChatRemoveAccess(MegaClient*, handle, handle, const char *);
};

class MEGA_API CommandChatUpdatePermissions : public Command
{
    handle chatid;
    handle uh;
    privilege_t priv;

public:
    void procresult();

    CommandChatUpdatePermissions(MegaClient*, handle, handle, privilege_t);
};

class MEGA_API CommandChatTruncate : public Command
{
    handle chatid;

public:
    void procresult();

    CommandChatTruncate(MegaClient*, handle, handle);
};

class MEGA_API CommandChatSetTitle : public Command
{
    handle chatid;
    string title;

public:
    void procresult();

    CommandChatSetTitle(MegaClient*, handle, const char *);
};

class MEGA_API CommandChatPresenceURL : public Command
{

public:
    void procresult();

    CommandChatPresenceURL(MegaClient*);
};

class MEGA_API CommandRegisterPushNotification : public Command
{
public:
    void procresult();

    CommandRegisterPushNotification(MegaClient*, int, const char*);
};

class MEGA_API CommandArchiveChat : public Command
{
public:
    void procresult();

    CommandArchiveChat(MegaClient*, handle chatid, bool archive);

protected:
    handle mChatid;
    bool mArchive;
};

class MEGA_API CommandRichLink : public Command
{
public:
    void procresult() override;

    CommandRichLink(MegaClient *client, const char *url);
};

class MEGA_API CommandChatLink : public Command
{
public:
    void procresult() override;

    CommandChatLink(MegaClient*, handle chatid, bool del, bool createifmissing);

protected:
    bool mDelete;
};

class MEGA_API CommandChatLinkURL : public Command
{
public:
    void procresult() override;

    CommandChatLinkURL(MegaClient*, handle publichandle);
};

class MEGA_API CommandChatLinkClose : public Command
{
public:
    void procresult();

    CommandChatLinkClose(MegaClient*, handle chatid, const char *title);

protected:
    handle mChatid;
    string mTitle;
};

class MEGA_API CommandChatLinkJoin : public Command
{
public:
    void procresult();

    CommandChatLinkJoin(MegaClient*, handle publichandle, const char *unifiedkey);
};

#endif

class MEGA_API CommandGetMegaAchievements : public Command
{
    AchievementsDetails* details;
public:
    void procresult() override;

    CommandGetMegaAchievements(MegaClient*, AchievementsDetails *details, bool registered_user = true);
};

class MEGA_API CommandGetWelcomePDF : public Command
{
public:
    void procresult() override;

    CommandGetWelcomePDF(MegaClient*);
};


class MEGA_API CommandMediaCodecs : public Command
{
public:
    typedef void(*Callback)(MegaClient* client, int codecListVersion);
    void procresult() override;

    CommandMediaCodecs(MegaClient*, Callback );

private:
    Callback callback;
};

class MEGA_API CommandContactLinkCreate : public Command
{
public:
    void procresult() override;

    CommandContactLinkCreate(MegaClient*, bool);
};

class MEGA_API CommandContactLinkQuery : public Command
{
public:
    void procresult() override;

    CommandContactLinkQuery(MegaClient*, handle);
};

class MEGA_API CommandContactLinkDelete : public Command
{
public:
    void procresult() override;

    CommandContactLinkDelete(MegaClient*, handle);
};

class MEGA_API CommandKeepMeAlive : public Command
{
public:
    void procresult() override;

    CommandKeepMeAlive(MegaClient*, int, bool = true);
};

class MEGA_API CommandMultiFactorAuthSetup : public Command
{
public:
    void procresult() override;

    CommandMultiFactorAuthSetup(MegaClient*, const char* = NULL);
};

class MEGA_API CommandMultiFactorAuthCheck : public Command
{
public:
    void procresult() override;

    CommandMultiFactorAuthCheck(MegaClient*, const char*);
};

class MEGA_API CommandMultiFactorAuthDisable : public Command
{
public:
    void procresult() override;

    CommandMultiFactorAuthDisable(MegaClient*, const char*);
};

class MEGA_API CommandGetPSA : public Command
{
public:
    void procresult() override;

    CommandGetPSA(MegaClient*);
};

class MEGA_API CommandFetchTimeZone : public Command
{
public:
    void procresult() override;

    CommandFetchTimeZone(MegaClient*, const char *timezone, const char *timeoffset);
};

class MEGA_API CommandSetLastAcknowledged: public Command
{
public:
    void procresult() override;

    CommandSetLastAcknowledged(MegaClient*);
};

class MEGA_API CommandSMSVerificationSend : public Command
{
public:
    void procresult() override;

    // don't request if it's definitely not a phone number
    static bool isPhoneNumber(const string& s);

    CommandSMSVerificationSend(MegaClient*, const string& phoneNumber, bool reVerifyingWhitelisted);
};

class MEGA_API CommandSMSVerificationCheck : public Command
{
public:
    void procresult() override;

    // don't request if it's definitely not a verification code
    static bool isVerificationCode(const string& s);

    CommandSMSVerificationCheck(MegaClient*, const string& code);
};

class MEGA_API CommandGetRegisteredContacts : public Command
{
public:
    // static to be called from unit tests
    static void processResult(MegaApp& app, JSON& json);

    void procresult() override;

    CommandGetRegisteredContacts(MegaClient* client, const map<const char*, const char*>& contacts);
};

class MEGA_API CommandGetCountryCallingCodes : public Command
{
public:
    // static to be called from unit tests
    static void processResult(MegaApp& app, JSON& json);

    void procresult() override;

    explicit
    CommandGetCountryCallingCodes(MegaClient* client);
};

class MEGA_API CommandFolderLinkInfo: public Command
{
    handle ph = UNDEF;
public:
    void procresult() override;

    CommandFolderLinkInfo(MegaClient*, handle);
};

} // namespace

#endif
