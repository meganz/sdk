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

// IMPORTANT: the main account must be empty (Cloud & Rubbish) before starting the test and it will be purged at exit.
// Both main and auxiliar accounts shouldn't be contacts yet and shouldn't have any pending contact requests.
// Set your login credentials as environment variables: $MEGA_EMAIL and $MEGA_PWD (and $MEGA_EMAIL_AUX / $MEGA_PWD_AUX for shares * contacts)

static const unsigned int pollingT      = 500000;   // (microseconds) to check if response from server is received
static const unsigned int maxTimeout    = 600;      // Maximum time (seconds) to wait for response from server
static const unsigned int defaultTimeout = 60;      // Normal time for most operations (seconds) to wait for response from server


struct TransferTracker : public ::mega::MegaTransferListener
{
    std::atomic<bool> started = { false };
    std::atomic<bool> finished = { false };
    std::atomic<ErrorCodes> result = { ErrorCodes::API_EINTERNAL };
    std::promise<ErrorCodes> promiseResult;
    MegaApi *mApi;
    std::future<ErrorCodes> futureResult;
    std::shared_ptr<TransferTracker> selfDeleteOnFinalCallback;

    MegaHandle resultNodeHandle = UNDEF;

    TransferTracker(MegaApi *api): mApi(api), futureResult(promiseResult.get_future())
    {

    }
    void onTransferStart(MegaApi *api, MegaTransfer *transfer) override
    {
        // called back on a different thread
        started = true;
    }
    void onTransferFinish(MegaApi* api, MegaTransfer *transfer, MegaError* error) override
    {
        // called back on a different thread
        resultNodeHandle = transfer->getNodeHandle();
        result = static_cast<ErrorCodes>(error->getErrorCode());
        finished = true;

        // this local version still valid even after we self-delete
        std::promise<ErrorCodes> local_promise = move(promiseResult);

        if (selfDeleteOnFinalCallback)
        {
            // this class can be used as a local on the stack, or constructed on the heap.
            // for the stack case, this object will be destroyed after the wait completes
            // but for the heap case, that is usually chosen so that deletion can occur on completion
            // or whenever the last needed reference is deleted.  So for that case,
            // set the selfDeleteOnFinalCallback to be a shared_ptr to this object.
            selfDeleteOnFinalCallback.reset();  // self-delete
        }

        // let the test main thread know it can now continue
        local_promise.set_value(result);
    }
    ErrorCodes waitForResult(int seconds = defaultTimeout, bool unregisterListenerOnTimeout = true)
    {
        // running on test's main thread
        if (std::future_status::ready != futureResult.wait_for(std::chrono::seconds(seconds)))
        {
            assert(mApi);
            if (unregisterListenerOnTimeout)
            {
                mApi->removeTransferListener(this);
            }
            return static_cast<ErrorCodes>(-999); // local timeout
        }
        return futureResult.get();
    }
};

typedef std::function<void(MegaError& e, MegaRequest& request)> OnReqFinish;

struct RequestTracker : public ::mega::MegaRequestListener
{
    std::atomic<bool> started = { false };
    std::atomic<bool> finished = { false };
    std::atomic<ErrorCodes> result = { ErrorCodes::API_EINTERNAL };
    std::promise<ErrorCodes> promiseResult;
    MegaApi *mApi;

    unique_ptr<MegaRequest> request;

    OnReqFinish onFinish;

    RequestTracker(MegaApi *api, OnReqFinish finish = nullptr)
        : mApi(api)
        , onFinish(finish)
    {
    }

    void onRequestStart(MegaApi* api, MegaRequest *request) override
    {
        started = true;
    }
    void onRequestFinish(MegaApi* api, MegaRequest *request, MegaError* e) override
    {
        if (onFinish) onFinish(*e, *request);

        result = ErrorCodes(e->getErrorCode());
        this->request.reset(request->copy());
        finished = true;
        promiseResult.set_value(static_cast<ErrorCodes>(result));
    }
    ErrorCodes waitForResult(int seconds = maxTimeout, bool unregisterListenerOnTimeout = true)
    {
        auto f = promiseResult.get_future();
        if (std::future_status::ready != f.wait_for(std::chrono::seconds(seconds)))
        {
            assert(mApi);
            if (unregisterListenerOnTimeout)
            {
                mApi->removeRequestListener(this);
            }
            return static_cast<ErrorCodes>(-999); // local timeout
        }
        return f.get();
    }

    MegaHandle getNodeHandle()
    {
        // if the operation succeeded and supplies a node handle
        if (request) return request->getNodeHandle();
        return INVALID_HANDLE;
    }

    string getLink()
    {
        // if the operation succeeded and supplies a link
        if (request && request->getLink()) return request->getLink();
        return "";
    }

    unique_ptr<MegaNode> getPublicMegaNode()
    {
        if (request) return unique_ptr<MegaNode>(request->getPublicMegaNode());
        return nullptr;
    }
};


struct OneShotListener : public ::mega::MegaRequestListener
{
    // on request completion, executes the lambda and deletes itself.

    std::function<void(MegaError& e, MegaRequest& request)> mFunc;

    OneShotListener(std::function<void(MegaError& e, MegaRequest& request)> f)
    : mFunc(f)
    {
    }

