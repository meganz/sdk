/**
 * @file tests/sdk_test.cpp
 * @brief Mega SDK test file
 *
 * (c) 2015 by Mega Limited, Wellsford, New Zealand
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


#include "mega.h"
#include "../include/megaapi.h"
#include "../include/megaapi_impl.h"
#include "gtest/gtest.h"
#include "test.h"

#include <iostream>
#include <fstream>
#include <future>
#include <atomic>

using namespace mega;
using ::testing::Test;

static const string APP_KEY     = "8QxzVRxD";

// IMPORTANT: the main account must be empty (Cloud & Rubbish) before starting the test and it will be purged at exit.
// Both main and auxiliar accounts shouldn't be contacts yet and shouldn't have any pending contact requests.
// Set your login credentials as environment variables: $MEGA_EMAIL and $MEGA_PWD (and $MEGA_EMAIL_AUX / $MEGA_PWD_AUX for shares * contacts)

static const unsigned int pollingT      = 500000;   // (microseconds) to check if response from server is received
static const unsigned int maxTimeout    = 600;      // Maximum time (seconds) to wait for response from server

static const string PUBLICFILE  = "file.txt";
static const string UPFILE      = "file1.txt";
static const string DOWNFILE    = "file2.txt";
static const string EMPTYFILE   = "empty-file.txt";
static const string AVATARSRC   = "logo.png";
static const string AVATARDST   = "deleteme.png";


struct TransferTracker : public ::mega::MegaTransferListener
{
    std::atomic<bool> started = { false };
    std::atomic<bool> finished = { false };
    std::atomic<int> result = { INT_MAX };
    std::promise<int> promiseResult;
    MegaApi *mApi;
    std::shared_ptr<TransferTracker> selfDeleteOnFinalCallback;

    TransferTracker(MegaApi *api): mApi(api)
    {

    }
    void onTransferStart(MegaApi *api, MegaTransfer *transfer) override
    {
        started = true;
    }
    void onTransferFinish(MegaApi* api, MegaTransfer *transfer, MegaError* error) override
    {
        result = error->getErrorCode();
        finished = true;
        promiseResult.set_value(result);
        if (selfDeleteOnFinalCallback)
        {
            // sometimes in tests we need to abandon the listener, because we need to time out (which a normal app would not do)
            // another case is when we deliberately destroy MegaApi or other objects to exercise shutdown cases
            // allowing the listener to destroy on final callback simplifies test object lifetime management
            selfDeleteOnFinalCallback.reset();
        }
    }
    int waitForResult(int seconds = maxTimeout, bool unregisterListenerOnTimeout = true)
    {
        auto f = promiseResult.get_future();
        if (std::future_status::ready != f.wait_for(std::chrono::seconds(seconds)))
        {
            assert(mApi);
            if (unregisterListenerOnTimeout)
            {
                mApi->removeTransferListener(this);
            }
            return -999; // local timeout
        }
        return f.get();
    }
};

struct RequestTracker : public ::mega::MegaRequestListener
{
    std::atomic<bool> started = { false };
    std::atomic<bool> finished = { false };
    std::atomic<int> result = { INT_MAX };
    std::promise<int> promiseResult;
    MegaApi *mApi;

    MegaRequest *request = nullptr;

    RequestTracker(MegaApi *api): mApi(api)
    {

    }

    ~RequestTracker() override
    {
        delete request;
    }

    void onRequestStart(MegaApi* api, MegaRequest *request) override
    {
        started = true;
    }
    void onRequestFinish(MegaApi* api, MegaRequest *request, MegaError* e) override
    {
        result = e->getErrorCode();
        this->request = request->copy();
        finished = true;
        promiseResult.set_value(result);
    }
    int waitForResult(int seconds = maxTimeout, bool unregisterListenerOnTimeout = true)
    {
        auto f = promiseResult.get_future();
        if (std::future_status::ready != f.wait_for(std::chrono::seconds(seconds)))
        {
            assert(mApi);
            if (unregisterListenerOnTimeout)
            {
                mApi->removeRequestListener(this);
            }
            return -999; // local timeout
        }
        return f.get();
    }
};

// Fixture class with common code for most of tests
class SdkTest : public ::testing::Test, public MegaListener, public MegaRequestListener, MegaTransferListener, MegaLogger {

public:

    struct PerApi
    {
        MegaApi* megaApi = nullptr;
        string email;
        string pwd;
        int lastError;
        int lastTransferError;

        // flags to monitor the completion of requests/transfers
        bool requestFlags[MegaRequest::TOTAL_OF_REQUEST_TYPES];
        bool transferFlags[MegaTransfer::TYPE_LOCAL_HTTP_DOWNLOAD];

        std::unique_ptr<MegaContactRequest> cr;
        std::unique_ptr<MegaTimeZoneDetails> tzDetails;
        std::unique_ptr<MegaAccountDetails> accountDetails;
        std::unique_ptr<MegaStringMap> mStringMap;

        // flags to monitor the updates of nodes/users/PCRs due to actionpackets
        bool nodeUpdated;
        bool userUpdated;
        bool contactRequestUpdated;
        bool accountUpdated;

        MegaHandle h;
#ifdef ENABLE_SYNC
        int lastSyncError;
#endif
#ifdef ENABLE_CHAT
        bool chatUpdated;        // flags to monitor the updates of chats due to actionpackets
        map<handle, std::unique_ptr<MegaTextChat>> chats;   //  runtime cache of fetched/updated chats
        MegaHandle chatid;          // last chat added
#endif
    };

    std::vector<PerApi> mApi;
    std::vector<std::unique_ptr<MegaApi>> megaApi;

    // relevant values received in response of requests
    string link;
    MegaNode *publicNode;
    string attributeValue;
    string sid;
    std::unique_ptr<MegaStringListMap> stringListMap;
    std::unique_ptr<MegaStringTable> stringTable;

    m_off_t onTransferUpdate_progress;
    m_off_t onTransferUpdate_filesize;
    unsigned onTranferFinishedCount = 0;

    std::mutex lastEventMutex;
    std::unique_ptr<MegaEvent> lastEvent;

    MegaHandle mBackupId = UNDEF;
    std::vector<std::pair<string, MegaHandle> > mBackupNameToBackupId;
    std::set<MegaHandle> mBackupIds;

protected:
    void SetUp() override;
    void TearDown() override;

    void Cleanup();

    int getApiIndex(MegaApi* api);

    bool checkAlert(int apiIndex, const string& title, const string& path);
    bool checkAlert(int apiIndex, const string& title, handle h, int n);

    void onRequestStart(MegaApi *api, MegaRequest *request) override {}
    void onRequestUpdate(MegaApi*api, MegaRequest *request) override {}
    void onRequestFinish(MegaApi *api, MegaRequest *request, MegaError *e) override;
    void onRequestTemporaryError(MegaApi *api, MegaRequest *request, MegaError* error) override {}
    void onTransferStart(MegaApi *api, MegaTransfer *transfer) override { }
    void onTransferFinish(MegaApi* api, MegaTransfer *transfer, MegaError* e) override;
    void onTransferUpdate(MegaApi *api, MegaTransfer *transfer) override;
    void onTransferTemporaryError(MegaApi *api, MegaTransfer *transfer, MegaError* error) override {}
    void onUsersUpdate(MegaApi* api, MegaUserList *users) override;
    void onNodesUpdate(MegaApi* api, MegaNodeList *nodes) override;
    void onAccountUpdate(MegaApi *api) override;
    void onContactRequestsUpdate(MegaApi* api, MegaContactRequestList* requests) override;
    void onReloadNeeded(MegaApi *api) override {}
#ifdef ENABLE_SYNC
    void onSyncFileStateChanged(MegaApi *api, MegaSync *sync, string* filePath, int newState) override {}
    void onSyncEvent(MegaApi *api, MegaSync *sync,  MegaSyncEvent *event) override {}
    void onSyncStateChanged(MegaApi *api,  MegaSync *sync) override {}
    void onGlobalSyncStateChanged(MegaApi* api) override {}
#endif
#ifdef ENABLE_CHAT
    void onChatsUpdate(MegaApi *api, MegaTextChatList *chats) override;
#endif
    void onEvent(MegaApi* api, MegaEvent *event) override;

public:
    //void login(unsigned int apiIndex, int timeout = maxTimeout);
    //void loginBySessionId(unsigned int apiIndex, const std::string& sessionId, int timeout = maxTimeout);
    void fetchnodes(unsigned int apiIndex, int timeout = maxTimeout);
    void logout(unsigned int apiIndex, int timeout = maxTimeout);
    char* dumpSession();
    void locallogout(int timeout = maxTimeout);
    void resumeSession(const char *session, int timeout = maxTimeout);

    void purgeTree(MegaNode *p, bool depthfirst = true);
    bool waitForResponse(bool *responseReceived, unsigned int timeout = maxTimeout);

    bool synchronousRequest(unsigned apiIndex, int type, std::function<void()> f, unsigned int timeout = maxTimeout);
    bool synchronousTransfer(unsigned apiIndex, int type, std::function<void()> f, unsigned int timeout = maxTimeout);

    // convenience functions - template args just make it easy to code, no need to copy all the exact argument types with listener defaults etc. To add a new one, just copy a line and change the flag and the function called.
    // WARNING: any sort of race can result in the lastError being set from some other command - better to use the listener based ones in the next list below
    template<typename ... Args> int synchronousStartUpload(unsigned apiIndex, Args... args) { synchronousTransfer(apiIndex, MegaTransfer::TYPE_UPLOAD, [this, apiIndex, args...]() { megaApi[apiIndex]->startUpload(args...); }); return mApi[apiIndex].lastError; }
    template<typename ... Args> int synchronousStartDownload(unsigned apiIndex, Args... args) { synchronousTransfer(apiIndex, MegaTransfer::TYPE_DOWNLOAD, [this, apiIndex, args...]() { megaApi[apiIndex]->startDownload(args...); }); return mApi[apiIndex].lastTransferError; }
    template<typename ... Args> int synchronousCatchup(unsigned apiIndex, Args... args) { synchronousRequest(apiIndex, MegaRequest::TYPE_CATCHUP, [this, apiIndex, args...]() { megaApi[apiIndex]->catchup(args...); }); return mApi[apiIndex].lastError; }
    template<typename ... Args> int synchronousCreateAccount(unsigned apiIndex, Args... args) { synchronousRequest(apiIndex, MegaRequest::TYPE_CREATE_ACCOUNT, [this, apiIndex, args...]() { megaApi[apiIndex]->createAccount(args...); }); return mApi[apiIndex].lastError; }
    template<typename ... Args> int synchronousResumeCreateAccount(unsigned apiIndex, Args... args) { synchronousRequest(apiIndex, MegaRequest::TYPE_CREATE_ACCOUNT, [this, apiIndex, args...]() { megaApi[apiIndex]->resumeCreateAccount(args...); }); return mApi[apiIndex].lastError; }
    template<typename ... Args> int synchronousSendSignupLink(unsigned apiIndex, Args... args) { synchronousRequest(apiIndex, MegaRequest::TYPE_SEND_SIGNUP_LINK, [this, apiIndex, args...]() { megaApi[apiIndex]->sendSignupLink(args...); }); return mApi[apiIndex].lastError; }
    template<typename ... Args> int synchronousFastLogin(unsigned apiIndex, Args... args) { synchronousRequest(apiIndex, MegaRequest::TYPE_LOGIN, [this, apiIndex, args...]() { megaApi[apiIndex]->fastLogin(args...); }); return mApi[apiIndex].lastError; }
    template<typename ... Args> int synchronousRemove(unsigned apiIndex, Args... args) { synchronousRequest(apiIndex, MegaRequest::TYPE_REMOVE, [this, apiIndex, args...]() { megaApi[apiIndex]->remove(args...); }); return mApi[apiIndex].lastError; }
    template<typename ... Args> int synchronousInviteContact(unsigned apiIndex, Args... args) { synchronousRequest(apiIndex, MegaRequest::TYPE_INVITE_CONTACT, [this, apiIndex, args...]() { megaApi[apiIndex]->inviteContact(args...); }); return mApi[apiIndex].lastError; }
    template<typename ... Args> int synchronousReplyContactRequest(unsigned apiIndex, Args... args) { synchronousRequest(apiIndex, MegaRequest::TYPE_REPLY_CONTACT_REQUEST, [this, apiIndex, args...]() { megaApi[apiIndex]->replyContactRequest(args...); }); return mApi[apiIndex].lastError; }
    template<typename ... Args> int synchronousRemoveContact(unsigned apiIndex, Args... args) { synchronousRequest(apiIndex, MegaRequest::TYPE_REMOVE_CONTACT, [this, apiIndex, args...]() { megaApi[apiIndex]->removeContact(args...); }); return mApi[apiIndex].lastError; }
    template<typename ... Args> int synchronousShare(unsigned apiIndex, Args... args) { synchronousRequest(apiIndex, MegaRequest::TYPE_SHARE, [this, apiIndex, args...]() { megaApi[apiIndex]->share(args...); }); return mApi[apiIndex].lastError; }
    template<typename ... Args> int synchronousExportNode(unsigned apiIndex, Args... args) { synchronousRequest(apiIndex, MegaRequest::TYPE_EXPORT, [this, apiIndex, args...]() { megaApi[apiIndex]->exportNode(args...); }); return mApi[apiIndex].lastError; }
    template<typename ... Args> int synchronousGetRegisteredContacts(unsigned apiIndex, Args... args) { synchronousRequest(apiIndex, MegaRequest::TYPE_GET_REGISTERED_CONTACTS, [this, apiIndex, args...]() { megaApi[apiIndex]->getRegisteredContacts(args...); }); return mApi[apiIndex].lastError; }
    template<typename ... Args> int synchronousGetCountryCallingCodes(unsigned apiIndex, Args... args) { synchronousRequest(apiIndex, MegaRequest::TYPE_GET_COUNTRY_CALLING_CODES, [this, apiIndex, args...]() { megaApi[apiIndex]->getCountryCallingCodes(args...); }); return mApi[apiIndex].lastError; }
    template<typename ... Args> int synchronousGetUserAvatar(unsigned apiIndex, Args... args) { synchronousRequest(apiIndex, MegaRequest::TYPE_GET_ATTR_USER, [this, apiIndex, args...]() { megaApi[apiIndex]->getUserAvatar(args...); }); return mApi[apiIndex].lastError; }
    template<typename ... Args> int synchronousGetUserAttribute(unsigned apiIndex, Args... args) { synchronousRequest(apiIndex, MegaRequest::TYPE_GET_ATTR_USER, [this, apiIndex, args...]() { megaApi[apiIndex]->getUserAttribute(args...); }); return mApi[apiIndex].lastError; }
    template<typename ... Args> int synchronousSetNodeDuration(unsigned apiIndex, Args... args) { synchronousRequest(apiIndex, MegaRequest::TYPE_SET_ATTR_NODE, [this, apiIndex, args...]() { megaApi[apiIndex]->setNodeDuration(args...); }); return mApi[apiIndex].lastError; }
    template<typename ... Args> int synchronousSetNodeCoordinates(unsigned apiIndex, Args... args) { synchronousRequest(apiIndex, MegaRequest::TYPE_SET_ATTR_NODE, [this, apiIndex, args...]() { megaApi[apiIndex]->setNodeCoordinates(args...); }); return mApi[apiIndex].lastError; }
    template<typename ... Args> int synchronousGetSpecificAccountDetails(unsigned apiIndex, Args... args) { synchronousRequest(apiIndex, MegaRequest::TYPE_ACCOUNT_DETAILS, [this, apiIndex, args...]() { megaApi[apiIndex]->getSpecificAccountDetails(args...); }); return mApi[apiIndex].lastError; }
    template<typename ... Args> int synchronousMediaUploadRequestURL(unsigned apiIndex, Args... args) { synchronousRequest(apiIndex, MegaRequest::TYPE_GET_BACKGROUND_UPLOAD_URL, [this, apiIndex, args...]() { megaApi[apiIndex]->backgroundMediaUploadRequestUploadURL(args...); }); return mApi[apiIndex].lastError; }
    template<typename ... Args> int synchronousFetchTimeZone(unsigned apiIndex, Args... args) { synchronousRequest(apiIndex, MegaRequest::TYPE_FETCH_TIMEZONE, [this, apiIndex, args...]() { megaApi[apiIndex]->fetchTimeZone(args...); }); return mApi[apiIndex].lastError; }
    template<typename ... Args> int synchronousGetMiscFlags(unsigned apiIndex, Args... args) { synchronousRequest(apiIndex, MegaRequest::TYPE_GET_MISC_FLAGS, [this, apiIndex, args...]() { megaApi[apiIndex]->getMiscFlags(args...); }); return mApi[apiIndex].lastError; }
    template<typename ... Args> int synchronousGetUserEmail(unsigned apiIndex, Args... args) { synchronousRequest(apiIndex, MegaRequest::TYPE_GET_USER_EMAIL, [this, apiIndex, args...]() { megaApi[apiIndex]->getUserEmail(args...); }); return mApi[apiIndex].lastError; }
    template<typename ... Args> int synchronousCleanRubbishBin(unsigned apiIndex, Args... args) { synchronousRequest(apiIndex, MegaRequest::TYPE_CLEAN_RUBBISH_BIN, [this, apiIndex, args...]() { megaApi[apiIndex]->cleanRubbishBin(args...); }); return mApi[apiIndex].lastError; }
    template<typename ... Args> int synchronousGetExtendedAccountDetails(unsigned apiIndex, Args... args) { synchronousRequest(apiIndex, MegaRequest::TYPE_ACCOUNT_DETAILS, [this, apiIndex, args...]() { megaApi[apiIndex]->getExtendedAccountDetails(args...); }); return mApi[apiIndex].lastError; }
    template<typename ... Args> int synchronousGetBanners(unsigned apiIndex, Args... args) { synchronousRequest(apiIndex, MegaRequest::TYPE_GET_BANNERS, [this, apiIndex, args...]() { megaApi[apiIndex]->getBanners(args...); }); return mApi[apiIndex].lastError; }
    template<typename ... Args> int synchronousUpdateBackup(unsigned apiIndex, Args... args) { synchronousRequest(apiIndex, MegaRequest::TYPE_BACKUP_PUT, [this, apiIndex, args...]() { megaApi[apiIndex]->updateBackup(args...); }); return mApi[apiIndex].lastError; }
    template<typename ... Args> int synchronousRemoveBackup(unsigned apiIndex, Args... args) { synchronousRequest(apiIndex, MegaRequest::TYPE_BACKUP_REMOVE, [this, apiIndex, args...]() { megaApi[apiIndex]->removeBackup(args...); }); return mApi[apiIndex].lastError; }
    template<typename ... Args> int synchronousSendBackupHeartbeat(unsigned apiIndex, Args... args) { synchronousRequest(apiIndex, MegaRequest::TYPE_BACKUP_PUT_HEART_BEAT, [this, apiIndex, args...]() { megaApi[apiIndex]->sendBackupHeartbeat(args...); }); return mApi[apiIndex].lastError; }
    template<typename ... Args> int synchronousSetMyBackupsFolder(unsigned apiIndex, Args... args) { synchronousRequest(apiIndex, MegaRequest::TYPE_SET_ATTR_USER, [this, apiIndex, args...]() { megaApi[apiIndex]->setMyBackupsFolder(args...); }); return mApi[apiIndex].lastError; }
    template<typename ... Args> int synchronousGetMyBackupsFolder(unsigned apiIndex, Args... args) { synchronousRequest(apiIndex, MegaRequest::TYPE_GET_ATTR_USER, [this, apiIndex, args...]() { megaApi[apiIndex]->getMyBackupsFolder(args...); }); return mApi[apiIndex].lastError; }
    template<typename ... Args> int synchronousBackupFolder(unsigned apiIndex, Args... args) { synchronousRequest(apiIndex, MegaRequest::TYPE_ADD_SYNC, [this, apiIndex, args...]() { megaApi[apiIndex]->backupFolder(args...); }); return mApi[apiIndex].lastError; }
    template<typename ... Args> int synchronousSetDeviceName(unsigned apiIndex, Args... args) { synchronousRequest(apiIndex, MegaRequest::TYPE_SET_ATTR_USER, [this, apiIndex, args...]() { megaApi[apiIndex]->setDeviceName(args...); }); return mApi[apiIndex].lastError; }
    template<typename ... Args> int synchronousGetDeviceName(unsigned apiIndex, Args... args) { synchronousRequest(apiIndex, MegaRequest::TYPE_GET_ATTR_USER, [this, apiIndex, args...]() { megaApi[apiIndex]->getDeviceName(args...); }); return mApi[apiIndex].lastError; }
    template<typename ... Args> int synchronousSetBackupName(unsigned apiIndex, Args... args) { synchronousRequest(apiIndex, MegaRequest::TYPE_SET_ATTR_USER, [this, apiIndex, args...]() { megaApi[apiIndex]->setBackupName(args...); }); return mApi[apiIndex].lastError; }
    template<typename ... Args> int synchronousGetBackupName(unsigned apiIndex, Args... args) { synchronousRequest(apiIndex, MegaRequest::TYPE_GET_ATTR_USER, [this, apiIndex, args...]() { megaApi[apiIndex]->getBackupName(args...); }); return mApi[apiIndex].lastError; }
    template<typename ... Args> int synchronousSetUserAlias(unsigned apiIndex, Args... args) { synchronousRequest(apiIndex, MegaRequest::TYPE_SET_ATTR_USER, [this, apiIndex, args...]() { megaApi[apiIndex]->setUserAlias(args...); }); return mApi[apiIndex].lastError; }
    template<typename ... Args> int synchronousGetUserAlias(unsigned apiIndex, Args... args) { synchronousRequest(apiIndex, MegaRequest::TYPE_SET_ATTR_USER, [this, apiIndex, args...]() { megaApi[apiIndex]->getUserAlias(args...); }); return mApi[apiIndex].lastError; }
    template<typename ... Args> int synchronousQueryGoogleAds(unsigned apiIndex, Args... args) { synchronousRequest(apiIndex, MegaRequest::TYPE_QUERY_GOOGLE_ADS, [this, apiIndex, args...]() { megaApi[apiIndex]->queryGoogleAds(args...); }); return mApi[apiIndex].lastError; }
    template<typename ... Args> int synchronousFetchGoogleAds(unsigned apiIndex, Args... args) { synchronousRequest(apiIndex, MegaRequest::TYPE_FETCH_GOOGLE_ADS, [this, apiIndex, args...]() { megaApi[apiIndex]->fetchGoogleAds(args...); }); return mApi[apiIndex].lastError; }


    // convenience functions - make a request and wait for the result via listener, return the result code.  To add new functions to call, just copy the line
    template<typename ... requestArgs> std::unique_ptr<RequestTracker> asyncRequestLogin(unsigned apiIndex, requestArgs... args) { auto rt = ::mega::make_unique<RequestTracker>(megaApi[apiIndex].get()); megaApi[apiIndex]->login(args..., rt.get()); return rt; }
    template<typename ... requestArgs> std::unique_ptr<RequestTracker> asyncRequestFastLogin(unsigned apiIndex, requestArgs... args) { auto rt = ::mega::make_unique<RequestTracker>(megaApi[apiIndex].get()); megaApi[apiIndex]->fastLogin(args..., rt.get()); return rt; }
    template<typename ... requestArgs> std::unique_ptr<RequestTracker> asyncRequestLoginToFolder(unsigned apiIndex, requestArgs... args) { auto rt = ::mega::make_unique<RequestTracker>(megaApi[apiIndex].get()); megaApi[apiIndex]->loginToFolder(args..., rt.get()); return rt; }
    template<typename ... requestArgs> std::unique_ptr<RequestTracker> asyncRequestLoginToFolder(MegaApi *api, requestArgs... args) { auto rt = ::mega::make_unique<RequestTracker>(api); api->loginToFolder(args..., rt.get()); return rt; }
    template<typename ... requestArgs> std::unique_ptr<RequestTracker> asyncRequestFetchnodes(unsigned apiIndex, requestArgs... args) { auto rt = ::mega::make_unique<RequestTracker>(megaApi[apiIndex].get()); megaApi[apiIndex]->fetchNodes(args..., rt.get()); return rt; }
    template<typename ... requestArgs> std::unique_ptr<RequestTracker> asyncRequestFetchnodes(MegaApi *api, requestArgs... args) { auto rt = ::mega::make_unique<RequestTracker>(api); api->fetchNodes(args..., rt.get()); return rt; }
    template<typename ... requestArgs> int doRequestLogout(unsigned apiIndex, requestArgs... args) { RequestTracker rt(megaApi[apiIndex].get()); megaApi[apiIndex]->logout(args..., &rt); return rt.waitForResult(); }
    template<typename ... requestArgs> int doRequestLocalLogout(unsigned apiIndex, requestArgs... args) { RequestTracker rt(megaApi[apiIndex].get()); megaApi[apiIndex]->localLogout(args..., &rt); return rt.waitForResult(); }
    template<typename ... requestArgs> int doSetNodeDuration(unsigned apiIndex, requestArgs... args) { RequestTracker rt(megaApi[apiIndex].get()); megaApi[apiIndex]->setNodeDuration(args..., &rt); return rt.waitForResult(); }
    template<typename ... requestArgs> int doStartUpload(unsigned apiIndex, requestArgs... args) { TransferTracker tt(megaApi[apiIndex].get()); megaApi[apiIndex]->startUpload(args..., &tt); return tt.waitForResult(); }
    template<typename ... requestArgs> int doSetFileVersionsOption(unsigned apiIndex, requestArgs... args) { RequestTracker rt(megaApi[apiIndex].get()); megaApi[apiIndex]->setFileVersionsOption(args..., &rt); return rt.waitForResult(); }
    template<typename ... requestArgs> int doMoveNode(unsigned apiIndex, requestArgs... args) { RequestTracker rt(megaApi[apiIndex].get()); megaApi[apiIndex]->moveNode(args..., &rt); return rt.waitForResult(); }
    template<typename ... requestArgs> int doCopyNode(unsigned apiIndex, requestArgs... args) { RequestTracker rt(megaApi[apiIndex].get()); megaApi[apiIndex]->copyNode(args..., &rt); return rt.waitForResult(); }
    template<typename ... requestArgs> int synchronousSyncFolder(unsigned apiIndex, requestArgs... args) { RequestTracker rt(megaApi[apiIndex].get()); megaApi[apiIndex]->syncFolder(args..., &rt); rt.waitForResult(); mApi[apiIndex].lastSyncError = rt.request->getNumDetails() ; return rt.result; }
    template<typename ... requestArgs> int synchronousRemoveSync(unsigned apiIndex, requestArgs... args) { RequestTracker rt(megaApi[apiIndex].get()); megaApi[apiIndex]->removeSync(args..., &rt); return rt.waitForResult(); }
    template<typename ... requestArgs> int synchronousDisableSync(unsigned apiIndex, requestArgs... args) { RequestTracker rt(megaApi[apiIndex].get()); megaApi[apiIndex]->disableSync(args..., &rt); return rt.waitForResult(); }
    template<typename ... requestArgs> int synchronousEnableSync(unsigned apiIndex, requestArgs... args) { RequestTracker rt(megaApi[apiIndex].get()); megaApi[apiIndex]->enableSync(args..., &rt); rt.waitForResult(); mApi[apiIndex].lastSyncError = rt.request->getNumDetails() ; return rt.result; }
    template<typename ... requestArgs> int synchronousKillSession(unsigned apiIndex, requestArgs... args) { RequestTracker rt(megaApi[apiIndex].get()); megaApi[apiIndex]->killSession(args..., &rt); return rt.waitForResult(); }
    template<typename ... requestArgs> int synchronousSetBackup(unsigned apiIndex, requestArgs... args) { RequestTracker rt(megaApi[apiIndex].get());  megaApi[apiIndex]->setBackup(args..., &rt); return rt.waitForResult(); }

    void createFile(string filename, bool largeFile = true);
    int64_t getFilesize(string filename);
    void deleteFile(string filename);

    void getAccountsForTest(unsigned howMany = 1);
    void releaseMegaApi(unsigned int apiIndex);

    void inviteContact(unsigned apiIndex, string email, string message, int action);
    void replyContact(MegaContactRequest *cr, int action);
    void removeContact(string email, int timeout = maxTimeout);
    void setUserAttribute(int type, string value, int timeout = maxTimeout);
    void getUserAttribute(MegaUser *u, int type, int timeout = maxTimeout, int accountIndex = 1);

    void shareFolder(MegaNode *n, const char *email, int action, int timeout = maxTimeout);

    void createPublicLink(unsigned apiIndex, MegaNode *n, m_time_t expireDate, int timeout, bool isFreeAccount, bool writable = false);
    void importPublicLink(unsigned apiIndex, string link, MegaNode *parent, int timeout = maxTimeout);
    void getPublicNode(unsigned apiIndex, string link, int timeout = maxTimeout);
    void removePublicLink(unsigned apiIndex, MegaNode *n, int timeout = maxTimeout);

    void getContactRequest(unsigned int apiIndex, bool outgoing, int expectedSize = 1);

    void createFolder(unsigned int apiIndex, const char * name, MegaNode *n, int timeout = maxTimeout);
    void deleteNode(unsigned int apiIndex, MegaNode *n, int timeout = maxTimeout);
    void renameNode(unsigned int apiIndex, MegaNode *n, std::string newFolderName, int timeout = maxTimeout);
    void moveNode(unsigned int apiIndex, MegaNode *n, MegaNode *newParent, int timeout = maxTimeout);
    void copyNode(unsigned int apiIndex, MegaNode *n, MegaNode *newParent, const char* newName, int timeout = maxTimeout);

    void getRegisteredContacts(const std::map<std::string, std::string>& contacts);

    void getCountryCallingCodes(int timeout = maxTimeout);

#ifdef ENABLE_CHAT
    void createChat(bool group, MegaTextChatPeerList *peers, int timeout = maxTimeout);
#endif
};
