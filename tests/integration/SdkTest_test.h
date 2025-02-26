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

#include "../include/megaapi.h"
#include "../include/megaapi_impl.h"
#include "gtest/gtest.h"
#include "mega.h"
#include "sdk_test_data_provider.h"
#include "test.h"

#include <atomic>
#include <fstream>
#include <future>
#include <iostream>
#include <memory>
#include <type_traits>

#ifndef WIN32
#define DOTSLASH "./"
#else
#define DOTSLASH ".\\"
#endif

using namespace mega;
using ::testing::Test;

// IMPORTANT: the main account must be empty (Cloud & Rubbish) before starting the test and it will be purged at exit.
// Both main and auxiliar accounts shouldn't be contacts yet and shouldn't have any pending contact requests.
// Set your login credentials as environment variables: $MEGA_EMAIL and $MEGA_PWD (and $MEGA_EMAIL_AUX / $MEGA_PWD_AUX for shares * contacts)

static const unsigned int pollingT      = 500000;   // (microseconds) to check if response from server is received
static const unsigned int maxTimeout    = 600;      // Maximum time (seconds) to wait for response from server
static const unsigned int defaultTimeout = 60;      // Normal time for most operations (seconds) to wait for response from server
static const unsigned int defaultTimeoutMs = defaultTimeout * 1000;
static const unsigned int waitForSyncsMs = 4000;    // Time to wait after a sync has been created and before adding new files to it

/**
 * @brief Wrapper struct of MegaListener to get all information related to a MEGA account
 *  - make sure callbacks are consistent - added() first, nothing after deleted(), etc.
 *
 * @note: map by tag for now, should be backupId when that is available
 */
struct SyncListener: MegaListener
{
    enum callbacks_t
    {
        SyncFileStateChanged,
        SyncAdded,
        SyncDeleted,
        SyncStateChanged,
        SyncRemoteRootChanged,
        GlobalSyncStateChanged,
        CountCbs,
    };

    enum syncstate_t
    {
        nonexistent,
        added,
        deleted
    };

    /**
     * Array of flags that informs, when SyncListener callbacks have been received
     */
    std::array<std::atomic<bool>, CountCbs> mRecvCbs{};

    std::map<handle, syncstate_t> stateMap;

    syncstate_t& state(MegaSync* sync)
    {
        if (stateMap.find(sync->getBackupId()) == stateMap.end())
        {
            stateMap[sync->getBackupId()] = nonexistent;
        }
        return stateMap[sync->getBackupId()];
    }

    std::vector<std::string> mErrors;

    bool anyErrors = false;

    bool hasAnyErrors()
    {
        for (auto& s: mErrors)
        {
            out() << "SyncListener error: " << s;
        }
        return anyErrors;
    }

    void check(bool b, std::string e = std::string())
    {
        if (!b)
        {
            anyErrors = true;
            if (!e.empty())
            {
                mErrors.push_back(e);
                out() << "SyncListener added error: " << e;
            }
        }
    }

    void clear()
    {
        // session was logged out (locally)
        stateMap.clear();
    }

    void onSyncFileStateChanged(MegaApi*,
                                MegaSync* /*sync*/,
                                std::string* /*localPath*/,
                                int /*newState*/) override
    {
        // probably too frequent to output
        // out() << "onSyncFileStateChanged " << sync << newState;
        mRecvCbs[SyncFileStateChanged] = true;
    }

    void onSyncAdded(MegaApi*, MegaSync* sync) override
    {
        out() << "onSyncAdded " << toHandle(sync->getBackupId());
        check(sync->getBackupId() != UNDEF, "sync added with undef backup Id");

        check(state(sync) == nonexistent);
        state(sync) = added;
        mRecvCbs[SyncAdded] = true;
    }

    void onSyncDeleted(MegaApi*, MegaSync* sync) override
    {
        out() << "onSyncDeleted " << toHandle(sync->getBackupId());
        check(state(sync) != nonexistent && state(sync) != deleted);
        state(sync) = nonexistent;
        mRecvCbs[SyncDeleted] = true;
    }

    void onSyncStateChanged(MegaApi*, MegaSync* sync) override
    {
        out() << "onSyncStateChanged " << toHandle(sync->getBackupId())
              << " runState: " << sync->getRunState();

        check(sync->getBackupId() != UNDEF, "onSyncStateChanged with undef backup Id");

        // MegaApi doco says: "Notice that adding a sync will not cause onSyncStateChanged to be
        // called." And also: "for changes that imply other callbacks, expect that the SDK will call
        // onSyncStateChanged first, so that you can update your model only using this one."
        check(state(sync) != nonexistent);
        mRecvCbs[SyncStateChanged] = true;
    }

    void onSyncRemoteRootChanged(MegaApi*, MegaSync* sync) override
    {
        out() << "onSyncRemoteRootChanged " << toHandle(sync->getBackupId())
              << " new Remote root: " << sync->getLastKnownMegaFolder();
        mRecvCbs[SyncRemoteRootChanged] = true;
    }

    void onGlobalSyncStateChanged(MegaApi*) override
    {
        // just too frequent for out() really
        // out() << "onGlobalSyncStateChanged ";
        mRecvCbs[GlobalSyncStateChanged] = true;
    }
};

/**
 * @brief The MegaListenerDeregisterer Struct
 *  - register the listener on constructions
 *  - deregister on destruction (ie, whenever we exit the function - we may exit early if a test
 * fails
 */
struct MegaListenerDeregisterer
{
    MegaApi* api = nullptr;
    MegaListener* listener;

    MegaListenerDeregisterer(MegaApi* a, SyncListener* l):
        api(a),
        listener(l)
    {
        api->addListener(listener);
    }

    ~MegaListenerDeregisterer()
    {
        api->removeListener(listener);
    }
};

struct TransferTracker : public ::mega::MegaTransferListener
{
    std::atomic<bool> started = { false };
    std::atomic<bool> finished = { false };
    std::atomic<ErrorCodes> result = { ErrorCodes::API_EINTERNAL };
    std::promise<ErrorCodes> promiseResult;
    MegaApi *mApi;
    std::future<ErrorCodes> futureResult;
    std::shared_ptr<TransferTracker> selfDeleteOnFinalCallback;

    bool mTempFileRemoved{false};
    MegaHandle resultNodeHandle = UNDEF;

    TransferTracker(MegaApi *api): mApi(api), futureResult(promiseResult.get_future())
    {

    }

    ~TransferTracker() override
    {
        if (!finished)
        {
            assert(mApi);
            mApi->removeTransferListener(this);
        }
    }

    void onTransferStart(MegaApi*, MegaTransfer*) override
    {
        // called back on a different thread
        started = true;
    }

