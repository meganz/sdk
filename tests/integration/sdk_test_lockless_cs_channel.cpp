/**
 * @file sdk_test_lockless_cs_channel.cpp
 * @brief This file defines tests for the lockless CS channel
 */

#include "integration_test_utils.h"
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
    auto checkCommand = [&usedLocklessChannel](std::unique_ptr<HttpReq>& locklessCSReq)
    {
        // Request finished and we can see that a "g" command has been sent.
        // API can still return an error for the command, but the correct channel has been used.
        if ((locklessCSReq->status == REQ_SUCCESS || locklessCSReq->status == REQ_FAILURE) &&
            locklessCSReq->out->find("\"a\":\"g\"") != std::string::npos)
        {
            usedLocklessChannel = true;
        }
    };

    globalMegaTestHooks.interceptLocklessCSRequest = checkCommand;

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