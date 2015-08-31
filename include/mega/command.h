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
#include "megaclient.h"
#include "account.h"
#include "http.h"

namespace mega {
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
    MegaClient* client;

    int tag;

    char level;
    bool persistent;

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
    void endobject();
    void element(int);
    void element(handle, int = sizeof(handle));
    void element(const byte*, int);
    void element(const char*);

    void openobject();
    void closeobject();
    int elements();

    virtual void procresult();

    const char* getstring() const;

    Command();
    virtual ~Command() { }
};

// list of new file attributes to write
// file attribute put
struct MEGA_API HttpReqCommandPutFA : public HttpReq, public Command
{
    handle th;
    fatype type;
    string* data;

    void procresult();

    HttpReqCommandPutFA(MegaClient*, handle, fatype, string*);
    ~HttpReqCommandPutFA();
};

class MEGA_API CommandGetFA : public Command
{
    int part;

public:
    void procresult();

    CommandGetFA(int, handle, bool);
};

class MEGA_API CommandLogin : public Command
{
    bool checksession;
    int sessionversion;

public:
    void procresult();

    CommandLogin(MegaClient*, const char*, uint64_t, const byte* = NULL,  int = 0);
};

class MEGA_API CommandSetMasterKey : public Command
{
public:
    void procresult();

    CommandSetMasterKey(MegaClient*, const byte*, const byte*, uint64_t);
};

class MEGA_API CommandCreateEphemeralSession : public Command
{
    byte pw[SymmCipher::KEYLENGTH];

public:
    void procresult();

    CommandCreateEphemeralSession(MegaClient*, const byte*, const byte*, const byte*);
};

class MEGA_API CommandResumeEphemeralSession : public Command
{
    byte pw[SymmCipher::KEYLENGTH];
    handle uh;

public:
    void procresult();

    CommandResumeEphemeralSession(MegaClient*, handle, const byte*, int);
};

class MEGA_API CommandSendSignupLink : public Command
{
public:
    void procresult();

    CommandSendSignupLink(MegaClient*, const char*, const char*, byte*);
};

class MEGA_API CommandQuerySignupLink : public Command
{
    string confirmcode;

public:
    void procresult();

    CommandQuerySignupLink(MegaClient*, const byte*, unsigned);
};

class MEGA_API CommandConfirmSignupLink : public Command
{
public:
    void procresult();

    CommandConfirmSignupLink(MegaClient*, const byte*, unsigned, uint64_t);
};

class MEGA_API CommandSetKeyPair : public Command
{
public:
    void procresult();

    CommandSetKeyPair(MegaClient*, const byte*, unsigned, const byte*, unsigned);
};

// invite contact/set visibility
class MEGA_API CommandUserRequest : public Command
{
public:
    void procresult();

    CommandUserRequest(MegaClient*, const char*, visibility_t);
};

// set user attributes
class MEGA_API CommandPutUA : public Command
{
public:
    CommandPutUA(MegaClient*, const char*, const byte*, unsigned);

    void procresult();
};

class MEGA_API CommandGetUA : public Command
{
    int priv;
    User* user;
    string attributename;

public:
    CommandGetUA(MegaClient*, const char*, const char*, int);

    void procresult();
};

// reload nodes/shares/contacts
class MEGA_API CommandFetchNodes : public Command
{
public:
    void procresult();

    CommandFetchNodes(MegaClient*);
};

// update own node keys
class MEGA_API CommandNodeKeyUpdate : public Command
{
public:
    CommandNodeKeyUpdate(MegaClient*, handle_vector*);
};

class MEGA_API CommandShareKeyUpdate : public Command
{
public:
    CommandShareKeyUpdate(MegaClient*, handle, const char*, const byte*, int);
    CommandShareKeyUpdate(MegaClient*, handle_vector*);
};

class MEGA_API CommandKeyCR : public Command
{
public:
    CommandKeyCR(MegaClient*, node_vector*, node_vector*, const char*);
};

class MEGA_API CommandMoveNode : public Command
{
    handle h;
    handle pp;
    bool syncop;
    syncdel_t syncdel;

public:
    void procresult();

    CommandMoveNode(MegaClient*, Node*, Node*, syncdel_t, handle = UNDEF);
};

class MEGA_API CommandSingleKeyCR : public Command
{
public:
    CommandSingleKeyCR(handle, handle, const byte*, unsigned);
};

class MEGA_API CommandDelNode : public Command
{
    handle h;

public:
    void procresult();

    CommandDelNode(MegaClient*, handle);
};

class MEGA_API CommandKillSessions : public Command
{
    handle h;

public:
    void procresult();

    CommandKillSessions(MegaClient*, handle);
    CommandKillSessions(MegaClient*);
};

class MEGA_API CommandLogout : public Command
{
public:
    void procresult();

    CommandLogout(MegaClient*);
};

class MEGA_API CommandPubKeyRequest : public Command
{
    User* u;

public:
    void procresult();

    CommandPubKeyRequest(MegaClient*, User*);
};

class MEGA_API CommandDirectRead : public Command
{
    DirectReadNode* drn;

public:
    void cancel();
    void procresult();

    CommandDirectRead(DirectReadNode*);
};

