/**
 * @file sdk_test_lockless_cs_channel.cpp
 * @brief This file defines tests for the lockless CS channel
 */

#include "env_var_accounts.h"
#include "integration_test_utils.h"
#include "mega/scoped_helpers.h"
#include "mega/testhooks.h"
#include "mock_listeners.h"
#include "sdk_test_utils.h"
#include "SdkTestNodesSetUp.h"

using namespace sdk_test;
using namespace testing;

class SdkTestLocklessCSChannel: public SdkTestNodesSetUp
{
public:
    const std::string& getRootTestDir() const override
    {
        return rootTestDir;
    }

    const std::vector<sdk_test::NodeInfo>& getElements() const override
    {
        return testNode;
    }

    const fs::path& getLocalFolder() const
    {
        return localFolder.getPath();
    }

private:
    const std::string rootTestDir{"locklessCS"};
    const std::string localFolderName{getFilePrefix() + "dir"};
    const LocalTempDir localFolder{fs::current_path() / localFolderName};

    const std::vector<NodeInfo> testNode{FileNodeInfo("remoteTestFile").setSize(100)};
};

// Returns a function that sets flag to true when a CS for a given command completes.
auto commandChecker(const std::string& command, bool& flag)
{
    std::ostringstream ostream;

    ostream << "\"a\":\"" << command << "\"";

    return [&flag, pattern = ostream.str()](auto& request)
    {
        flag |= (request->status == REQ_FAILURE || request->status == REQ_SUCCESS) &&
                request->out->find(pattern) != std::string::npos;
    };
}

#ifdef MEGASDK_DEBUG_TEST_HOOKS_ENABLED
/**
 * @brief Ensures that the lockless channel is correctly used when retrieving the download URL ("g")
 * from the intermediate layer or internally when downloading a node.
 */
TEST_F(SdkTestLocklessCSChannel, DownloadFile)
{
    const auto logPre{getLogPrefix()};

    const std::unique_ptr<MegaNode> remoteNode{getNodeByPath("remoteTestFile")};
    ASSERT_TRUE(remoteNode) << "Failed to get the node to be downloaded";

    bool usedLocklessChannel{false};

    globalMegaTestHooks.interceptLocklessCSRequest = commandChecker("g", usedLocklessChannel);

    LOG_info << logPre
             << "Get the download URL. The \"g\" command should use the lockless channel.";
    NiceMock<MockRequestListener> urlTracker(megaApi[0].get());
    megaApi[0]->getDownloadUrl(remoteNode.get(), false, &urlTracker);
    ASSERT_TRUE(urlTracker.waitForFinishOrTimeout(MAX_TIMEOUT))
        << "Error getting the download URL for the remote node.";
    ASSERT_TRUE(usedLocklessChannel)
        << "Lockless channel has not been used to get the download URL.";

    LOG_info << logPre
             << "Download a node. The internal \"g\" command should use the lockless channel.";
    usedLocklessChannel = false;
    const auto errCode = downloadNode(megaApi[0].get(),
                                      remoteNode.get(),
                                      getLocalFolder(),
                                      true,
                                      MAX_TIMEOUT,
                                      ::mega::MegaTransfer::COLLISION_CHECK_ASSUMEDIFFERENT,
                                      ::mega::MegaTransfer::COLLISION_RESOLUTION_NEW_WITH_N);
    ASSERT_EQ(errCode, API_OK) << "Failed to download the remote node.";
    ASSERT_TRUE(usedLocklessChannel)
        << "The lockless channel was not used when downloading a node.";
}

TEST_F(SdkTestLocklessCSChannel, ImportFileLink)
{
    // Convenience.
    auto& client = *megaApi[0];

    // Try and locate the node we want to share.
    auto source = getNodeByPath("remoteTestFile");
    ASSERT_TRUE(source) << "Couldn't locate test file";

    // Try and generate a public link for our node.
    NiceMock<MockRequestListener> exportTracker(&client);

    client.exportNode(source.get(), 0, false, false, &exportTracker);

    ASSERT_TRUE(exportTracker.waitForFinishOrTimeout(MAX_TIMEOUT))
        << "Couldn't generate public link for test file";

    // Refresh the snapshot of our test file.
    source = getNodeByPath("remoteTestFile");
    ASSERT_TRUE(source) << "Couldn't locate test file";

    // Retrieve our node's public link.
    auto link = makeUniqueFrom(source->getPublicLink());
    ASSERT_TRUE(link) << "Couldn't retrieve public link for test file";

    // Log our client into a different account so we can import the link.
    ASSERT_NO_FATAL_FAILURE(locallogout(0));

    auto [username, password] = getEnvVarAccounts().getVarValues(1);
    ASSERT_FALSE(username.empty());
    ASSERT_FALSE(password.empty());

    auto loginTracker = asyncRequestLogin(0, username.c_str(), password.c_str());
    ASSERT_EQ(loginTracker->waitForResult(), API_OK) << "Couldn't log in client as " << username;

    ASSERT_NO_FATAL_FAILURE(fetchnodes(0));

    // Get our hands on the target client's root node.
    auto target = makeUniqueFrom(client.getRootNode());
    ASSERT_TRUE(target) << "Couldn't get target client's root node";

    // So we know whether the import below used the lockless CS channel.
    auto usedLocklessChannel = false;

    globalMegaTestHooks.interceptLocklessCSRequest = commandChecker("g", usedLocklessChannel);

    // Try and import the node into our second client.
    NiceMock<MockRequestListener> importTracker(&client);

    client.importFileLink(link.get(), target.get(), &importTracker);

    ASSERT_TRUE(importTracker.waitForFinishOrTimeout(MAX_TIMEOUT))
        << "Couldn't import test file into target client";

    // Make sure import used the lockless CS channel.
    ASSERT_TRUE(usedLocklessChannel) << "Test file import didn't use the lockless CS channel";
}

