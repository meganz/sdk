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
#include "gtest/gtest.h"

using namespace mega;
using ::testing::Test;

static const string APP_KEY     = "8QxzVRxD";
static const string USER_AGENT  = "Unit Tests with GoogleTest framework";

static const string EMAIL       = "megasdktest@yopmail.com";
static const string PWD         = "megasdktest??";

static const unsigned int pollingT = 1;  // (seconds) to check if response from server is received

// Fixture class with common code for most of tests
class SdkTest : public ::testing::Test, public MegaListener, MegaRequestListener {

public:
    MegaApi *megaApi = NULL;

    int lastError;
    int timeout;

    bool loggingReceived;
    bool fetchnodesReceived;

private:


protected:
    virtual void SetUp()
    {
        // do some initialization

        if (megaApi == NULL)
        {
            char buf[1024];
            getcwd(buf, sizeof buf);
            megaApi = new MegaApi(APP_KEY.c_str(), buf, USER_AGENT.c_str());

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
    void login(int t)   // t: seconds to wait for response
    {
        timeout = t;
        loggingReceived = false;

        megaApi->login(EMAIL.data(), PWD.data());

        for(int i = 0; !loggingReceived && i < timeout; i++)
            sleep(pollingT);

        ASSERT_TRUE(loggingReceived) << "Logging failed after " << timeout  << " seconds";
        ASSERT_EQ(MegaError::API_OK, lastError) << "Logging failed (error: " << lastError << ")";
    }

    void fetchnodes(int t)   // t: seconds to wait for response
    {
        timeout = t;
        fetchnodesReceived = false;

        megaApi->fetchNodes(this);

        for(int i = 0; !fetchnodesReceived && i < timeout; i++)
            sleep(pollingT);

        ASSERT_TRUE(fetchnodesReceived) << "Logging failed after " << timeout  << " seconds";
        ASSERT_EQ(MegaError::API_OK, lastError) << "Fetchnodes failed (error: " << lastError << ")";
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

