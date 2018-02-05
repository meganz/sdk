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

#include <iostream>
#include <fstream>

using namespace mega;
using ::testing::Test;

static const string APP_KEY     = "8QxzVRxD";
static const string USER_AGENT  = "Unit Tests with GoogleTest framework";

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

static bool testingInvalidArgs = false;

class MegaLoggerSDK : public MegaLogger {

public:
    MegaLoggerSDK(const char *filename);
    ~MegaLoggerSDK();

private:
    ofstream sdklog;

protected:
    void log(const char *time, int loglevel, const char *source, const char *message);
};

// Fixture class with common code for most of tests
class SdkTest : public ::testing::Test, public MegaListener, MegaRequestListener, MegaTransferListener, MegaLogger {

public:
    MegaApi* megaApi[2];
    string email[2];
    string pwd[2];

    int lastError[2];

    // flags to monitor the completion of requests/transfers
    bool requestFlags[2][MegaRequest::TYPE_SET_PROXY];
    bool transferFlags[2][MegaTransfer::TYPE_LOCAL_HTTP_DOWNLOAD];

    // relevant values received in response of requests
    MegaHandle h;
    string link;
    MegaNode *publicNode;
    string attributeValue;
    string sid;

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

protected:
    virtual void SetUp();
    virtual void TearDown();

    void onRequestStart(MegaApi *api, MegaRequest *request) {}
    void onRequestUpdate(MegaApi*api, MegaRequest *request) {}
    void onRequestFinish(MegaApi *api, MegaRequest *request, MegaError *e);
    void onRequestTemporaryError(MegaApi *api, MegaRequest *request, MegaError* error) {}
    void onTransferStart(MegaApi *api, MegaTransfer *transfer) { }
    void onTransferFinish(MegaApi* api, MegaTransfer *transfer, MegaError* e);
    void onTransferUpdate(MegaApi *api, MegaTransfer *transfer) {}
    void onTransferTemporaryError(MegaApi *api, MegaTransfer *transfer, MegaError* error) {}
    void onUsersUpdate(MegaApi* api, MegaUserList *users);
    void onNodesUpdate(MegaApi* api, MegaNodeList *nodes);
    void onAccountUpdate(MegaApi *api);
    void onContactRequestsUpdate(MegaApi* api, MegaContactRequestList* requests);
    void onReloadNeeded(MegaApi *api) {}
#ifdef ENABLE_SYNC
    void onSyncFileStateChanged(MegaApi *api, MegaSync *sync, const char *filePath, int newState) {}
    void onSyncEvent(MegaApi *api, MegaSync *sync,  MegaSyncEvent *event) {}
    void onSyncStateChanged(MegaApi *api,  MegaSync *sync) {}
    void onGlobalSyncStateChanged(MegaApi* api) {}
#endif
#ifdef ENABLE_CHAT
    void onChatsUpdate(MegaApi *api, MegaTextChatList *chats);
#endif

public:
    void login(unsigned int apiIndex, int timeout = maxTimeout);
    void fetchnodes(unsigned int apiIndex, int timeout = maxTimeout);
    void logout(unsigned int apiIndex, int timeout = maxTimeout);
    char* dumpSession();
    void locallogout(int timeout = maxTimeout);
    void resumeSession(char *session, int timeout = maxTimeout);

    void purgeTree(MegaNode *p);
    bool waitForResponse(bool *responseReceived, unsigned int timeout = maxTimeout);

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

    void createPublicLink(MegaNode *n, m_time_t expireDate = 0, int timeout = maxTimeout);
    void importPublicLink(string link, MegaNode *parent, int timeout = maxTimeout);
    void getPublicNode(string link, int timeout = maxTimeout);
    void removePublicLink(MegaNode *n, int timeout = maxTimeout);

    void getContactRequest(unsigned int apiIndex, bool outgoing, int expectedSize = 1);

    void createFolder(unsigned int apiIndex, char * name, MegaNode *n, int timeout = maxTimeout);

#ifdef ENABLE_CHAT
    void createChat(bool group, MegaTextChatPeerList *peers, int timeout = maxTimeout);
#endif
};