    void onRequestFinish(MegaApi* api, MegaRequest* request, MegaError* e) override
    {
        mFunc(*e, *request);
        delete this;
    }
};

using onNodesUpdateCompletion_t = std::function<void(size_t apiIndex, MegaNodeList* nodes)>;

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
        bool requestFlags[MegaRequest::TOTAL_OF_REQUEST_TYPES]; // to be removed due to race conditions
        bool transferFlags[MegaTransfer::TYPE_LOCAL_HTTP_DOWNLOAD];

        std::unique_ptr<MegaContactRequest> cr;
        std::unique_ptr<MegaTimeZoneDetails> tzDetails;
        std::unique_ptr<MegaAccountDetails> accountDetails;
        std::unique_ptr<MegaStringMap> mStringMap;
        std::unique_ptr<MegaPricing> mMegaPricing;
        std::unique_ptr<MegaCurrency> mMegaCurrency;

        // flags to monitor the updates of nodes/users/sets/set-elements/PCRs due to actionpackets
        bool userUpdated;
        bool setUpdated;
        bool setElementUpdated;
        bool contactRequestUpdated;
        bool accountUpdated;
        bool nodeUpdated; // flag to check specific updates for a node (upon onNodesUpdate)

        bool userAlertsUpdated;
        std::unique_ptr<MegaUserAlertList> userAlertList;

        // unique_ptr to custom functions that will be called upon reception of MegaApi callbacks
        onNodesUpdateCompletion_t mOnNodesUpdateCompletion;

        int lastSyncError;
        handle lastSyncBackupId = 0;

#ifdef ENABLE_CHAT
        bool chatUpdated;        // flags to monitor the updates of chats due to actionpackets
        map<handle, std::unique_ptr<MegaTextChat>> chats;   //  runtime cache of fetched/updated chats
        MegaHandle chatid;          // last chat added
#endif
    };

    std::vector<PerApi> mApi;
    std::vector<std::unique_ptr<MegaApi>> megaApi;

    // relevant values received in response of requests
    string chatlink;  // to be removed due to race conditions
    string attributeValue;
    string sid;
    std::unique_ptr<MegaStringListMap> stringListMap;
    std::unique_ptr<MegaStringTable> stringTable;

    m_off_t onTransferUpdate_progress;
    m_off_t onTransferUpdate_filesize;
    unsigned onTranferFinishedCount = 0;

    std::mutex lastEventMutex;
    std::unique_ptr<MegaEvent> lastEvent;
    std::set<int> lastEvents;

    void resetlastEvent()
    {
        lock_guard<mutex> g(lastEventMutex);
        lastEvent.reset();
        lastEvents.clear();
    }

    bool lastEventsContains(int type)
    {
        lock_guard<mutex> g(lastEventMutex);
        return lastEvents.find(type) != lastEvents.end();
    }


    MegaHandle mBackupId = UNDEF;
    unique_ptr<MegaHandleList> mMegaFavNodeList;

protected:
    void SetUp() override;
    void TearDown() override;

    void Cleanup();

    int getApiIndex(MegaApi* api);
    bool getApiIndex(MegaApi* api, size_t& apindex);

    bool checkAlert(int apiIndex, const string& title, const string& path);
    bool checkAlert(int apiIndex, const string& title, handle h, int64_t n = -1, MegaHandle mh = INVALID_HANDLE);

    void syncTestMyBackupsRemoteFolder(unsigned apiIdx);

    void onRequestStart(MegaApi *api, MegaRequest *request) override {}
    void onRequestUpdate(MegaApi*api, MegaRequest *request) override {}
    void onRequestFinish(MegaApi *api, MegaRequest *request, MegaError *e) override;
    void onRequestTemporaryError(MegaApi *api, MegaRequest *request, MegaError* error) override {}
    void onTransferStart(MegaApi *api, MegaTransfer *transfer) override { }
    void onTransferFinish(MegaApi* api, MegaTransfer *transfer, MegaError* e) override;
    void onTransferUpdate(MegaApi *api, MegaTransfer *transfer) override;
    void onTransferTemporaryError(MegaApi *api, MegaTransfer *transfer, MegaError* error) override {}
    void onUsersUpdate(MegaApi* api, MegaUserList *users) override;
    void onAccountUpdate(MegaApi *api) override;
    void onNodesUpdate(MegaApi* api, MegaNodeList *nodes) override;
    void onSetsUpdate(MegaApi *api, MegaSetList *sets) override;
    void onSetElementsUpdate(MegaApi *api, MegaSetElementList *elements) override;
    void onContactRequestsUpdate(MegaApi* api, MegaContactRequestList* requests) override;
    void onReloadNeeded(MegaApi *api) override {}

    void onUserAlertsUpdate(MegaApi* api, MegaUserAlertList* alerts) override;

#ifdef ENABLE_SYNC
    void onSyncFileStateChanged(MegaApi *api, MegaSync *sync, string* filePath, int newState) override {}
    void onSyncStateChanged(MegaApi *api,  MegaSync *sync) override {}
    void onGlobalSyncStateChanged(MegaApi* api) override {}