class MEGA_API CommandGetFile : public Command
{
    TransferSlot* tslot;
    handle ph;
    byte filekey[FILENODEKEYLENGTH];

public:
    void cancel();
    void procresult();

    CommandGetFile(TransferSlot*, byte*, handle, bool, const char* = NULL);
};

class MEGA_API CommandPutFile : public Command
{
    TransferSlot* tslot;

public:
    void cancel(void);
    void procresult();

    CommandPutFile(TransferSlot*, int);
};

class MEGA_API CommandAttachFA : public Command
{
    handle h;
    fatype type;

public:
    void procresult();

    CommandAttachFA(handle, fatype, handle, int);
};


class MEGA_API CommandPutNodes : public Command
{
    NewNode* nn;
    int nnsize;
    targettype_t type;
    putsource_t source;

public:
    void procresult();

    CommandPutNodes(MegaClient*, handle, const char*, NewNode*, int, int, putsource_t = PUTNODES_APP);
};

class MEGA_API CommandSetAttr : public Command
{
    handle h;
    string pa;
    bool syncop;

public:
    void procresult();

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
    void procresult();

    CommandSetShare(MegaClient*, Node*, User*, accesslevel_t, int, const char*, const char* = NULL);
};

class MEGA_API CommandGetUserData : public Command
{
public:
    void procresult();

    CommandGetUserData(MegaClient*);
};

class MEGA_API CommandSetPendingContact : public Command
{
    opcactions_t action;

public:
    void procresult();

    CommandSetPendingContact(MegaClient*, const char*, opcactions_t, const char* = NULL, const char* = NULL);
};

class MEGA_API CommandUpdatePendingContact : public Command
{
    ipcactions_t action;

public:
    void procresult();

    CommandUpdatePendingContact(MegaClient*, handle, ipcactions_t);
};

class MEGA_API CommandGetUserQuota : public Command
{
    AccountDetails* details;

public:
    void procresult();

    CommandGetUserQuota(MegaClient*, AccountDetails*, bool, bool, bool);
};

class MEGA_API CommandGetUserTransactions : public Command
{
    AccountDetails* details;

public:
    void procresult();

    CommandGetUserTransactions(MegaClient*, AccountDetails*);
};

class MEGA_API CommandGetUserPurchases : public Command
{
    AccountDetails* details;

public:
    void procresult();

    CommandGetUserPurchases(MegaClient*, AccountDetails*);
};

class MEGA_API CommandGetUserSessions : public Command
{
    AccountDetails* details;

public:
    void procresult();

    CommandGetUserSessions(MegaClient*, AccountDetails*);
};

class MEGA_API CommandSetPH : public Command
{
    handle h;

public:
    void procresult();

    CommandSetPH(MegaClient*, Node*, int);
};

class MEGA_API CommandGetPH : public Command
{
    handle ph;
    byte key[FILENODEKEYLENGTH];
    int op;

public:
    void procresult();

    CommandGetPH(MegaClient*, handle, const byte*, int);
};

class MEGA_API CommandPurchaseAddItem : public Command
{
public:
    void procresult();

    CommandPurchaseAddItem(MegaClient*, int, handle, unsigned, const char*, unsigned, const char*, const char*);
};

class MEGA_API CommandPurchaseCheckout : public Command
{
public:
    void procresult();

    CommandPurchaseCheckout(MegaClient*, int);
};

class MEGA_API CommandEnumerateQuotaItems : public Command
{
public:
    void procresult();

    CommandEnumerateQuotaItems(MegaClient*);
};

class MEGA_API CommandReportEvent : public Command
{
public:
    void procresult();

    CommandReportEvent(MegaClient*, const char*, const char*);
};

class MEGA_API CommandLoadBalancing : public Command
{
public:
    string service;

    void procresult();

    CommandLoadBalancing(MegaClient*, const char*);
};

class MEGA_API CommandSubmitPurchaseReceipt : public Command
{
public:
    void procresult();

    CommandSubmitPurchaseReceipt(MegaClient*, int, const char*);
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
    void procresult();

    CommandCreditCardStore(MegaClient*, const char *, const char *, const char *, const char *, const char *);
};

class MEGA_API CommandCreditCardQuerySubscriptions : public Command
{
public:
    void procresult();

    CommandCreditCardQuerySubscriptions(MegaClient*);
};

class MEGA_API CommandCreditCardCancelSubscriptions : public Command
{
public:
    void procresult();

    CommandCreditCardCancelSubscriptions(MegaClient*, const char* = NULL);
};

class MEGA_API CommandCopySession : public Command
{
public:
    void procresult();

    CommandCopySession(MegaClient*);
};

class MEGA_API CommandGetPaymentMethods : public Command
{
public:
    void procresult();

    CommandGetPaymentMethods(MegaClient*);
};

class MEGA_API CommandUserFeedbackStore : public Command
{
public:
    void procresult();

    CommandUserFeedbackStore(MegaClient*, const char *, const char *, const char *);
};

class MEGA_API CommandSendEvent : public Command
{
public:
    void procresult();

    CommandSendEvent(MegaClient*, int, const char *);
};

class MEGA_API CommandCleanRubbishBin : public Command
{
public:
    void procresult();

    CommandCleanRubbishBin(MegaClient*);
};

} // namespace

#endif
