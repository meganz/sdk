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

using namespace mega;
using ::testing::Test;

static const string APP_KEY     = "8QxzVRxD";
static const string USER_AGENT  = "Unit Tests with GoogleTest framework";

static const string EMAIL       = "megasdktest@yopmail.com";
static const string PWD         = "megasdktest??";

static const unsigned int pollingT = 500000;  // (microseconds) to check if response from server is received

// Fixture class with common code for most of tests
class SdkTest : public ::testing::Test, public MegaListener, MegaRequestListener {

public:
    MegaApi *megaApi = NULL;

    int lastError;

    bool loggingReceived;
    bool fetchnodesReceived;
    bool logoutReceived;
    bool responseReceived;

    MegaHandle h;

private:


protected:
    virtual void SetUp()
    {
        // do some initialization

        if (megaApi == NULL)
        {
            char path[1024];
            getcwd(path, sizeof path);
            megaApi = new MegaApi(APP_KEY.c_str(), path, USER_AGENT.c_str());

            megaApi->addListener(this);
        }
    }

    virtual void TearDown()
    {
        // do some cleanup

        delete megaApi;
    }

    void onRequestStart(MegaApi *api, MegaRequest *request) {}
    void onRequestUpdate(MegaApi*api, MegaRequest *request) {}
    void onRequestFinish(MegaApi *api, MegaRequest *request, MegaError *e)
    {
        lastError = e->getErrorCode();

        switch(request->getType())
        {
        case MegaRequest::TYPE_LOGIN:
            loggingReceived = true;
            break;

        case MegaRequest::TYPE_FETCH_NODES:
            fetchnodesReceived = true;
            break;

        case MegaRequest::TYPE_LOGOUT:
            logoutReceived = true;
            break;

        case MegaRequest::TYPE_CREATE_FOLDER:
            responseReceived = true;
            h = request->getNodeHandle();
            break;

        case MegaRequest::TYPE_RENAME:
            responseReceived = true;
            break;

        case MegaRequest::TYPE_COPY:
            responseReceived = true;
            h = request->getNodeHandle();
            break;

        case MegaRequest::TYPE_MOVE:
            responseReceived = true;
            break;

        case MegaRequest::TYPE_REMOVE:
            responseReceived = true;
            break;

        }
    }
    void onRequestTemporaryError(MegaApi *api, MegaRequest *request, MegaError* error) {}
    void onTransferStart(MegaApi *api, MegaTransfer *transfer) {}
    void onTransferFinish(MegaApi* api, MegaTransfer *transfer, MegaError* error) {}
    void onTransferUpdate(MegaApi *api, MegaTransfer *transfer) {}
    void onTransferTemporaryError(MegaApi *api, MegaTransfer *transfer, MegaError* error) {}
    void onUsersUpdate(MegaApi* api, MegaUserList *users) {}
    void onNodesUpdate(MegaApi* api, MegaNodeList *nodes) {}
    void onAccountUpdate(MegaApi *api) {}
    void onContactRequestsUpdate(MegaApi* api, MegaContactRequestList* requests) {}
    void onReloadNeeded(MegaApi *api) {}
#ifdef ENABLE_SYNC
    void onSyncFileStateChanged(MegaApi *api, MegaSync *sync, const char *filePath, int newState) {}
    void onSyncEvent(MegaApi *api, MegaSync *sync,  MegaSyncEvent *event) {}
    void onSyncStateChanged(MegaApi *api,  MegaSync *sync) {}
    void onGlobalSyncStateChanged(MegaApi* api) {}
#endif

public:
    void login(int timeout = 0)   // Seconds to wait for response. 0 means no timeout
    {
        loggingReceived = false;

        megaApi->login(EMAIL.data(), PWD.data());

        waitForResponse(&loggingReceived, timeout);

        if (timeout)
        {
            ASSERT_TRUE(loggingReceived) << "Logging failed after " << timeout  << " seconds";
        }

        ASSERT_EQ(MegaError::API_OK, lastError) << "Logging failed (error: " << lastError << ")";
    }

    void fetchnodes(int timeout = 0)   // t: seconds to wait for response. 0 means no timeout
    {
        fetchnodesReceived = false;

        megaApi->fetchNodes(this);

        waitForResponse(&fetchnodesReceived, timeout);

        if (timeout)
        {
            ASSERT_TRUE(fetchnodesReceived) << "Fetchnodes failed after " << timeout  << " seconds";
        }

        ASSERT_EQ(MegaError::API_OK, lastError) << "Fetchnodes failed (error: " << lastError << ")";
    }