    void onTransferFinish(MegaApi*, MegaTransfer* transfer, MegaError* error) override
    {
        LOG_debug << "TransferTracker::onTransferFinish callback received.  Result: " << error->getErrorCode() << " for " << (transfer->getFileName() ? transfer->getFileName() : "<null>");
        mTempFileRemoved = static_cast<bool>(transfer->getStage());

        // called back on a different thread
        resultNodeHandle = transfer->getNodeHandle();
        result = static_cast<ErrorCodes>(error->getErrorCode());
        finished = true;

        // this local version still valid even after we self-delete
        std::promise<ErrorCodes> local_promise = std::move(promiseResult);

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
            return static_cast<ErrorCodes>(LOCAL_ETIMEOUT); // local timeout
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

    ~RequestTracker() override
    {
        if (!finished)
        {
            assert(mApi);
            mApi->removeRequestListener(this);
        }
    }

    void onRequestStart(MegaApi*, MegaRequest*) override
    {
        started = true;
    }

    void onRequestFinish(MegaApi*, MegaRequest* request, MegaError* e) override
    {
        if (onFinish) onFinish(*e, *request);

        result = ErrorCodes(e->getErrorCode());
        this->request.reset(request->copy());
        assert(this->request->getType() != MegaRequest::TYPE_ADD_SYNC || this->request->getNumDetails() <= SyncError::NO_SYNC_ERROR || this->request->getNumDetails() == e->getSyncError());
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
            return static_cast<ErrorCodes>(LOCAL_ETIMEOUT); // local timeout
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

    bool getFlag()
    {
        return request ? request->getFlag() : false;
    }

    template<typename... Args, typename... Params>
    static unique_ptr<RequestTracker> async(MegaApi& api,
                                            void (MegaApi::*mf)(Params...),
                                            Args&&... args)
    {
        static_assert(sizeof...(Args) + 1 == sizeof...(Params));
        auto rt = std::make_unique<RequestTracker>(&api);
        (api.*mf)(std::forward<Args>(args)..., rt.get());
        return rt;
    }
};


struct OneShotListener : public ::mega::MegaRequestListener
{
    // on request completion, executes the lambda and deletes itself.

    std::function<void(MegaError& e, MegaRequest& request)> mFunc;

    OneShotListener(std::function<void(MegaError&, MegaRequest&)> f):
        mFunc(f)
    {}

    void onRequestFinish(MegaApi*, MegaRequest* request, MegaError* e) override
    {
        mFunc(*e, *request);
        delete this;
    }
};

using onNodesUpdateCompletion_t = std::function<void(size_t apiIndex, MegaNodeList* nodes)>;

class MegaApiTest: public MegaApi
{
public:
    MegaApiTest(const char* appKey,
                const char* basePath = nullptr,
                const char* userAgent = nullptr,
                unsigned workerThreadCount = 1,
                const int clientType = MegaApi::CLIENT_TYPE_DEFAULT);

    MegaApiTest(const char* appKey,
                MegaGfxProvider* provider,
                const char* basePath = nullptr,
                const char* userAgent = nullptr,
                unsigned workerThreadCount = 1,
                const int clientType = MegaApi::CLIENT_TYPE_DEFAULT);

    MegaClient* getClient();
};

class MegaApiTestDeleter
{
public:
    MegaApiTestDeleter(const std::string& endpointName):
        mEndpointName{endpointName} {};

    MegaApiTestDeleter():
        MegaApiTestDeleter(""){};

    void operator()(MegaApiTest* p) const;

private:
    std::string mEndpointName;
};

// Poor man's expected.
template<typename T>
using Expected = std::variant<Error, T>;

template<typename T>
struct IsExpected: std::false_type
{}; // IsExpected<T>

template<typename T>
struct IsExpected<Expected<T>>: std::true_type
{}; // IsExpected<Expected<T>>

template<typename T>
static constexpr auto IsExpectedV = IsExpected<T>::value;

template<typename T>
using RemoveCVRef = std::remove_cv<std::remove_reference_t<T>>;

template<typename T>
using RemoveCVRefT = typename RemoveCVRef<T>::type;

template<typename T>
Error result(const Expected<T>& expected)
{
    if (auto* result = std::get_if<0>(&expected))
        return *result;

    return API_OK;
}

template<typename T, typename = std::enable_if_t<IsExpectedV<RemoveCVRefT<T>>>>
decltype(auto) value(T&& expected)
{
    assert(result(expected) == API_OK);

    return std::get<1>(std::forward<T>(expected));
}

using MegaApiTestPointer = std::unique_ptr<MegaApiTest, MegaApiTestDeleter>;

// Fixture class with common code for most of tests
class SdkTest:
    public SdkTestBase,
    public SdkTestDataProvider,
    public MegaListener,
    public MegaRequestListener,
    MegaTransferListener,
    MegaLogger
{
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
        bool userFirstNameUpdated = false;
        bool setUpdated;
        bool setElementUpdated;
        bool contactRequestUpdated{false};
        bool accountUpdated;
        bool nodeUpdated; // flag to check specific updates for a node (upon onNodesUpdate)

        // A map to store custom functions to be called inside callbacks
        std::map<MegaHandle, std::weak_ptr<std::function<void()>>> customCallbackCheck;

        bool userAlertsUpdated;
        std::unique_ptr<MegaUserAlertList> userAlertList;

        // unique_ptr to custom functions that will be called upon reception of MegaApi callbacks
        onNodesUpdateCompletion_t mOnNodesUpdateCompletion;

        std::unique_ptr<MegaFolderInfo> mFolderInfo;

        int lastSyncError;
        handle lastSyncBackupId = 0;

#ifdef ENABLE_CHAT
        bool chatUpdated;        // flags to monitor the updates of chats due to actionpackets
        bool schedUpdated;       // flags to monitor the updates of scheduled meetings due to actionpackets
        map<handle, std::unique_ptr<MegaTextChat>> chats;   //  runtime cache of fetched/updated chats
        MegaHandle chatid;          // last chat added
        MegaHandle schedId;         // last scheduled meeting added
#endif

        /**
         * @brief Ensures that the access to the customCallbackCheck map and the posterior function
         * call is properly managed.
         */
        void callCustomCallbackCheck(const MegaHandle userHandle)
        {
            auto it = customCallbackCheck.find(userHandle);
            if (it == customCallbackCheck.end())
            {
                return;
            }
            auto funPtr = it->second.lock();
            if (funPtr)
            {
                (*funPtr)();
            }
            else
            {
                customCallbackCheck.erase(it);
            }
        }

        void receiveEvent(MegaEvent* e)
        {
            if (!e) return;

            lock_guard<mutex> g(getResourceMutex());
            lastEvent.reset(e->copy());
            lastEvents.insert(e->getType());
        }

        void resetlastEvent()
        {
            lock_guard<mutex> g(getResourceMutex());
            lastEvent.reset();
            lastEvents.clear();
        }

        bool lastEventsContain(int type) const
        {
            lock_guard<mutex> g(getResourceMutex());
            return lastEvents.find(type) != lastEvents.end();
        }

        void setSid(const string& s) { sid = s; }
        const string& getSid() const { return sid; }

        void setAttributeValue(const string& v) { attributeValue = v; }
        const string& getAttributeValue() const { return attributeValue; }

        void setChatLink(const string& c) { chatlink = c; }
        const string& getChatLink() const { return chatlink; }

        void setBackupId(MegaHandle b) { mBackupId = b; }
        MegaHandle getBackupId() const { return mBackupId; }

        void setFavNodes(const MegaHandleList* f) { mMegaFavNodeList.reset(f); }
        unsigned getFavNodeCount() const { return mMegaFavNodeList ? mMegaFavNodeList->size() : 0u; }
        MegaHandle getFavNode(unsigned i) const { return mMegaFavNodeList->size() > i ? mMegaFavNodeList->get(i) : INVALID_HANDLE; }

        void setStringLists(const MegaStringListMap* s)
        {
            stringListMap.reset(s);
        }

        unsigned getStringListCount() const
        {
            return stringListMap ? static_cast<unsigned>(stringListMap->size()) : 0u;
        }
        const MegaStringList* getStringList(const char* key) const { return stringListMap ? stringListMap->get(key) : nullptr; }

        void setStringTable(const MegaStringTable* s) { stringTable.reset(s); }
        int getStringTableSize() const { return stringTable ? stringTable->size() : 0; }
        const MegaStringList* getStringTableRow(int i) { return stringTable ? stringTable->get(i) : nullptr; }

    private:
        mutex& getResourceMutex() const
        {
            if (!resourceMtx) resourceMtx.reset(new mutex);
            return *resourceMtx.get();
        } // a single mutex will do fine in tests
        mutable shared_ptr<mutex> resourceMtx;

        shared_ptr<MegaEvent> lastEvent; // not used though; should it be removed?
        set<int> lastEvents;

        // relevant values received in response of requests
        string sid;
        string attributeValue;
        string chatlink;  // not really used anywhere, should it be removed ?
        MegaHandle mBackupId = UNDEF;
        shared_ptr<const MegaStringListMap> stringListMap;
        shared_ptr<const MegaHandleList> mMegaFavNodeList;
        shared_ptr<const MegaStringTable> stringTable;
    };

    std::vector<PerApi> mApi;
    std::vector<MegaApiTestPointer> megaApi;

    m_off_t onTransferStart_progress;
    m_off_t onTransferUpdate_progress;
    m_off_t onTransferUpdate_filesize;
    unsigned onTranferFinishedCount = 0;

    struct SdkTestTransferStats
    {
        m_off_t numFailedRequests{};
        m_off_t numTotalRequests{};
        double failedRequestRatio{};

        SdkTestTransferStats& operator=(const stats::TransferSlotStats& transferSlotStats)
        {
            numFailedRequests = transferSlotStats.mNumFailedRequests;
            numTotalRequests = transferSlotStats.mNumTotalRequests;
            failedRequestRatio = transferSlotStats.failedRequestRatio();
            return *this;
        }
    };

    SdkTestTransferStats onTransferFinish_transferStats{};

protected:
    void SetUp() override;
    void TearDown() override;

    void Cleanup();

    int getApiIndex(MegaApi* api);
    bool getApiIndex(MegaApi* api, size_t& apindex);

    bool checkAlert(int apiIndex, const string& title, const string& path);
    bool checkAlert(int apiIndex, const string& title, handle h, int64_t n = -1, MegaHandle mh = INVALID_HANDLE);

    void testPrefs(const std::string& title, int type);
    void testRecents(const std::string& title, bool useSensitiveExclusion);

#ifdef ENABLE_CHAT
    void cancelSchedMeetings();
#endif

    void syncTestEnsureMyBackupsRemoteFolderExists(unsigned apiIdx);

    void onRequestStart(MegaApi*, MegaRequest*) override {}

    void onRequestUpdate(MegaApi*, MegaRequest*) override {}

    void onRequestFinish(MegaApi* api, MegaRequest* request, MegaError* e) override;

    void onRequestTemporaryError(MegaApi*, MegaRequest*, MegaError*) override {}
    void onTransferStart(MegaApi *api, MegaTransfer *transfer) override;
    void onTransferFinish(MegaApi* api, MegaTransfer *transfer, MegaError* e) override;
    void onTransferUpdate(MegaApi *api, MegaTransfer *transfer) override;

    void onTransferTemporaryError(MegaApi*, MegaTransfer*, MegaError*) override {}
    void onUsersUpdate(MegaApi* api, MegaUserList *users) override;
    void onAccountUpdate(MegaApi *api) override;
    void onNodesUpdate(MegaApi* api, MegaNodeList *nodes) override;
    void onSetsUpdate(MegaApi *api, MegaSetList *sets) override;
    void onSetElementsUpdate(MegaApi *api, MegaSetElementList *elements) override;
    void onContactRequestsUpdate(MegaApi* api, MegaContactRequestList* requests) override;

    void onUserAlertsUpdate(MegaApi* api, MegaUserAlertList* alerts) override;

#ifdef ENABLE_SYNC
    void
        onSyncFileStateChanged(MegaApi*, MegaSync*, string* /*filePath*/, int /*newState*/) override
    {}

    void onSyncStateChanged(MegaApi*, MegaSync*) override {}

    void onSyncRemoteRootChanged(MegaApi*, MegaSync*) override {}

    void onGlobalSyncStateChanged(MegaApi*) override {}
    void purgeVaultTree(unsigned int apiIndex, MegaNode *vault);
#endif
#ifdef ENABLE_CHAT
    void onChatsUpdate(MegaApi *api, MegaTextChatList *chats) override;
#endif
    void onEvent(MegaApi* api, MegaEvent *event) override;

    void resetOnNodeUpdateCompletionCBs();

    onNodesUpdateCompletion_t createOnNodesUpdateLambda(const MegaHandle&, int, bool& flag);
public:
    //void login(unsigned int apiIndex, int timeout = maxTimeout);
    //void loginBySessionId(unsigned int apiIndex, const std::string& sessionId, int timeout = maxTimeout);
    void fetchnodes(unsigned int apiIndex, int timeout = 300);
    void logout(unsigned int apiIndex, bool keepSyncConfigs, int timeout);
    char* dumpSession(unsigned apiIndex = 0);
    void locallogout(unsigned apiIndex = 0);
    void resumeSession(const char *session, unsigned apiIndex = 0);

    void purgeTree(unsigned int apiIndex, MegaNode *p, bool depthfirst = true);

    bool waitForResponse(bool *responseReceived, unsigned int timeout = maxTimeout);

    bool waitForEvent(std::function<bool()> method, unsigned int timeout = maxTimeout);

    static bool WaitFor(const std::function<bool()>& predicate, unsigned timeoutMs);

    bool synchronousRequest(unsigned apiIndex, int type, std::function<void()> f, unsigned int timeout = maxTimeout);
    bool synchronousTransfer(unsigned apiIndex, int type, std::function<void()> f, unsigned int timeout = maxTimeout);

    // *** WARNING *** THESE FUNCTIONS RETURN VALUE ARE SUBJECT TO RACE CONDITIONS
    // convenience functions - template args just make it easy to code, no need to copy all the exact argument types with listener defaults etc. To add a new one, just copy a line and change the flag and the function called.
    // WARNING: any sort of race can result in the lastError being set from some other command - better to use the listener based ones in the next list below
    template<typename... Args>
    int synchronousCatchup(unsigned apiIndex, Args... args)
    {
        synchronousRequest(apiIndex,
                           MegaRequest::TYPE_CATCHUP,
                           [this, apiIndex, args...]()
                           {
                               megaApi[apiIndex]->catchup(args...);
                           });
        return mApi[apiIndex].lastError;
    }

    template<typename... Args>
    int synchronousCreateEphemeralAccountPlusPlus(unsigned apiIndex, Args... args)
    {
        synchronousRequest(apiIndex,
                           MegaRequest::TYPE_CREATE_ACCOUNT,
                           [this, apiIndex, args...]()
                           {
                               megaApi[apiIndex]->createEphemeralAccountPlusPlus(args...);
                           });
        return mApi[apiIndex].lastError;
    }

    template<typename... Args>
    int synchronousResumeCreateAccountEphemeralPlusPlus(unsigned apiIndex, Args... args)
    {
        synchronousRequest(apiIndex,
                           MegaRequest::TYPE_CREATE_ACCOUNT,
                           [this, apiIndex, args...]()
                           {
                               megaApi[apiIndex]->resumeCreateAccountEphemeralPlusPlus(args...);
                           });
        return mApi[apiIndex].lastError;
    }

    template<typename... Args>
    int synchronousCreateAccount(unsigned apiIndex, Args... args)
    {
        synchronousRequest(apiIndex,
                           MegaRequest::TYPE_CREATE_ACCOUNT,
                           [this, apiIndex, args...]()
                           {
                               megaApi[apiIndex]->createAccount(args...);
                           });
        return mApi[apiIndex].lastError;
    }

    template<typename... Args>
    int synchronousResumeCreateAccount(unsigned apiIndex, Args... args)
    {
        synchronousRequest(apiIndex,
                           MegaRequest::TYPE_CREATE_ACCOUNT,
                           [this, apiIndex, args...]()
                           {
                               megaApi[apiIndex]->resumeCreateAccount(args...);
                           });
        return mApi[apiIndex].lastError;
    }
    template<typename ... Args> int synchronousConfirmSignupLink(unsigned apiIndex, Args... args) { synchronousRequest(apiIndex, MegaRequest::TYPE_CONFIRM_ACCOUNT, [this, apiIndex, args...]() { megaApi[apiIndex]->confirmAccount(args...); }); return mApi[apiIndex].lastError; }
    template<typename ... Args> int synchronousFastLogin(unsigned apiIndex, Args... args) { synchronousRequest(apiIndex, MegaRequest::TYPE_LOGIN, [this, apiIndex, args...]() { megaApi[apiIndex]->fastLogin(args...); }); return mApi[apiIndex].lastError; }

    // SMS verification was deprecated. This function should be removed in the future,
    // along with the rest of the code dealing with the deprecated functionality.
    // template<typename ... Args> int synchronousGetCountryCallingCodes(unsigned apiIndex, Args...
    // args) { synchronousRequest(apiIndex, MegaRequest::TYPE_GET_COUNTRY_CALLING_CODES, [this,
    // apiIndex, args...]() { megaApi[apiIndex]->getCountryCallingCodes(args...); }); return
    // mApi[apiIndex].lastError; }

    template<typename... Args>
    int synchronousGetUserAvatar(unsigned apiIndex, Args... args)
    {
        synchronousRequest(apiIndex,
                           MegaRequest::TYPE_GET_ATTR_USER,
                           [this, apiIndex, args...]()
                           {
                               megaApi[apiIndex]->getUserAvatar(args...);
                           });
        return mApi[apiIndex].lastError;
    }

    template<typename... Args>
    int synchronousGetUserAttribute(unsigned apiIndex, Args... args)
    {
        synchronousRequest(apiIndex,
                           MegaRequest::TYPE_GET_ATTR_USER,
                           [this, apiIndex, args...]()
                           {
                               megaApi[apiIndex]->getUserAttribute(args...);
                           });
        return mApi[apiIndex].lastError;
    }
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
    template<typename ... Args> int synchronousSetUserAlias(unsigned apiIndex, Args... args) { synchronousRequest(apiIndex, MegaRequest::TYPE_SET_ATTR_USER, [this, apiIndex, args...]() { megaApi[apiIndex]->setUserAlias(args...); }); return mApi[apiIndex].lastError; }
    template<typename ... Args> int synchronousGetUserAlias(unsigned apiIndex, Args... args) { synchronousRequest(apiIndex, MegaRequest::TYPE_GET_ATTR_USER, [this, apiIndex, args...]() { megaApi[apiIndex]->getUserAlias(args...); }); return mApi[apiIndex].lastError; }
    template<typename ... Args> int synchronousFolderInfo(unsigned apiIndex, Args... args) { synchronousRequest(apiIndex, MegaRequest::TYPE_FOLDER_INFO, [this, apiIndex, args...]() { megaApi[apiIndex]->getFolderInfo(args...); }); return mApi[apiIndex].lastError; }
    // do not add functions using this pattern, see comment at top of this stanza

    // *** USE THESE ONES INSTEAD ***
    // convenience functions - make a request and wait for the result via listener, return the result code.  To add new functions to call, just copy the line
    template<typename... requestArgs>
    std::unique_ptr<RequestTracker> asyncQueryAds(unsigned apiIndex, requestArgs... args)
    {
        auto rt = std::make_unique<RequestTracker>(megaApi[apiIndex].get());
        megaApi[apiIndex]->queryAds(args..., rt.get());
        return rt;
    }

    template<typename... requestArgs>
    std::unique_ptr<RequestTracker> asyncFetchAds(unsigned apiIndex, requestArgs... args)
    {
        auto rt = std::make_unique<RequestTracker>(megaApi[apiIndex].get());
        megaApi[apiIndex]->fetchAds(args..., rt.get());
        return rt;
    }

    template<typename... requestArgs>
    std::unique_ptr<RequestTracker> asyncRequestLogin(unsigned apiIndex, requestArgs... args)
    {
        auto rt = std::make_unique<RequestTracker>(megaApi[apiIndex].get());
        megaApi[apiIndex]->login(args..., rt.get());
        return rt;
    }

    template<typename... requestArgs>
    std::unique_ptr<RequestTracker> asyncRequestFastLogin(unsigned apiIndex, requestArgs... args)
    {
        auto rt = std::make_unique<RequestTracker>(megaApi[apiIndex].get());
        megaApi[apiIndex]->fastLogin(args..., rt.get());
        return rt;
    }

    template<typename... requestArgs>
    std::unique_ptr<RequestTracker> asyncRequestFastLogin(int apiIndex, requestArgs... args)
    {
        auto rt = std::make_unique<RequestTracker>(megaApi[static_cast<size_t>(apiIndex)].get());
        megaApi[static_cast<size_t>(apiIndex)]->fastLogin(args..., rt.get());
        return rt;
    }

    template<typename... requestArgs>
    std::unique_ptr<RequestTracker> asyncRequestFastLogin(MegaApi* api, requestArgs... args)
    {
        auto rt = std::make_unique<RequestTracker>(api);
        api->fastLogin(args..., rt.get());
        return rt;
    }

    template<typename... requestArgs>
    std::unique_ptr<RequestTracker> asyncRequestLoginToFolder(unsigned apiIndex,
                                                              requestArgs... args)
    {
        auto rt = std::make_unique<RequestTracker>(megaApi[apiIndex].get());
        megaApi[apiIndex]->loginToFolder(args..., rt.get());
        return rt;
    }

    template<typename... requestArgs>
    std::unique_ptr<RequestTracker> asyncRequestLoginToFolder(MegaApi* api, requestArgs... args)
    {
        auto rt = std::make_unique<RequestTracker>(api);
        api->loginToFolder(args..., rt.get());
        return rt;
    }

    template<typename... requestArgs>
    std::unique_ptr<RequestTracker> asyncRequestLocalLogout(MegaApi* api, requestArgs... args)
    {
        auto rt = std::make_unique<RequestTracker>(api);
        api->localLogout(args..., rt.get());
        return rt;
    }

    template<typename... requestArgs>
    std::unique_ptr<RequestTracker> asyncRequestFetchnodes(unsigned apiIndex, requestArgs... args)
    {
        auto rt = std::make_unique<RequestTracker>(megaApi[apiIndex].get());
        megaApi[apiIndex]->fetchNodes(args..., rt.get());
        return rt;
    }

    template<typename... requestArgs>
    std::unique_ptr<RequestTracker> asyncRequestFetchnodes(MegaApi* api, requestArgs... args)
    {
        auto rt = std::make_unique<RequestTracker>(api);
        api->fetchNodes(args..., rt.get());
        return rt;
    }

    template<typename... requestArgs>
    std::unique_ptr<RequestTracker> asyncRequestGetVisibleWelcomeDialog(unsigned apiIndex)
    {
        auto rt = std::make_unique<RequestTracker>(megaApi[apiIndex].get());
        megaApi[apiIndex]->getVisibleWelcomeDialog(rt.get());
        return rt;
    }

    template<typename... requestArgs>
    int doGetDeviceName(unsigned apiIndex, string* dvc, requestArgs... args)
    {
        RequestTracker rt(megaApi[apiIndex].get());
        megaApi[apiIndex]->getDeviceName(args..., &rt);
        auto e = rt.waitForResult();
        if (dvc && e == API_OK)
            *dvc = rt.request->getName();
        return e;
    }

    template<typename... requestArgs>
    int doSetDeviceName(unsigned apiIndex, requestArgs... args)
    {
        RequestTracker rt(megaApi[apiIndex].get());
        megaApi[apiIndex]->setDeviceName(args..., &rt);
        return rt.waitForResult();
    }

    template<typename... requestArgs>
    int doGetDriveName(unsigned apiIndex, string* drv, requestArgs... args)
    {
        RequestTracker rt(megaApi[apiIndex].get());
        megaApi[apiIndex]->getDriveName(args..., &rt);
        auto e = rt.waitForResult();
        if (drv && e == API_OK)
            *drv = rt.request->getName();
        return e;
    }

    template<typename... requestArgs>
    int doSetDriveName(unsigned apiIndex, requestArgs... args)
    {
        RequestTracker rt(megaApi[apiIndex].get());
        megaApi[apiIndex]->setDriveName(args..., &rt);
        return rt.waitForResult();
    }

    template<typename... requestArgs>
    int doRequestLogout(unsigned apiIndex, requestArgs... args)
    {
        RequestTracker rt(megaApi[apiIndex].get());
        megaApi[apiIndex]->logout(args..., &rt);
        return rt.waitForResult();
    }

    template<typename... requestArgs>
    int doRequestLocalLogout(unsigned apiIndex, requestArgs... args)
    {
        RequestTracker rt(megaApi[apiIndex].get());
        megaApi[apiIndex]->localLogout(args..., &rt);
        return rt.waitForResult();
    }

    template<typename ... requestArgs> int doStartUpload(unsigned apiIndex, MegaHandle* newNodeHandleResult, requestArgs... args) { TransferTracker tt(megaApi[apiIndex].get()); megaApi[apiIndex]->startUpload(args..., &tt); auto e = tt.waitForResult(); if (newNodeHandleResult) *newNodeHandleResult = tt.resultNodeHandle; return e;}
    template<typename ... requestArgs> int doStartDownload(unsigned apiIndex, requestArgs... args) { TransferTracker tt(megaApi[apiIndex].get()); megaApi[apiIndex]->startDownload(args..., &tt); auto e = tt.waitForResult(); return e;}
    template<typename ... requestArgs> int doSetFileVersionsOption(unsigned apiIndex, requestArgs... args) { RequestTracker rt(megaApi[apiIndex].get()); megaApi[apiIndex]->setFileVersionsOption(args..., &rt); return rt.waitForResult(); }
    template<typename ... requestArgs> int doRemoveVersion(unsigned apiIndex, requestArgs... args) { RequestTracker rt(megaApi[apiIndex].get()); megaApi[apiIndex]->removeVersion(args..., &rt); return rt.waitForResult(); }
    template<typename ... requestArgs> int doRemoveVersions(unsigned apiIndex) { RequestTracker rt(megaApi[apiIndex].get()); megaApi[apiIndex]->removeVersions(&rt); return rt.waitForResult(); }
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
    template<typename ... requestArgs> int doOpenShareDialog(unsigned apiIndex, requestArgs... args) { RequestTracker rt(megaApi[apiIndex].get()); megaApi[apiIndex]->openShareDialog(args..., &rt); rt.waitForResult(); return rt.result; }
    template<typename ... requestArgs> int synchronousDoUpgradeSecurity(unsigned apiIndex, requestArgs... args) { RequestTracker rt(megaApi[apiIndex].get()); megaApi[apiIndex]->upgradeSecurity(args..., &rt); rt.waitForResult(); return rt.result; }
#ifdef ENABLE_SYNC
    template<typename ... requestArgs> int synchronousSyncFolder(unsigned apiIndex, MegaHandle* newSyncRootNodeResult, requestArgs... args)
    {
        RequestTracker rt(megaApi[apiIndex].get());
        megaApi[apiIndex]->syncFolder(args..., &rt);
        rt.waitForResult();
        mApi[apiIndex].lastSyncError = rt.request ? rt.request->getNumDetails() : -888; // request was not set ???
        mApi[apiIndex].lastSyncBackupId = rt.request ? rt.request->getParentHandle() : UNDEF;
        if (newSyncRootNodeResult) *newSyncRootNodeResult = rt.getNodeHandle();
        return rt.result;
    }
    template<typename ... requestArgs> int synchronousRemoveSync(unsigned apiIndex, requestArgs... args) { RequestTracker rt(megaApi[apiIndex].get()); megaApi[apiIndex]->removeSync(args..., &rt); return rt.waitForResult(); }
    template<typename ... requestArgs> int synchronousRemoveBackupNodes(unsigned apiIndex, requestArgs... args) { RequestTracker rt(megaApi[apiIndex].get()); megaApi[apiIndex]->moveOrRemoveDeconfiguredBackupNodes(args..., INVALID_HANDLE, &rt); return rt.waitForResult(); }
    template<typename ... requestArgs> int synchronousSetSyncRunState(unsigned apiIndex, requestArgs... args) { RequestTracker rt(megaApi[apiIndex].get()); megaApi[apiIndex]->setSyncRunState(args..., &rt); rt.waitForResult(); mApi[apiIndex].lastSyncError = rt.request->getNumDetails(); return rt.result; }
#endif // ENABLE_SYNC
    template<typename ... requestArgs> int synchronousKillSession(unsigned apiIndex, requestArgs... args) { RequestTracker rt(megaApi[apiIndex].get()); megaApi[apiIndex]->killSession(args..., &rt); return rt.waitForResult(); }
    template<typename ... requestArgs> int synchronousSetBackup(unsigned apiIndex, OnReqFinish f, requestArgs... args) { RequestTracker rt(megaApi[apiIndex].get(), f);  megaApi[apiIndex]->setBackup(args..., &rt); return rt.waitForResult(); }
    template<typename ... requestArgs> int doExportNode(unsigned apiIndex, requestArgs... args) { RequestTracker rt(megaApi[apiIndex].get()); megaApi[apiIndex]->exportNode(args..., &rt); return rt.waitForResult(); }
    template<typename ... requestArgs> int doDisableExport(unsigned apiIndex, requestArgs... args) { RequestTracker rt(megaApi[apiIndex].get()); megaApi[apiIndex]->disableExport(args..., &rt); return rt.waitForResult(); }
    template<typename ... requestArgs> int synchronousSetNodeFavourite(unsigned apiIndex, requestArgs... args) { RequestTracker rt(megaApi[apiIndex].get());  megaApi[apiIndex]->setNodeFavourite(args..., &rt); return rt.waitForResult(); }
    template<typename ... requestArgs> int synchronousSetNodeLabel(unsigned apiIndex, requestArgs... args) { RequestTracker rt(megaApi[apiIndex].get());  megaApi[apiIndex]->setNodeLabel(args..., &rt); return rt.waitForResult(); }
    template<typename ... requestArgs> int synchronousResetNodeLabel(unsigned apiIndex, requestArgs... args) { RequestTracker rt(megaApi[apiIndex].get());  megaApi[apiIndex]->resetNodeLabel(args..., &rt); return rt.waitForResult(); }
    template<typename ... requestArgs> int synchronousGetFavourites(unsigned apiIndex, requestArgs... args) { RequestTracker rt(megaApi[apiIndex].get());  megaApi[apiIndex]->getFavourites(args..., &rt); return rt.waitForResult(); }
    template<typename ... requestArgs> int synchronousSetNodeSensitive(unsigned apiIndex, requestArgs... args) { RequestTracker rt(megaApi[apiIndex].get());  megaApi[apiIndex]->setNodeSensitive(args..., &rt); return rt.waitForResult(); }
    template<typename ... requestArgs> int synchronousInviteContact(unsigned apiIndex, requestArgs... args) { RequestTracker rt(megaApi[apiIndex].get());  megaApi[apiIndex]->inviteContact(args..., &rt); return rt.waitForResult(); }
    template<typename ... requestArgs> int synchronousReplyContactRequest(unsigned apiIndex, requestArgs... args) { RequestTracker rt(megaApi[apiIndex].get());  megaApi[apiIndex]->replyContactRequest(args..., &rt); return rt.waitForResult(); }
    template<typename ... requestArgs> int synchronousVerifyCredentials(unsigned apiIndex, requestArgs... args) { RequestTracker rt(megaApi[apiIndex].get());  megaApi[apiIndex]->verifyCredentials(args..., &rt); return rt.waitForResult(); }
    template<typename ... requestArgs> int synchronousResetCredentials(unsigned apiIndex, requestArgs... args) { RequestTracker rt(megaApi[apiIndex].get());  megaApi[apiIndex]->resetCredentials(args..., &rt); return rt.waitForResult(); }
    template<typename ... requestArgs> int synchronousRemove(unsigned apiIndex, requestArgs... args) { RequestTracker rt(megaApi[apiIndex].get()); megaApi[apiIndex]->remove(args..., &rt); return rt.waitForResult(); }
    template<typename ... requestArgs> int doCreateSet(unsigned apiIndex, MegaSet** s, requestArgs... args) { RequestTracker rt(megaApi[apiIndex].get()); megaApi[apiIndex]->createSet(args..., &rt); rt.waitForResult(); if (s && rt.request->getMegaSet()) *s = rt.request->getMegaSet()->copy(); return rt.result; }
    template<typename ... requestArgs> int doUpdateSetName(unsigned apiIndex, MegaHandle* id, requestArgs... args) { RequestTracker rt(megaApi[apiIndex].get()); megaApi[apiIndex]->updateSetName(args..., &rt); rt.waitForResult(); if (id) *id = rt.request->getParentHandle(); return rt.result; }
    template<typename ... requestArgs> int doPutSetCover(unsigned apiIndex, MegaHandle* id, requestArgs... args) { RequestTracker rt(megaApi[apiIndex].get()); megaApi[apiIndex]->putSetCover(args..., &rt); rt.waitForResult(); if (id) *id = rt.request->getParentHandle(); return rt.result; }
    template<typename ... requestArgs> int doRemoveSet(unsigned apiIndex, requestArgs... args) { RequestTracker rt(megaApi[apiIndex].get()); megaApi[apiIndex]->removeSet(args..., &rt); rt.waitForResult(); return rt.result; }
    template<typename ... requestArgs> int doCreateBulkSetElements(unsigned apiIndex, MegaSetElementList** els, MegaIntegerList** errs, requestArgs... args)
    {
        RequestTracker rt(megaApi[apiIndex].get());
        megaApi[apiIndex]->createSetElements(args..., &rt);
        rt.waitForResult();
        if (els && rt.request->getMegaSetElementList()) *els = rt.request->getMegaSetElementList()->copy();
        if (errs && rt.request->getMegaIntegerList()) *errs = rt.request->getMegaIntegerList()->copy();
        return rt.result;
    }
    template<typename ... requestArgs> int doCreateSetElement(unsigned apiIndex, MegaSetElementList** ell, requestArgs... args) { RequestTracker rt(megaApi[apiIndex].get()); megaApi[apiIndex]->createSetElement(args..., &rt); rt.waitForResult(); if (ell && rt.request->getMegaSetElementList()) *ell = rt.request->getMegaSetElementList()->copy(); return rt.result; }
    template<typename ... requestArgs> int doUpdateSetElementName(unsigned apiIndex, MegaHandle* eid, requestArgs... args) { RequestTracker rt(megaApi[apiIndex].get()); megaApi[apiIndex]->updateSetElementName(args..., &rt); rt.waitForResult(); if (eid) *eid = rt.request->getParentHandle(); return rt.result; }
    template<typename ... requestArgs> int doUpdateSetElementOrder(unsigned apiIndex, MegaHandle* eid, requestArgs... args) { RequestTracker rt(megaApi[apiIndex].get()); megaApi[apiIndex]->updateSetElementOrder(args..., &rt); rt.waitForResult(); if (eid) *eid = rt.request->getParentHandle(); return rt.result; }
    template<typename ... requestArgs> int doRemoveBulkSetElements(unsigned apiIndex, MegaIntegerList** errs, requestArgs... args)
    {
        RequestTracker rt(megaApi[apiIndex].get());
        megaApi[apiIndex]->removeSetElements(args..., &rt);
        rt.waitForResult();
        if (errs && rt.request->getMegaIntegerList()) *errs = rt.request->getMegaIntegerList()->copy();
        return rt.result;
    }
    template<typename ... requestArgs> int doRemoveSetElement(unsigned apiIndex, requestArgs... args) { RequestTracker rt(megaApi[apiIndex].get()); megaApi[apiIndex]->removeSetElement(args..., &rt); rt.waitForResult(); return rt.result; }
    template<typename ... requestArgs> int doExportSet(unsigned apiIndex, MegaSet** s, string& url, requestArgs... args)
    {
        RequestTracker rt(megaApi[apiIndex].get()); megaApi[apiIndex]->exportSet(args..., &rt); rt.waitForResult();
        if (rt.result == API_OK)
        {
            if (s) *s = rt.request->getMegaSet()->copy();
            if (rt.request->getLink()) url.assign(rt.request->getLink());
        }
        return rt.result;
    }
    template<typename ... requestArgs> int doDisableExportSet(unsigned apiIndex, requestArgs... args) { RequestTracker rt(megaApi[apiIndex].get()); megaApi[apiIndex]->disableExportSet(args..., &rt); rt.waitForResult(); return rt.result; }
    template<typename ... requestArgs> int doFetchPublicSet(unsigned apiIndex, MegaSet** s, MegaSetElementList** els, requestArgs... args)
    {
        RequestTracker rt(megaApi[apiIndex].get());
        megaApi[apiIndex]->fetchPublicSet(args..., &rt); rt.waitForResult();
        if (rt.result == API_OK)
        {
            if (s) *s = rt.request->getMegaSet()->copy();
            if (els) *els = rt.request->getMegaSetElementList()->copy();
        }
        return rt.result;
    }
    template<typename ... requestArgs> int doGetPreviewElementNode(unsigned apiIndex, MegaNode** n, requestArgs... args)
    {
        RequestTracker rt(megaApi[apiIndex].get());
        megaApi[apiIndex]->getPreviewElementNode(args..., &rt); rt.waitForResult();
        if (n && rt.result == API_OK) *n = rt.request->getPublicMegaNode(); // ownership received (it's a copy)
        return rt.result;
    }
    template<typename ... requestArgs> int synchronousCancelTransfers(unsigned apiIndex, requestArgs... args) { RequestTracker rt(megaApi[apiIndex].get()); megaApi[apiIndex]->cancelTransfers(args..., &rt); return rt.waitForResult(); }
    template<typename ... requestArgs> int synchronousSetUserAttribute(unsigned apiIndex, requestArgs... args) { RequestTracker rt(megaApi[apiIndex].get()); megaApi[apiIndex]->setUserAttribute(args..., &rt); return rt.waitForResult(); }
    template<typename ... requestArgs> int synchronousSetAvatar(unsigned apiIndex, requestArgs... args) { RequestTracker rt(megaApi[apiIndex].get()); megaApi[apiIndex]->setAvatar(args..., &rt); return rt.waitForResult(); }
    template<typename ... requestArgs> int synchronousRemoveContact(unsigned apiIndex, requestArgs... args) { RequestTracker rt(megaApi[apiIndex].get()); megaApi[apiIndex]->removeContact(args..., &rt); return rt.waitForResult(); }
    template<typename ... requestArgs> int synchronousShare(unsigned apiIndex, requestArgs... args) { RequestTracker rt(megaApi[apiIndex].get()); megaApi[apiIndex]->share(args..., &rt); return rt.waitForResult(); }
    template<typename ... requestArgs> int synchronousResetPassword(unsigned apiIndex, requestArgs... args) { RequestTracker rt(megaApi[apiIndex].get()); megaApi[apiIndex]->resetPassword(args..., &rt); return rt.waitForResult(); }
    template<typename ... requestArgs> int synchronousConfirmResetPassword(unsigned apiIndex, requestArgs... args) { RequestTracker rt(megaApi[apiIndex].get()); megaApi[apiIndex]->confirmResetPassword(args..., &rt); return rt.waitForResult(); }
    template<typename ... requestArgs> int synchronousGetRecommendedProLevel(unsigned apiIndex, int& recommendedLevel, requestArgs... args) {
        RequestTracker rt(megaApi[apiIndex].get());
        megaApi[apiIndex]->getRecommendedProLevel(args..., &rt);
        int err = rt.waitForResult();
        if (err == API_OK) recommendedLevel = (int)rt.request->getNumber();
        return err;
    }

    template<typename... requestArgs>
    int synchronousChangeEmail(unsigned apiIndex, requestArgs... args)
    {
        RequestTracker rt(megaApi[apiIndex].get());
        megaApi[apiIndex]->changeEmail(args..., &rt);
        return rt.waitForResult();
    }

    template<typename... requestArgs>
    int synchronousConfirmChangeEmail(unsigned apiIndex, requestArgs... args)
    {
        RequestTracker rt(megaApi[apiIndex].get());
        megaApi[apiIndex]->confirmChangeEmail(args..., &rt);
        return rt.waitForResult();
    }
#ifdef ENABLE_SYNC
    template<typename ... requestArgs> int syncMoveToDebris(unsigned apiIndex, requestArgs... args) { RequestTracker rt(megaApi[apiIndex].get()); megaApi[apiIndex]->moveToDebris(args..., &rt); return rt.waitForResult(); }
#endif // ENABLE_SYNC
    template<typename ... requestArgs> int synchronousSetVisibleWelcomeDialog(unsigned apiIndex, requestArgs... args) { RequestTracker rt(megaApi[apiIndex].get()); megaApi[apiIndex]->setVisibleWelcomeDialog(args..., &rt); return rt.waitForResult(); }
    template<typename ... requestArgs> int synchronousCreateNodeTree(unsigned apiIndex, requestArgs... args) { RequestTracker rt(megaApi[apiIndex].get()); megaApi[apiIndex]->createNodeTree(args..., &rt); return rt.waitForResult(); }

    // Checkup methods called from MegaApi callbacks
    void onNodesUpdateCheck(size_t apiIndex, MegaHandle target, MegaNodeList* nodes, int change, bool& flag);

    bool createFile(string filename, bool largeFile = true, string content = "test ");
    int64_t getFilesize(string filename);
    void deleteFile(string filename);
    void deleteFolder(string foldername);

    void fetchNodesForAccounts(const unsigned howMany);
    void getAccountsForTest(unsigned howMany = 1,
                            bool fetchNodes = true,
                            const int clientType = MegaApi::CLIENT_TYPE_DEFAULT);
    void configureTestInstance(unsigned index,
                               const std::string& email,
                               const std::string& pass,
                               bool checkCredentials = true,
                               const int clientType = MegaApi::CLIENT_TYPE_DEFAULT);
    void releaseMegaApi(unsigned int apiIndex);

    void inviteTestAccount(const unsigned invitorIndex, const unsigned inviteIndex, const string &message);
    void inviteContact(unsigned apiIndex, const string &email, const string& message, const int action);
    void replyContact(MegaContactRequest* cr, int action, const unsigned apiIndex = 1);
    int removeContact(unsigned apiIndex, string email);
    void getUserAttribute(MegaUser *u, int type, int timeout = maxTimeout, int accountIndex = 1);

    void verifyCredentials(unsigned apiIndex, string email);
    void resetCredentials(unsigned apiIndex, string email);
    bool areCredentialsVerified(unsigned apiIndex, string email);
    void shareFolder(MegaNode* n, const char* email, int action, unsigned apiIndex = 0);

#ifdef ENABLE_CHAT
    void createChatScheduledMeeting(const unsigned apiIndex, MegaHandle& chatid);
    void updateScheduledMeeting(const unsigned apiIndex, MegaHandle& chatid);
#endif

    string createPublicLink(unsigned apiIndex, MegaNode *n, m_time_t expireDate, int timeout, bool isFreeAccount, bool writable = false, bool megaHosted = false);
    MegaHandle importPublicLink(unsigned apiIndex, string link, MegaNode *parent);
    unique_ptr<MegaNode> getPublicNode(unsigned apiIndex, string link);
    MegaHandle removePublicLink(unsigned apiIndex, MegaNode *n);

    void getContactRequest(unsigned int apiIndex, bool outgoing, int expectedSize = 1);

    MegaHandle createFolder(unsigned int apiIndex, const char *name, MegaNode *parent, int timeout = maxTimeout);

    // SMS verification was deprecated. This function should be removed in the future,
    // along with the rest of the code dealing with the deprecated functionality.
    // void getCountryCallingCodes(int timeout = maxTimeout);

    void explorePath(int account, MegaNode* node, int& files, int& folders);

    void synchronousMediaUpload(unsigned int apiIndex, int64_t fileSize, const char* filename, const char* fileEncrypted, const char* fileOutput, const char* fileThumbnail, const char* filePreview);
    void synchronousMediaUploadIncomplete(unsigned int apiIndex, int64_t fileSize, const char* filename, const char* fileEncrypted, std::string& fingerprint, std::string& string64UploadToken, std::string& string64FileKey);

#ifdef ENABLE_CHAT
    void createChat(bool group, MegaTextChatPeerList *peers, int timeout = maxTimeout);

    /**
     * @brief Creates a chat room from the mApi[creatorIndex] account waiting for all the events to
     * finish before returning. It uses EXPECT in the implementation to check everything finished
     * properly and print error messages in case something is wrong. This means you don't need to
     * call this method with ASSERT_NO_FATAL_FAILURE but you need to check that te return value is
     * not equal to INVALID_HANDLE.
     *
     * @param creatorIndex The index of the account to call the creatChat method from
     * @param invitedIndices A vector with the indices of the accounts that will be invited to the
     * chat. creatorIndex should not be inside the vector.
     * @param group If true a group chat room is created, else a 1on1
     * @param timeout_sec The max time to wait for each response in seconds. 10 minutes by default
     * @return The chatId of the created chat room. INVALID_HANDLE if something went wrong.
     */
    MegaHandle createChatWithChecks(const unsigned int creatorIndex,
                                    const std::vector<unsigned int>& invitedIndices,
                                    const bool group,
                                    const unsigned int timeout_sec = maxTimeout);
#endif

    template<typename... requestArgs>
    ErrorCodes doSetMaxConnections(unsigned apiIndex, requestArgs... args)
    {
        RequestTracker rt(megaApi[apiIndex].get());
        megaApi[apiIndex]->setMaxConnections(args..., &rt);
        return rt.waitForResult();
    }

    /* MegaVpnCredentials */
    template<typename... requestArgs>
    int doGetVpnRegions(unsigned apiIndex,
                        unique_ptr<MegaStringList>& vpnRegions,
                        unique_ptr<MegaVpnRegionList>& vpnRegionsDetailed,
                        requestArgs... args)
    {
        RequestTracker rt(megaApi[apiIndex].get());
        megaApi[apiIndex]->getVpnRegions(args..., &rt);
        auto e = rt.waitForResult();
        auto vpnRegionsFromRequest = rt.request->getMegaStringList() ? rt.request->getMegaStringList()->copy() : nullptr;
        vpnRegions.reset(vpnRegionsFromRequest);
        vpnRegionsDetailed.reset(rt.request->getMegaVpnRegionsDetailed() ?
                                     rt.request->getMegaVpnRegionsDetailed()->copy() :
                                     nullptr);
        return e;
    }
    template<typename ... requestArgs> int doGetVpnCredentials(unsigned apiIndex, unique_ptr<MegaVpnCredentials>& vpnCredentials, requestArgs... args)
    {
        RequestTracker rt(megaApi[apiIndex].get());
        megaApi[apiIndex]->getVpnCredentials(args..., &rt);
        auto e = rt.waitForResult();
        auto vpnCredentialsFromRequest = rt.request->getMegaVpnCredentials() ? rt.request->getMegaVpnCredentials()->copy() : nullptr;
        vpnCredentials.reset(vpnCredentialsFromRequest);
        return e;
    }
    template<typename ... requestArgs> int doPutVpnCredential(unsigned apiIndex, int& slotID, std::string& userPubKey, std::string& newCredential, requestArgs... args)
    {
        RequestTracker rt(megaApi[apiIndex].get());
        megaApi[apiIndex]->putVpnCredential(args..., &rt);
        auto e = rt.waitForResult();
        slotID = static_cast<int>(rt.request->getNumber());
        userPubKey = rt.request->getPassword() ? rt.request->getPassword() : ""; // User Public Key used to register the VPN credentials
        newCredential = rt.request->getSessionKey() ? rt.request->getSessionKey() : ""; // Credential string for conf file
        return e;
    }

    template<typename... requestArgs>
    int doDelVpnCredential(unsigned apiIndex, requestArgs... args)
    {
        RequestTracker rt(megaApi[apiIndex].get());
        megaApi[apiIndex]->delVpnCredential(args..., &rt);
        return rt.waitForResult();
    }

    template<typename... requestArgs>
    int doCheckVpnCredential(unsigned apiIndex, requestArgs... args)
    {
        RequestTracker rt(megaApi[apiIndex].get());
        megaApi[apiIndex]->checkVpnCredential(args..., &rt);
        return rt.waitForResult();
    }

    /* MegaVpnCredentials END */

    auto getAccountLevel(MegaApi& api) -> std::tuple<int, int, int>;

    auto getAccountDetails(MegaApi& api) -> std::tuple<std::unique_ptr<MegaAccountDetails>, int>;

    auto getPricing(MegaApi& api) -> std::tuple<std::unique_ptr<MegaPricing>, int>;

    auto makeScopedAccountLevelRestorer(MegaApi& api);

    template<typename... requestArgs>
    int setAccountLevel(MegaApi& api, requestArgs... args)
    {
        // So we can wait for the client's result.
        RequestTracker tracker(&api);

        // Try and set the user's account level.
        api.sendSetAccountLevelDevCommand(args..., &tracker);

        // Return client's result to caller.
        return tracker.waitForResult();
    }

    template<typename... Arguments>
    int setThumbnail(MegaApi& client, Arguments... arguments)
    {
        RequestTracker tracker(&client);

        client.setThumbnail(arguments..., &tracker);

        return tracker.waitForResult();
    }

    // Checks if a function is a valid invitation list accessor function.
    template<typename Function>
    static constexpr auto IsInvitationListAccessorV =
        std::is_invocable_r_v<const MegaContactRequestList*, Function, const MegaApi*>;

    // Checks if a function is a valid invitation predicate function.
    template<typename Predicate>
    static constexpr auto IsInvitationPredicateV =
        std::is_invocable_r_v<bool, Predicate, const MegaContactRequest&>;

    /**
     * Accept the specified contact invitation.
     *
     * @param client
     * The client that should accept the contact request.
     *
     * @param invitation
     * The invitation the client should accept.
     *
     * @return
     * API_OK if client could accept the invitation.
     */
    Error acceptInvitation(MegaApi& client, const MegaContactRequest& invitation);

    /*
     * Establish a friendship between two clients.
     *
     * @return
     * API_OK if the friendship could be established.
     */
    Error befriend(MegaApi& client0, MegaApi& client1);

    /**
     * Search a list of invitations for an invitation satisfying some predicate.
     *
     * @param invitations
     * The list of invitations to search.
     *
     * @param invitationOK
     * The predicate an invitation must satisfy to be returned.
     *
     * @return
     * The first invitation satisfying predicate, if any.
     */
    template<typename InvitationPredicate>
    auto findInvitation(const MegaContactRequestList& invitations, InvitationPredicate invitationOK)
        -> std::enable_if_t<IsInvitationPredicateV<InvitationPredicate>,
                            std::unique_ptr<MegaContactRequest>>
    {
        // Try and find an invitation satisfying our predicate.
        for (auto i = 0, j = invitations.size(); i < j; ++i)
        {
            // Found a suitable invitation!
            if (auto* invitation = invitations.get(i); std::invoke(invitationOK, *invitation))
            {
                return makeUniqueFrom(invitation->copy());
            }
        }

        // Couldn't find a suitable invitation.
        return nullptr;
    }

    /**
     * Search a list of invitations for an invitation satisfying some predicate.
     *
     * @param getInvitations
     * An accessor specifying which invitation list we want to search.
     *
     * @param invitationOK
     * The predicate an invitation must satisfy to be returned.
     *
     * @return
     * The first invitation satisfying predicate, if any.
     */
    template<typename InvitationListAccessor, typename InvitationPredicate>
    auto findInvitation(MegaApi& client,
                        InvitationListAccessor getInvitations,
                        InvitationPredicate invitationOK)
        -> std::enable_if_t<IsInvitationListAccessorV<InvitationListAccessor> &&
                                IsInvitationPredicateV<InvitationPredicate>,
                            std::unique_ptr<MegaContactRequest>>
    {
        // Get invitation list.
        auto* invitations = std::invoke(getInvitations, &client);

        // No invitations.
        if (!invitations || invitations->size() == 0)
        {
            return nullptr;
        }

        // Try and find an invitation satisfying our predicate.
        return findInvitation(*invitations, std::move(invitationOK));
    }

    /**
     * Check if email is a contact.
     *
     * @param client
     * The client (user) whose contacts we will search.
     *
     * @param email
     * The contact we are searching for.
     */
    auto hasContact(MegaApi& client, const std::string& email) -> std::unique_ptr<MegaUser>;

    /**
     * Check if a client has received a contact invite from some user.
     *
     * @apram client
     * The client that should've received an invitation.
     *
     * @param email
     * The user that should've sent us the invitation.
     *
     * @return
     * A matching invitation, if found.
     */
    auto hasReceivedInvitationFrom(MegaApi& client, const std::string& email)
        -> std::unique_ptr<MegaContactRequest>;

    /*
     * Check if this client has sent a contact invite to some user.
     *
     * @param client
     * The client that should've sent the invitation.
     *
     * @param email
     * The user that we've sent the invitation to.
     *
     * @return
     * A matching invitation, if found.
     */
    auto hasSentInvitationTo(MegaApi& client, const std::string& email)
        -> std::unique_ptr<MegaContactRequest>;

    /**
     * Remove a contact.
     *
     * @param client
     * The client that wants to remove this contact.
     *
     * @param email
     * The contact we want to remove.
     *
     * @returns
     * API_OK if the contact was removed.
     */
    Error removeContact(MegaApi& client, const std::string& email);

    /**
     * Break a contact relationship between two clients.
     *
     * @return
     * API_ENOENT if no contact relationship exists.
     * API_OK if the contact relationship was successfully broken.
     */
    Error removeContact(MegaApi& client0, MegaApi& client1);

    /**
     * Send a contact invitation from one user to another.
     *
     * @param client
     * The client that should send the invitation.
     *
     * @param email
     * The user that we are inviting to be a contact.
     *
     * @return
     * API_OK if the invitation was sent successfully.
     */
    Error sendInvitationTo(MegaApi& client, const std::string& email);

    /**
     * Send a contact invitation from one client to another.
     *
     * @param client0
     * The client that will be sending the invitation.
     *
     * @param client1
     * The client that will be receiving the invitation.
     *
     * @return
     * {invitation, API_OK} on success.
     * {nullptr, ?} on failure.
     */
    using SendInvitationToResult = std::pair<std::unique_ptr<MegaContactRequest>, Error>;

    auto sendInvitationTo(MegaApi& client0, MegaApi& client1) -> SendInvitationToResult;

    /**
     * @brief Returns the current tests suite and case name.
     * @example calling this inside `TEST_F(SdkTest, Foo)` returns `std::pair {"SdkTest", "Foo"}`
     */
    std::pair<std::string, std::string> getTestSuiteAndName() const;

    /**
     * @brief Returns an identifier of the current test. It can be used to prefix log messages.
     * @example calling this inside `TEST_F(SdkTest, Foo)` returns `"SdkTest.Foo : "`
     */
    std::string getLogPrefix() const;

    /**
     * @brief Gets a file name prefix unique for the test case
     * @example calling this inside `TEST_F(SdkTest, Foo)` returns `"SdkTest_Foo_"`
     */
    std::string getFilePrefix() const;
};

/**
 * @brief
 * Create a directory with a given name under a specified parent.
 *
 * @param client
 * The client who should create the directory.
 *
 * @param parent
 * The directory where our new directory will live.
 *
 * @param name
 * The name of the directory we are creating.
 *
 * @return
 * An Error if the directory couldn't be created.
 * An std::unique_ptr<MegaNode> if the directory was created.
 */
auto createDirectory(MegaApi& client, const MegaNode& parent, const std::string& name)
    -> Expected<std::unique_ptr<MegaNode>>;

/**
 * @brief
 * Export the specified node.
 *
 * @param client
 * The client that should export the node.
 *
 * @param node
 * The node we want to export.
 *
 * @param expirationDate
 * When should the node's resulting public link expire?
 *
 * @return
 * A string (the node's public link) if the node could be exported
 * An error if the node couldn't be exported.
 */
auto exportNode(MegaApi& client,
                const MegaNode& node,
                std::optional<std::int64_t> expirationDate = std::nullopt) -> Expected<std::string>;

/**
 * Import a node into this account via public link.
 *
 * @param client
 * The client who is importing the node.
 *
 * @param link
 * The public link of the node we want to import.
 *
 * @param parent
 * Where the node should be imported.
 *
 * @return
 * An Error if the node could not be imported.
 * An std::unique_ptr<MegaNode> if the node was imported.
 */
auto importNode(MegaApi& client, const std::string& link, const MegaNode& parent)
    -> Expected<std::unique_ptr<MegaNode>>;
