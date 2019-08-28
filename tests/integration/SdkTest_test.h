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
static const string USER_AGENT  = "Integration Tests with GoogleTest framework";

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

class MegaLoggerSDK : public MegaLogger {

public:
    MegaLoggerSDK(const char *filename);
    ~MegaLoggerSDK();

private:
    std::ofstream sdklog;

protected:
    void log(const char *time, int loglevel, const char *source, const char *message);
};

struct TransferTracker : public ::mega::MegaTransferListener
{
    std::atomic<bool> started = { false };
    std::atomic<bool> finished = { false };
    std::atomic<int> result = { INT_MAX };
    std::promise<int> promiseResult;
    void onTransferStart(MegaApi *api, MegaTransfer *transfer) override
    {
        started = true;
    }
    void onTransferFinish(MegaApi* api, MegaTransfer *transfer, MegaError* error) override
    {
        result = error->getErrorCode();
        finished = true;
        promiseResult.set_value(result);
    }
    int waitForResult()
    {
        return promiseResult.get_future().get();
    }
};

struct RequestTracker : public ::mega::MegaRequestListener
{
    std::atomic<bool> started = { false };
    std::atomic<bool> finished = { false };
    std::atomic<int> result = { INT_MAX };
    std::promise<int> promiseResult;
    void onRequestStart(MegaApi* api, MegaRequest *request) override
    {
        started = true;
    }
    void onRequestFinish(MegaApi* api, MegaRequest *request, MegaError* e) override
    {
        result = e->getErrorCode();
        finished = true;
        promiseResult.set_value(result);
    }
    int waitForResult()
    {
        return promiseResult.get_future().get();
    }
};

// Fixture class with common code for most of tests
class SdkTest : public ::testing::Test, public MegaListener, MegaRequestListener, MegaTransferListener, MegaLogger {

public:
    MegaApi* megaApi[2];
    string email[2];
    string pwd[2];

    int lastError[2];

    // flags to monitor the completion of requests/transfers
    bool requestFlags[2][MegaRequest::TOTAL_OF_REQUEST_TYPES];
    bool transferFlags[2][MegaTransfer::TYPE_LOCAL_HTTP_DOWNLOAD];

    // relevant values received in response of requests
    MegaHandle h;
    string link;
    MegaNode *publicNode;
    string attributeValue;
    string sid;
    std::unique_ptr<MegaStringListMap> stringListMap;
    std::unique_ptr<MegaStringTable> stringTable;

    MegaContactRequest* cr[2];

    // flags to monitor the updates of nodes/users/PCRs due to actionpackets
    bool nodeUpdated[2];
    bool userUpdated[2];
    bool contactRequestUpdated[2];
    bool accountUpdated[2];

#ifdef ENABLE_CHAT
    bool chatUpdated[2];        // flags to monitor the updates of chats due to actionpackets
    map<handle, MegaTextChat*> chats;   //  runtime cache of fetched/updated chats
    MegaHandle chatid;          // last chat added
#endif

    MegaLoggerSDK *logger;

    m_off_t onTransferUpdate_progress;
    m_off_t onTransferUpdate_filesize;


protected:
    virtual void SetUp();
    virtual void TearDown();

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
    void onAccountUpdate(MegaApi *api);
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

public:
    void login(unsigned int apiIndex, int timeout = maxTimeout);
    void loginBySessionId(unsigned int apiIndex, const std::string& sessionId, int timeout = maxTimeout);
    void fetchnodes(unsigned int apiIndex, int timeout = maxTimeout);
    void logout(unsigned int apiIndex, int timeout = maxTimeout);
    char* dumpSession();
    void locallogout(int timeout = maxTimeout);
    void resumeSession(const char *session, int timeout = maxTimeout);

    void purgeTree(MegaNode *p);
    bool waitForResponse(bool *responseReceived, unsigned int timeout = maxTimeout);

    bool synchronousCall(bool &responseFlag, std::function<void()> f, unsigned int timeout = maxTimeout);

    // convenience functions - template args just make it easy to code, no need to copy all the exact argument types with listener defaults etc. To add a new one, just copy a line and change the flag and the function called.
    template<typename ... uploadArgs> int synchronousUpload(int apiIndex, uploadArgs... args) { synchronousCall(transferFlags[apiIndex][MegaTransfer::TYPE_UPLOAD], [this, apiIndex, args...]() { megaApi[apiIndex]->startUpload(args...); }); return lastError[apiIndex]; }
    template<typename ... uploadArgs> int synchronousCatchup(int apiIndex, uploadArgs... args) { synchronousCall(requestFlags[apiIndex][MegaRequest::TYPE_CATCHUP], [this, apiIndex, args...]() { megaApi[apiIndex]->catchup(args...); }); return lastError[apiIndex]; }


    // convenience functions - make a request and wait for the result via listener, return the result code.  To add new functions to call, just copy the line
    template<typename ... requestArgs> int doRequestLogout(int apiIndex, requestArgs... args) { RequestTracker rt; megaApi[apiIndex]->logout(args..., &rt); return rt.waitForResult(); }

    void createFile(string filename, bool largeFile = true);
    size_t getFilesize(string filename);
    void deleteFile(string filename);

    void getMegaApiAux();
    void releaseMegaApi(unsigned int apiIndex);

    void inviteContact(string email, string message, int action, int timeout = maxTimeout);
    void replyContact(MegaContactRequest *cr, int action, int timeout = maxTimeout);
    void removeContact(string email, int timeout = maxTimeout);
    void setUserAttribute(int type, string value, int timeout = maxTimeout);
    void getUserAttribute(MegaUser *u, int type, int timeout = maxTimeout, int accountIndex = 1);

    void shareFolder(MegaNode *n, const char *email, int action, int timeout = maxTimeout);

    void createPublicLink(unsigned apiIndex, MegaNode *n, m_time_t expireDate = 0, int timeout = maxTimeout);
    void importPublicLink(unsigned apiIndex, string link, MegaNode *parent, int timeout = maxTimeout);
    void getPublicNode(unsigned apiIndex, string link, int timeout = maxTimeout);
    void removePublicLink(unsigned apiIndex, MegaNode *n, int timeout = maxTimeout);

    void getContactRequest(unsigned int apiIndex, bool outgoing, int expectedSize = 1);

    void createFolder(unsigned int apiIndex, char * name, MegaNode *n, int timeout = maxTimeout);

    void getRegisteredContacts(const std::map<std::string, std::string>& contacts, int timeout = maxTimeout);

    void getCountryCallingCodes(int timeout = maxTimeout);

#ifdef ENABLE_CHAT
    void createChat(bool group, MegaTextChatPeerList *peers, int timeout = maxTimeout);
#endif
};