#endif
#ifdef ENABLE_CHAT
    void onChatsUpdate(MegaApi *api, MegaTextChatList *chats) override;
#endif
    void onEvent(MegaApi* api, MegaEvent *event) override;

    void resetOnNodeUpdateCompletionCBs();
    onNodesUpdateCompletion_t createOnNodesUpdateLambda(const MegaHandle&, int);
public:
    //void login(unsigned int apiIndex, int timeout = maxTimeout);
    //void loginBySessionId(unsigned int apiIndex, const std::string& sessionId, int timeout = maxTimeout);
    void fetchnodes(unsigned int apiIndex, int timeout = maxTimeout);
    void logout(unsigned int apiIndex, bool keepSyncConfigs, int timeout);
    char* dumpSession();
    void locallogout(int timeout = maxTimeout);
    void resumeSession(const char *session, int timeout = maxTimeout);

    void purgeTree(MegaNode *p, bool depthfirst = true);
    void purgeVaultTree(MegaNode *vault);
    bool waitForResponse(bool *responseReceived, unsigned int timeout = maxTimeout);

    bool synchronousRequest(unsigned apiIndex, int type, std::function<void()> f, unsigned int timeout = maxTimeout);
    bool synchronousTransfer(unsigned apiIndex, int type, std::function<void()> f, unsigned int timeout = maxTimeout);

    // *** WARNING *** THESE FUNCTIONS RETURN VALUE ARE SUBJECT TO RACE CONDITIONS
    // convenience functions - template args just make it easy to code, no need to copy all the exact argument types with listener defaults etc. To add a new one, just copy a line and change the flag and the function called.
    // WARNING: any sort of race can result in the lastError being set from some other command - better to use the listener based ones in the next list below
    template<typename ... Args> int synchronousCatchup(unsigned apiIndex, Args... args) { synchronousRequest(apiIndex, MegaRequest::TYPE_CATCHUP, [this, apiIndex, args...]() { megaApi[apiIndex]->catchup(args...); }); return mApi[apiIndex].lastError; }
    template<typename ... Args> int synchronousCreateEphemeralAccountPlusPlus(unsigned apiIndex, Args... args) { synchronousRequest(apiIndex, MegaRequest::TYPE_CREATE_ACCOUNT, [this, apiIndex, args...]() { megaApi[apiIndex]->createEphemeralAccountPlusPlus(args...); }); return mApi[apiIndex].lastError; }
    template<typename ... Args> int synchronousResumeCreateAccountEphemeralPlusPlus(unsigned apiIndex, Args... args) { synchronousRequest(apiIndex, MegaRequest::TYPE_CREATE_ACCOUNT, [this, apiIndex, args...]() { megaApi[apiIndex]->resumeCreateAccountEphemeralPlusPlus(args...); }); return mApi[apiIndex].lastError; }
    template<typename ... Args> int synchronousCreateAccount(unsigned apiIndex, Args... args) { synchronousRequest(apiIndex, MegaRequest::TYPE_CREATE_ACCOUNT, [this, apiIndex, args...]() { megaApi[apiIndex]->createAccount(args...); }); return mApi[apiIndex].lastError; }
    template<typename ... Args> int synchronousResumeCreateAccount(unsigned apiIndex, Args... args) { synchronousRequest(apiIndex, MegaRequest::TYPE_CREATE_ACCOUNT, [this, apiIndex, args...]() { megaApi[apiIndex]->resumeCreateAccount(args...); }); return mApi[apiIndex].lastError; }
    template<typename ... Args> int synchronousSendSignupLink(unsigned apiIndex, Args... args) { synchronousRequest(apiIndex, MegaRequest::TYPE_SEND_SIGNUP_LINK, [this, apiIndex, args...]() { megaApi[apiIndex]->sendSignupLink(args...); }); return mApi[apiIndex].lastError; }
    template<typename ... Args> int synchronousConfirmSignupLink(unsigned apiIndex, Args... args) { synchronousRequest(apiIndex, MegaRequest::TYPE_CONFIRM_ACCOUNT, [this, apiIndex, args...]() { megaApi[apiIndex]->confirmAccount(args...); }); return mApi[apiIndex].lastError; }
    template<typename ... Args> int synchronousFastLogin(unsigned apiIndex, Args... args) { synchronousRequest(apiIndex, MegaRequest::TYPE_LOGIN, [this, apiIndex, args...]() { megaApi[apiIndex]->fastLogin(args...); }); return mApi[apiIndex].lastError; }
    template<typename ... Args> int synchronousRemoveContact(unsigned apiIndex, Args... args) { synchronousRequest(apiIndex, MegaRequest::TYPE_REMOVE_CONTACT, [this, apiIndex, args...]() { megaApi[apiIndex]->removeContact(args...); }); return mApi[apiIndex].lastError; }
    template<typename ... Args> int synchronousShare(unsigned apiIndex, Args... args) { synchronousRequest(apiIndex, MegaRequest::TYPE_SHARE, [this, apiIndex, args...]() { megaApi[apiIndex]->share(args...); }); return mApi[apiIndex].lastError; }
    template<typename ... Args> int synchronousGetRegisteredContacts(unsigned apiIndex, Args... args) { synchronousRequest(apiIndex, MegaRequest::TYPE_GET_REGISTERED_CONTACTS, [this, apiIndex, args...]() { megaApi[apiIndex]->getRegisteredContacts(args...); }); return mApi[apiIndex].lastError; }
    template<typename ... Args> int synchronousGetCountryCallingCodes(unsigned apiIndex, Args... args) { synchronousRequest(apiIndex, MegaRequest::TYPE_GET_COUNTRY_CALLING_CODES, [this, apiIndex, args...]() { megaApi[apiIndex]->getCountryCallingCodes(args...); }); return mApi[apiIndex].lastError; }
    template<typename ... Args> int synchronousGetUserAvatar(unsigned apiIndex, Args... args) { synchronousRequest(apiIndex, MegaRequest::TYPE_GET_ATTR_USER, [this, apiIndex, args...]() { megaApi[apiIndex]->getUserAvatar(args...); }); return mApi[apiIndex].lastError; }
    template<typename ... Args> int synchronousGetUserAttribute(unsigned apiIndex, Args... args) { synchronousRequest(apiIndex, MegaRequest::TYPE_GET_ATTR_USER, [this, apiIndex, args...]() { megaApi[apiIndex]->getUserAttribute(args...); }); return mApi[apiIndex].lastError; }
    template<typename ... Args> int synchronousSetNodeDuration(unsigned apiIndex, Args... args) { synchronousRequest(apiIndex, MegaRequest::TYPE_SET_ATTR_NODE, [this, apiIndex, args...]() { megaApi[apiIndex]->setNodeDuration(args...); }); return mApi[apiIndex].lastError; }
    template<typename ... Args> int synchronousSetNodeCoordinates(unsigned apiIndex, Args... args) { synchronousRequest(apiIndex, MegaRequest::TYPE_SET_ATTR_NODE, [this, apiIndex, args...]() { megaApi[apiIndex]->setNodeCoordinates(args...); }); return mApi[apiIndex].lastError; }
    template<typename ... Args> int synchronousGetSpecificAccountDetails(unsigned apiIndex, Args... args) { synchronousRequest(apiIndex, MegaRequest::TYPE_ACCOUNT_DETAILS, [this, apiIndex, args...]() { megaApi[apiIndex]->getSpecificAccountDetails(args...); }); return mApi[apiIndex].lastError; }
    template<typename ... Args> int synchronousMediaUploadRequestURL(unsigned apiIndex, Args... args) { synchronousRequest(apiIndex, MegaRequest::TYPE_GET_BACKGROUND_UPLOAD_URL, [this, apiIndex, args...]() { megaApi[apiIndex]->backgroundMediaUploadRequestUploadURL(args...); }); return mApi[apiIndex].lastError; }
    template<typename ... Args> int synchronousMediaUploadComplete(unsigned apiIndex, Args... args) { synchronousRequest(apiIndex, MegaRequest::TYPE_COMPLETE_BACKGROUND_UPLOAD, [this, apiIndex, args...]() { megaApi[apiIndex]->backgroundMediaUploadComplete(args...); }); return mApi[apiIndex].lastError; }
    template<typename ... Args> int synchronousFetchTimeZone(unsigned apiIndex, Args... args) { synchronousRequest(apiIndex, MegaRequest::TYPE_FETCH_TIMEZONE, [this, apiIndex, args...]() { megaApi[apiIndex]->fetchTimeZone(args...); }); return mApi[apiIndex].lastError; }
    template<typename ... Args> int synchronousGetMiscFlags(unsigned apiIndex, Args... args) { synchronousRequest(apiIndex, MegaRequest::TYPE_GET_MISC_FLAGS, [this, apiIndex, args...]() { megaApi[apiIndex]->getMiscFlags(args...); }); return mApi[apiIndex].lastError; }
    template<typename ... Args> int synchronousGetUserEmail(unsigned apiIndex, Args... args) { synchronousRequest(apiIndex, MegaRequest::TYPE_GET_USER_EMAIL, [this, apiIndex, args...]() { megaApi[apiIndex]->getUserEmail(args...); }); return mApi[apiIndex].lastError; }
    template<typename ... Args> int synchronousCleanRubbishBin(unsigned apiIndex, Args... args) { synchronousRequest(apiIndex, MegaRequest::TYPE_CLEAN_RUBBISH_BIN, [this, apiIndex, args...]() { megaApi[apiIndex]->cleanRubbishBin(args...); }); return mApi[apiIndex].lastError; }
    template<typename ... Args> int synchronousGetExtendedAccountDetails(unsigned apiIndex, Args... args) { synchronousRequest(apiIndex, MegaRequest::TYPE_ACCOUNT_DETAILS, [this, apiIndex, args...]() { megaApi[apiIndex]->getExtendedAccountDetails(args...); }); return mApi[apiIndex].lastError; }
    template<typename ... Args> int synchronousGetBanners(unsigned apiIndex, Args... args) { synchronousRequest(apiIndex, MegaRequest::TYPE_GET_BANNERS, [this, apiIndex, args...]() { megaApi[apiIndex]->getBanners(args...); }); return mApi[apiIndex].lastError; }
    template<typename ... Args> int synchronousGetPricing(unsigned apiIndex, Args... args) { synchronousRequest(apiIndex, MegaRequest::TYPE_GET_PRICING, [this, apiIndex, args...]() { megaApi[apiIndex]->getPricing(args...); }); return mApi[apiIndex].lastError; }
    template<typename ... Args> int synchronousUpdateBackup(unsigned apiIndex, Args... args) { synchronousRequest(apiIndex, MegaRequest::TYPE_BACKUP_PUT, [this, apiIndex, args...]() { megaApi[apiIndex]->updateBackup(args...); }); return mApi[apiIndex].lastError; }
    template<typename ... Args> int synchronousRemoveBackup(unsigned apiIndex, Args... args) { synchronousRequest(apiIndex, MegaRequest::TYPE_BACKUP_REMOVE, [this, apiIndex, args...]() { megaApi[apiIndex]->removeBackup(args...); }); return mApi[apiIndex].lastError; }
    template<typename ... Args> int synchronousSendBackupHeartbeat(unsigned apiIndex, Args... args) { synchronousRequest(apiIndex, MegaRequest::TYPE_BACKUP_PUT_HEART_BEAT, [this, apiIndex, args...]() { megaApi[apiIndex]->sendBackupHeartbeat(args...); }); return mApi[apiIndex].lastError; }
    template<typename ... Args> int synchronousSetMyBackupsFolder(unsigned apiIndex, Args... args) { synchronousRequest(apiIndex, MegaRequest::TYPE_SET_MY_BACKUPS, [this, apiIndex, args...]() { megaApi[apiIndex]->setMyBackupsFolder(args...); }); return mApi[apiIndex].lastError; }
    template<typename ... Args> int synchronousSetDeviceName(unsigned apiIndex, Args... args) { synchronousRequest(apiIndex, MegaRequest::TYPE_SET_ATTR_USER, [this, apiIndex, args...]() { megaApi[apiIndex]->setDeviceName(args...); }); return mApi[apiIndex].lastError; }
    template<typename ... Args> int synchronousGetDeviceName(unsigned apiIndex, Args... args) { synchronousRequest(apiIndex, MegaRequest::TYPE_GET_ATTR_USER, [this, apiIndex, args...]() { megaApi[apiIndex]->getDeviceName(args...); }); return mApi[apiIndex].lastError; }
    template<typename ... Args> int synchronousSetDriveName(unsigned apiIndex, Args... args) { synchronousRequest(apiIndex, MegaRequest::TYPE_SET_ATTR_USER, [this, apiIndex, args...]() { megaApi[apiIndex]->setDriveName(args...); }); return mApi[apiIndex].lastError; }
    template<typename ... Args> int synchronousGetDriveName(unsigned apiIndex, Args... args) { synchronousRequest(apiIndex, MegaRequest::TYPE_GET_ATTR_USER, [this, apiIndex, args...]() { megaApi[apiIndex]->getDriveName(args...); }); return mApi[apiIndex].lastError; }
    template<typename ... Args> int synchronousSetUserAlias(unsigned apiIndex, Args... args) { synchronousRequest(apiIndex, MegaRequest::TYPE_SET_ATTR_USER, [this, apiIndex, args...]() { megaApi[apiIndex]->setUserAlias(args...); }); return mApi[apiIndex].lastError; }
    template<typename ... Args> int synchronousGetUserAlias(unsigned apiIndex, Args... args) { synchronousRequest(apiIndex, MegaRequest::TYPE_GET_ATTR_USER, [this, apiIndex, args...]() { megaApi[apiIndex]->getUserAlias(args...); }); return mApi[apiIndex].lastError; }

    // *** USE THESE ONES INSTEAD ***
    // convenience functions - make a request and wait for the result via listener, return the result code.  To add new functions to call, just copy the line
    template<typename ... requestArgs> std::unique_ptr<RequestTracker> asyncRequestLogin(unsigned apiIndex, requestArgs... args) { auto rt = ::mega::make_unique<RequestTracker>(megaApi[apiIndex].get()); megaApi[apiIndex]->login(args..., rt.get()); return rt; }
    template<typename ... requestArgs> std::unique_ptr<RequestTracker> asyncRequestFastLogin(unsigned apiIndex, requestArgs... args) { auto rt = ::mega::make_unique<RequestTracker>(megaApi[apiIndex].get()); megaApi[apiIndex]->fastLogin(args..., rt.get()); return rt; }
    template<typename ... requestArgs> std::unique_ptr<RequestTracker> asyncRequestFastLogin(int apiIndex, requestArgs... args) { auto rt = ::mega::make_unique<RequestTracker>(megaApi[apiIndex].get()); megaApi[apiIndex]->fastLogin(args..., rt.get()); return rt; }
    template<typename ... requestArgs> std::unique_ptr<RequestTracker> asyncRequestFastLogin(MegaApi *api, requestArgs... args) { auto rt = ::mega::make_unique<RequestTracker>(api); api->fastLogin(args..., rt.get()); return rt; }
    template<typename ... requestArgs> std::unique_ptr<RequestTracker> asyncRequestLoginToFolder(unsigned apiIndex, requestArgs... args) { auto rt = ::mega::make_unique<RequestTracker>(megaApi[apiIndex].get()); megaApi[apiIndex]->loginToFolder(args..., rt.get()); return rt; }
    template<typename ... requestArgs> std::unique_ptr<RequestTracker> asyncRequestLoginToFolder(MegaApi *api, requestArgs... args) { auto rt = ::mega::make_unique<RequestTracker>(api); api->loginToFolder(args..., rt.get()); return rt; }
    template<typename ... requestArgs> std::unique_ptr<RequestTracker> asyncRequestLocalLogout(MegaApi *api, requestArgs... args) { auto rt = ::mega::make_unique<RequestTracker>(api); api->localLogout(args..., rt.get()); return rt; }
    template<typename ... requestArgs> std::unique_ptr<RequestTracker> asyncRequestFetchnodes(unsigned apiIndex, requestArgs... args) { auto rt = ::mega::make_unique<RequestTracker>(megaApi[apiIndex].get()); megaApi[apiIndex]->fetchNodes(args..., rt.get()); return rt; }
    template<typename ... requestArgs> std::unique_ptr<RequestTracker> asyncRequestFetchnodes(MegaApi *api, requestArgs... args) { auto rt = ::mega::make_unique<RequestTracker>(api); api->fetchNodes(args..., rt.get()); return rt; }
    template<typename ... requestArgs> int doRequestLogout(unsigned apiIndex, requestArgs... args) { RequestTracker rt(megaApi[apiIndex].get()); megaApi[apiIndex]->logout(args..., &rt); return rt.waitForResult(); }
    template<typename ... requestArgs> int doRequestLocalLogout(unsigned apiIndex, requestArgs... args) { RequestTracker rt(megaApi[apiIndex].get()); megaApi[apiIndex]->localLogout(args..., &rt); return rt.waitForResult(); }
    template<typename ... requestArgs> int doSetNodeDuration(unsigned apiIndex, requestArgs... args) { RequestTracker rt(megaApi[apiIndex].get()); megaApi[apiIndex]->setNodeDuration(args..., &rt); return rt.waitForResult(); }
    template<typename ... requestArgs> int doStartUpload(unsigned apiIndex, MegaHandle* newNodeHandleResult, requestArgs... args) { TransferTracker tt(megaApi[apiIndex].get()); megaApi[apiIndex]->startUpload(args..., &tt); auto e = tt.waitForResult(); if (newNodeHandleResult) *newNodeHandleResult = tt.resultNodeHandle; return e;}
    template<typename ... requestArgs> int doStartDownload(unsigned apiIndex, requestArgs... args) { TransferTracker tt(megaApi[apiIndex].get()); megaApi[apiIndex]->startDownload(args..., &tt); auto e = tt.waitForResult(); return e;}
    template<typename ... requestArgs> int doSetFileVersionsOption(unsigned apiIndex, requestArgs... args) { RequestTracker rt(megaApi[apiIndex].get()); megaApi[apiIndex]->setFileVersionsOption(args..., &rt); return rt.waitForResult(); }
    template<typename ... requestArgs> int doMoveNode(unsigned apiIndex, MegaHandle* movedNodeHandle, requestArgs... args) { RequestTracker rt(megaApi[apiIndex].get()); megaApi[apiIndex]->moveNode(args..., &rt); rt.waitForResult(); if (movedNodeHandle) *movedNodeHandle = rt.getNodeHandle(); return rt.result; }
    template<typename ... requestArgs> int doCopyNode(unsigned apiIndex, MegaHandle* newNodeResult, requestArgs... args) { RequestTracker rt(megaApi[apiIndex].get()); megaApi[apiIndex]->copyNode(args..., &rt); rt.waitForResult(); if (newNodeResult) *newNodeResult = rt.getNodeHandle(); return rt.result; }
    template<typename ... requestArgs> int doRenameNode(unsigned apiIndex, requestArgs... args) { RequestTracker rt(megaApi[apiIndex].get()); megaApi[apiIndex]->renameNode(args..., &rt); return rt.waitForResult(); }
    template<typename ... requestArgs> int doDeleteNode(unsigned apiIndex, requestArgs... args) { RequestTracker rt(megaApi[apiIndex].get()); megaApi[apiIndex]->remove(args..., &rt); return rt.waitForResult(); }
    template<typename ... requestArgs> int doGetThumbnail(unsigned apiIndex, requestArgs... args) { RequestTracker rt(megaApi[apiIndex].get()); megaApi[apiIndex]->getThumbnail(args..., &rt); return rt.waitForResult(); }
    template<typename ... requestArgs> int doGetPreview(unsigned apiIndex, requestArgs... args) { RequestTracker rt(megaApi[apiIndex].get()); megaApi[apiIndex]->getPreview(args..., &rt); return rt.waitForResult(); }
    template<typename ... requestArgs> int doGetThumbnailUploadURL(unsigned apiIndex, std::string& url, requestArgs... args) { RequestTracker rt(megaApi[apiIndex].get()); megaApi[apiIndex]->getThumbnailUploadURL(args..., &rt); rt.waitForResult(); url = rt.request->getName(); return rt.result; }
    template<typename ... requestArgs> int doGetPreviewUploadURL(unsigned apiIndex, std::string& url, requestArgs... args) { RequestTracker rt(megaApi[apiIndex].get()); megaApi[apiIndex]->getPreviewUploadURL(args..., &rt); rt.waitForResult(); url = rt.request->getName(); return rt.result; }
    template<typename ... requestArgs> int doPutThumbnail(unsigned apiIndex, MegaBackgroundMediaUpload* mbmu, requestArgs... args) { RequestTracker rt(megaApi[apiIndex].get()); megaApi[apiIndex]->putThumbnail(mbmu, args..., &rt); rt.waitForResult(); mbmu->setThumbnail(rt.getNodeHandle()); return rt.result; }
    template<typename ... requestArgs> int doPutPreview(unsigned apiIndex, MegaBackgroundMediaUpload* mbmu, requestArgs... args) { RequestTracker rt(megaApi[apiIndex].get()); megaApi[apiIndex]->putPreview(mbmu, args..., &rt); rt.waitForResult(); mbmu->setPreview(rt.getNodeHandle()); return rt.result; }
    template<typename ... requestArgs> int doAckUserAlerts(unsigned apiIndex) { RequestTracker rt(megaApi[apiIndex].get()); megaApi[apiIndex]->acknowledgeUserAlerts(&rt); rt.waitForResult(); return rt.result; }