TEST_F(SdkTestLocklessCSChannel, StreamFile)
{
    // Address our client more easily.
    auto& client = *megaApi[0];

    // Try and locate the node we want to stream.
    auto node = getNodeByPath("remoteTestFile");
    ASSERT_TRUE(node) << "Couldn't locate test file";

    // So we know whether streaming below used the lockless CS channel.
    auto usedLocklessChannel = false;

    globalMegaTestHooks.interceptLocklessCSRequest = commandChecker("g", usedLocklessChannel);

    // Try and stream some data from the node.
    NiceMock<MockMegaTransferListener> listener;

    client.startStreaming(node.get(), 0, 100, &listener);

    // Wait for all of the data to be streamed.
    ASSERT_TRUE(listener.waitForFinishOrTimeout(MAX_TIMEOUT))
        << "Couldn't stream data from test file";

    // Make sure streaming used the lockless CS channel.
    ASSERT_TRUE(usedLocklessChannel) << "Test file stream didn't use the lockless CS channel";
}
#endif

#ifdef MEGASDK_DEBUG_TEST_HOOKS_ENABLED
/**
 * @brief Simulates and tests recovery from communication failures in the lockless CS channel.
 */
TEST_F(SdkTestLocklessCSChannel, CommunicationFailures)
{
    const auto logPre{getLogPrefix()};

    const std::unique_ptr<MegaNode> remoteNode{getNodeByPath("remoteTestFile")};
    ASSERT_TRUE(remoteNode) << "Failed to get the node to be downloaded";

    bool usedLocklessChannel;
    int errorCounter;

    // Each error (request timeout) takes HttpIO::REQUESTTIMEOUT (2 minutes)
    auto simulateNoResponse =
        [&usedLocklessChannel, &errorCounter](std::unique_ptr<HttpReq>& locklessCSReq)
    {
        if (errorCounter && locklessCSReq->status == REQ_SUCCESS &&
            locklessCSReq->out->find("\"a\":\"g\"") != std::string::npos)
        {
            LOG_info << "Restore API request status to REQ_INFLIGHT to simulate a timeout.";
            locklessCSReq->status = REQ_INFLIGHT;
            usedLocklessChannel = true;
            --errorCounter;
        }
        return true;
    };

    // Each error causes an exponential backoff for the lockless CS channel.
    auto simulateAPI_EAGAIN =
        [&usedLocklessChannel, &errorCounter](std::unique_ptr<HttpReq>& locklessCSReq)
    {
        if (errorCounter && locklessCSReq->status == REQ_SUCCESS &&
            locklessCSReq->out->find("\"a\":\"g\"") != std::string::npos)
        {
            LOG_info << "Replacing API response in the lockless channel with -3.";
            locklessCSReq->in = "-3";
            usedLocklessChannel = true;
            --errorCounter;
        }
        return true;
    };

    LOG_info << logPre << "Download a node after a timeout due to API not responding.";
    usedLocklessChannel = false;
    errorCounter = 1; // Cause one request timeout
    globalMegaTestHooks.interceptLocklessCSRequest = simulateNoResponse;
    auto errCode = downloadNode(megaApi[0].get(),
                                remoteNode.get(),
                                getLocalFolder(),
                                true,
                                MAX_TIMEOUT,
                                ::mega::MegaTransfer::COLLISION_CHECK_ASSUMEDIFFERENT,
                                ::mega::MegaTransfer::COLLISION_RESOLUTION_NEW_WITH_N);
    globalMegaTestHooks.interceptLocklessCSRequest = nullptr;
    ASSERT_EQ(errCode, API_OK) << "Failed to download the remote node.";
    ASSERT_EQ(errorCounter, 0) << "No error simulation has been caused.";
    ASSERT_TRUE(usedLocklessChannel)
        << "The lockless channel was not used when downloading a node.";

    LOG_info << logPre << "Download a node after a backoff due to API returning -3.";
    usedLocklessChannel = false;
    errorCounter = 6; // Receive -3 6 times.
    globalMegaTestHooks.interceptLocklessCSRequest = simulateAPI_EAGAIN;
    errCode = downloadNode(megaApi[0].get(),
                           remoteNode.get(),
                           getLocalFolder(),
                           true,
                           MAX_TIMEOUT,
                           ::mega::MegaTransfer::COLLISION_CHECK_ASSUMEDIFFERENT,
                           ::mega::MegaTransfer::COLLISION_RESOLUTION_NEW_WITH_N);
    globalMegaTestHooks.interceptLocklessCSRequest = nullptr;
    ASSERT_EQ(errCode, API_OK) << "Failed to download the remote node.";
    ASSERT_EQ(errorCounter, 0) << "No error simulation has ocurred.";
    ASSERT_TRUE(usedLocklessChannel)
        << "The lockless channel was not used when downloading a node.";
}
#endif
