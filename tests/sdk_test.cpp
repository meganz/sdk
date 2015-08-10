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

// IMPORTANT: the account must be empty (Cloud & Rubbish) before start the test
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

            login();
            fetchnodes();
        }
    }

    virtual void TearDown()
    {
        // do some cleanup

        // Remove nodes in Cloud & Rubbish
        purgeTree(megaApi->getRootNode());
        purgeTree(megaApi->getRubbishNode());

        logout(10);
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

    void purgeTree(MegaNode *p)
    {
        MegaNodeList *children;
        children = megaApi->getChildren(p);

        for (int i = 0; i < children->size(); i++)
        {
            megaApi->remove(children->get(i));
        }
    }

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

TEST_F(SdkTest, SdkTestResumeSession)
{
    char *session = dumpSession();
    locallogout();

    resumeSession(session);

    delete session;
}

// create, rename, copy, move, remove, children, child by name, node by fingerprint, node by path
TEST_F(SdkTest, SdkTestNodeOperations)
{
    // --- Create a new folder ---

    MegaNode *rootnode = megaApi->getRootNode();
    char name1[64] = "New folder";

    responseReceived = false;
    megaApi->createFolder(name1, rootnode);
    waitForResponse(&responseReceived);

    ASSERT_EQ(MegaError::API_OK, lastError) << "Cannot create a folder (error: " << lastError << ")";


    // --- Rename a node ---

    MegaNode *n1 = megaApi->getNodeByHandle(h);
    strcpy(name1, "Folder renamed");

    responseReceived = false;
    megaApi->renameNode(n1, name1);
    waitForResponse(&responseReceived);

    ASSERT_EQ(MegaError::API_OK, lastError) << "Cannot rename a node (error: " << lastError << ")";


    // --- Copy a node ---

    MegaNode *n2;
    char name2[64] = "Folder copy";

    responseReceived = false;
    megaApi->copyNode(n1, rootnode, name2);
    waitForResponse(&responseReceived);

    ASSERT_EQ(MegaError::API_OK, lastError) << "Cannot create a copy of a node (error: " << lastError << ")";
    n2 = megaApi->getNodeByHandle(h);


    // --- Get child nodes ---

    MegaNodeList *children;
    children = megaApi->getChildren(rootnode);

    EXPECT_EQ(2, children->size()) << "Wrong number of child nodes";
    EXPECT_STREQ(name2, children->get(0)->getName()) << "Wrong name of child node"; // "Folder copy"
    EXPECT_STREQ(name1, children->get(1)->getName()) << "Wrong name of child node"; // "Folder rename"

    delete children;


    // --- Get child node by name ---

    MegaNode *n3;
    n3 = megaApi->getChildNode(rootnode, name2);

    bool null_pointer = (n3 == NULL);
    EXPECT_FALSE(null_pointer) << "Child node by name not found";
//    ASSERT_EQ(n2->getHandle(), n3->getHandle());  This test may fail due to multiple nodes with the same name


    // --- Get node by fingerprint ---

    char *fingerprint = megaApi->getFingerprint(n2);
    MegaNode *n4;
    n4 = megaApi->getNodeByFingerprint(fingerprint);

    null_pointer = (n4 == NULL);
    EXPECT_FALSE(null_pointer) << "Node by fingerprint not found";
//    ASSERT_EQ(n2->getHandle(), n4->getHandle());  This test may fail due to multiple nodes with the same name
    delete fingerprint;


    // --- Get node by path ---

    char path[128] = "/Folder copy";
    MegaNode *n5;
    n5 = megaApi->getNodeByPath(path);

    null_pointer = (n5 == NULL);
    EXPECT_FALSE(null_pointer) << "Node by path not found";


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
}

TEST_F(SdkTest, SdkTestTransfers)
{
    // --- Upload a file ---




    // --- Download a file ---


    // --- Pause a transfer ---

    // megaApi->areTransfersPaused(MegaTransfer::TYPE_DOWNLOAD);
    // megaApi->areTransfersPaused(MegaTransfer::TYPE_UPLOAD);

    // --- Resume a transfer ---


    // --- Cancel a transfer ---
    // megaApi->cancelTransfer(transfer);
    // or
    // megaApi->cancelTransfers(type);
}

TEST_F(SdkTest, SdkTestShares)
{
    // --- Create a new outgoing share ---


    // --- Modify the access level of an outgoing share ---


    // --- Check access level of a node ---
    // megaApi->checkAccess(level);


    // --- Revoke access to an outgoing share ---


    // --- Receive a new incoming share ---
    // megaApi->getInShares(user);
    // megaApi->getInShares();   // from all the users


    // --- Create a public link ---
    // megaApi->exportNode(node);


    // --- Remove a public link ---
    // megaApi->disableExport(node);
}

TEST_F(SdkTest, SdkTestContacts)
{
    // --- Check my email ---
    // megaApi->getMyEmail();


    // --- Add contact ---


    // --- Get incoming contacts ---
    // megaApi->getIncomingContactRequests();


    // --- Accept contact ---


    // --- Deny contact ---


    // --- Remove contact ---


    // --- Get contact ---
    // megaApi->getContact(email);
    // megaApi->getContacts();
}