#ifdef ENABLE_SYNC
    template<typename ... requestArgs> int synchronousSyncFolder(unsigned apiIndex, MegaHandle* newSyncRootNodeResult, requestArgs... args) { RequestTracker rt(megaApi[apiIndex].get()); megaApi[apiIndex]->syncFolder(args..., &rt); rt.waitForResult(); mApi[apiIndex].lastSyncError = rt.request->getNumDetails(); mApi[apiIndex].lastSyncBackupId = rt.request->getParentHandle(); if (newSyncRootNodeResult) *newSyncRootNodeResult = rt.getNodeHandle(); return rt.result; }
    template<typename ... requestArgs> int synchronousRemoveSync(unsigned apiIndex, requestArgs... args) { RequestTracker rt(megaApi[apiIndex].get()); megaApi[apiIndex]->removeSync(args..., &rt); return rt.waitForResult(); }
    template<typename ... requestArgs> int synchronousRemoveBackupNodes(unsigned apiIndex, requestArgs... args) { RequestTracker rt(megaApi[apiIndex].get()); megaApi[apiIndex]->moveOrRemoveDeconfiguredBackupNodes(args..., INVALID_HANDLE, &rt); return rt.waitForResult(); }
    template<typename ... requestArgs> int synchronousDisableSync(unsigned apiIndex, requestArgs... args) { RequestTracker rt(megaApi[apiIndex].get()); megaApi[apiIndex]->disableSync(args..., &rt); return rt.waitForResult(); }
    template<typename ... requestArgs> int synchronousEnableSync(unsigned apiIndex, requestArgs... args) { RequestTracker rt(megaApi[apiIndex].get()); megaApi[apiIndex]->enableSync(args..., &rt); rt.waitForResult(); mApi[apiIndex].lastSyncError = rt.request->getNumDetails() ; return rt.result; }