    void logout(int timeout = 0)
    {
        logoutReceived = false;

        megaApi->logout(this);

        waitForResponse(&logoutReceived, timeout);

        if (timeout)
        {
            EXPECT_TRUE(logoutReceived) << "Logout failed after " << timeout  << " seconds";
        }

        EXPECT_EQ(MegaError::API_OK, lastError) << "Logout failed (error: " << lastError << ")";
    }

    char* dumpSession()
    {
        return megaApi->dumpSession();
    }

    void locallogout(int timeout = 0)
    {
        logoutReceived = false;

        megaApi->localLogout(this);

        waitForResponse(&logoutReceived, timeout);

        if (timeout)
        {
            EXPECT_TRUE(logoutReceived) << "Local logout failed after " << timeout  << " seconds";
        }

        EXPECT_EQ(MegaError::API_OK, lastError) << "Local logout failed (error: " << lastError << ")";
    }

    void resumeSession(char *session, int timeout = 0)
    {
        loggingReceived = false;

        megaApi->fastLogin(session, this);

        waitForResponse(&loggingReceived, timeout);

        if (timeout)
        {
            ASSERT_TRUE(loggingReceived) << "Resume session failed after " << timeout  << " seconds";
        }

        ASSERT_EQ(MegaError::API_OK, lastError) << "Resume session failed (error: " << lastError << ")";
    }

public:
    void waitForResponse(bool *responseReceived, int timeout = 0)
    {
        timeout *= 1000000; // convert to micro-seconds
        int tWaited = 0;    // microseconds
        while(!(*responseReceived))
        {
            usleep(pollingT);

            if (timeout)
            {
                tWaited += pollingT;
                if (tWaited >= timeout)
                {
                    break;
                }
            }
        }
    }

};

///////////////////////////__ Tests using SdkTest __//////////////////////////////////

TEST_F(SdkTest, SdkTestLogin)
{
    login(5);
}

TEST_F(SdkTest, SdkTestFetchnodes)
{
    login(5);
    fetchnodes(30);
}

TEST_F(SdkTest, SdkTestResumeSession)
{
    login();
    fetchnodes();

    char *session = dumpSession();
    locallogout();

    resumeSession(session);

    delete session;

    logout();
}

// create, rename, copy, move, remove
TEST_F(SdkTest, SdkTestNodeOperations)
{
    login();
    fetchnodes();


    // --- Create a new folder ---

    MegaNode *rootnode = megaApi->getRootNode();
    char name[64] = "New folder";

    responseReceived = false;
    megaApi->createFolder(name, rootnode);
    waitForResponse(&responseReceived);

    ASSERT_EQ(MegaError::API_OK, lastError) << "Cannot create a folder (error: " << lastError << ")";


    // --- Rename a node ---

    MegaNode *n1 = megaApi->getNodeByHandle(h);
    strcpy(name, "Folder renamed");

    responseReceived = false;
    megaApi->renameNode(n1, name);
    waitForResponse(&responseReceived);

    ASSERT_EQ(MegaError::API_OK, lastError) << "Cannot rename a node (error: " << lastError << ")";


    // --- Copy a node ---

    MegaNode *n2;
    strcpy(name, "Folder copy");

    responseReceived = false;
    megaApi->copyNode(n1, rootnode, name);
    waitForResponse(&responseReceived);

    ASSERT_EQ(MegaError::API_OK, lastError) << "Cannot create a copy of a node (error: " << lastError << ")";
    n2 = megaApi->getNodeByHandle(h);


    // --- Move a node ---

    responseReceived = false;
    megaApi->moveNode(n1, n2);
    waitForResponse(&responseReceived);

    ASSERT_EQ(MegaError::API_OK, lastError) << "Cannot move node (error: " << lastError << ")";


    // --- Send to Rubbish bin ---

    responseReceived = false;
    megaApi->moveNode(n2, megaApi->getRubbishNode());
    waitForResponse(&responseReceived);

    ASSERT_EQ(MegaError::API_OK, lastError) << "Cannot move node to Rubbish bin (error: " << lastError << ")";


    // --- Remove a node ---

    responseReceived = false;
    megaApi->remove(n2);
    waitForResponse(&responseReceived);

    ASSERT_EQ(MegaError::API_OK, lastError) << "Cannot remove a node (error: " << lastError << ")";


    logout();
}