#endif // ENABLE_SYNC
    template<typename ... requestArgs> int synchronousKillSession(unsigned apiIndex, requestArgs... args) { RequestTracker rt(megaApi[apiIndex].get()); megaApi[apiIndex]->killSession(args..., &rt); return rt.waitForResult(); }
    template<typename ... requestArgs> int synchronousSetBackup(unsigned apiIndex, OnReqFinish f, requestArgs... args) { RequestTracker rt(megaApi[apiIndex].get(), f);  megaApi[apiIndex]->setBackup(args..., &rt); return rt.waitForResult(); }
    template<typename ... requestArgs> int doExportNode(unsigned apiIndex, requestArgs... args) { RequestTracker rt(megaApi[apiIndex].get()); megaApi[apiIndex]->exportNode(args..., &rt); return rt.waitForResult(); }
    template<typename ... requestArgs> int doDisableExport(unsigned apiIndex, requestArgs... args) { RequestTracker rt(megaApi[apiIndex].get()); megaApi[apiIndex]->disableExport(args..., &rt); return rt.waitForResult(); }
    template<typename ... requestArgs> int synchronousSetNodeFavourite(unsigned apiIndex, requestArgs... args) { RequestTracker rt(megaApi[apiIndex].get());  megaApi[apiIndex]->setNodeFavourite(args..., &rt); return rt.waitForResult(); }
    template<typename ... requestArgs> int synchronousGetFavourites(unsigned apiIndex, requestArgs... args) { RequestTracker rt(megaApi[apiIndex].get());  megaApi[apiIndex]->getFavourites(args..., &rt); return rt.waitForResult(); }
    template<typename ... requestArgs> int synchronousInviteContact(unsigned apiIndex, requestArgs... args) { RequestTracker rt(megaApi[apiIndex].get());  megaApi[apiIndex]->inviteContact(args..., &rt); return rt.waitForResult(); }
    template<typename ... requestArgs> int synchronousReplyContactRequest(unsigned apiIndex, requestArgs... args) { RequestTracker rt(megaApi[apiIndex].get());  megaApi[apiIndex]->replyContactRequest(args..., &rt); return rt.waitForResult(); }
    template<typename ... requestArgs> int synchronousRemove(unsigned apiIndex, requestArgs... args) { RequestTracker rt(megaApi[apiIndex].get()); megaApi[apiIndex]->remove(args..., &rt); return rt.waitForResult(); }
    template<typename ... requestArgs> int doCreateSet(unsigned apiIndex, MegaSet** s, requestArgs... args) { RequestTracker rt(megaApi[apiIndex].get()); megaApi[apiIndex]->createSet(args..., &rt); rt.waitForResult(); if (s && rt.request->getMegaSet()) *s = rt.request->getMegaSet()->copy(); return rt.result; }
    template<typename ... requestArgs> int doUpdateSetName(unsigned apiIndex, MegaHandle* id, requestArgs... args) { RequestTracker rt(megaApi[apiIndex].get()); megaApi[apiIndex]->updateSetName(args..., &rt); rt.waitForResult(); if (id) *id = rt.request->getParentHandle(); return rt.result; }
    template<typename ... requestArgs> int doPutSetCover(unsigned apiIndex, MegaHandle* id, requestArgs... args) { RequestTracker rt(megaApi[apiIndex].get()); megaApi[apiIndex]->putSetCover(args..., &rt); rt.waitForResult(); if (id) *id = rt.request->getParentHandle(); return rt.result; }
    template<typename ... requestArgs> int doFetchSet(unsigned apiIndex, MegaSet** s, MegaSetElementList** els, requestArgs... args)
    {
        RequestTracker rt(megaApi[apiIndex].get()); megaApi[apiIndex]->fetchSet(args..., &rt); rt.waitForResult();
        if (s) *s = rt.request->getMegaSet()->copy();
        if (els) *els = rt.request->getMegaSetElementList()->copy();
        return rt.result;
    }
    template<typename ... requestArgs> int doRemoveSet(unsigned apiIndex, requestArgs... args) { RequestTracker rt(megaApi[apiIndex].get()); megaApi[apiIndex]->removeSet(args..., &rt); rt.waitForResult(); return rt.result; }
    template<typename ... requestArgs> int doCreateSetElement(unsigned apiIndex, MegaSetElementList** ell, requestArgs... args) { RequestTracker rt(megaApi[apiIndex].get()); megaApi[apiIndex]->createSetElement(args..., &rt); rt.waitForResult(); if (ell && rt.request->getMegaSetElementList()) *ell = rt.request->getMegaSetElementList()->copy(); return rt.result; }
    template<typename ... requestArgs> int doUpdateSetElementName(unsigned apiIndex, MegaHandle* eid, requestArgs... args) { RequestTracker rt(megaApi[apiIndex].get()); megaApi[apiIndex]->updateSetElementName(args..., &rt); rt.waitForResult(); if (eid) *eid = rt.request->getParentHandle(); return rt.result; }
    template<typename ... requestArgs> int doUpdateSetElementOrder(unsigned apiIndex, MegaHandle* eid, requestArgs... args) { RequestTracker rt(megaApi[apiIndex].get()); megaApi[apiIndex]->updateSetElementOrder(args..., &rt); rt.waitForResult(); if (eid) *eid = rt.request->getParentHandle(); return rt.result; }
    template<typename ... requestArgs> int doRemoveSetElement(unsigned apiIndex, requestArgs... args) { RequestTracker rt(megaApi[apiIndex].get()); megaApi[apiIndex]->removeSetElement(args..., &rt); rt.waitForResult(); return rt.result; }
    template<typename ... requestArgs> int synchronousCancelTransfers(unsigned apiIndex, requestArgs... args) { RequestTracker rt(megaApi[apiIndex].get()); megaApi[apiIndex]->cancelTransfers(args..., &rt); return rt.waitForResult(); }

    // Checkup methods called from MegaApi callbacks
    void onNodesUpdateCheck(size_t apiIndex, MegaHandle target, MegaNodeList* nodes, int change = -1);

    bool createFile(string filename, bool largeFile = true);
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

    string createPublicLink(unsigned apiIndex, MegaNode *n, m_time_t expireDate, int timeout, bool isFreeAccount, bool writable = false, bool megaHosted = false);
    MegaHandle importPublicLink(unsigned apiIndex, string link, MegaNode *parent);
    unique_ptr<MegaNode> getPublicNode(unsigned apiIndex, string link);
    MegaHandle removePublicLink(unsigned apiIndex, MegaNode *n);

    void getContactRequest(unsigned int apiIndex, bool outgoing, int expectedSize = 1);

    MegaHandle createFolder(unsigned int apiIndex, const char *name, MegaNode *parent, int timeout = maxTimeout);

    void getRegisteredContacts(const std::map<std::string, std::string>& contacts);

    void getCountryCallingCodes(int timeout = maxTimeout);

    void synchronousMediaUpload(unsigned int apiIndex, int64_t fileSize, const char* filename, const char* fileEncrypted, const char* fileOutput, const char* fileThumbnail, const char* filePreview);

#ifdef ENABLE_CHAT
    void createChat(bool group, MegaTextChatPeerList *peers, int timeout = maxTimeout);
#endif
};
