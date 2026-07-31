#include "easy_curl.h"
#include "mega/common/testing/utility.h"
#include "mega/testhooks.h"
#include "mega/utils.h"
#include "sdk_server_test_utils.h"

#include <atomic>
#include <chrono>

using ::mega::common::testing::randomBytes;
using ::mega::common::testing::randomName;

class SdkFileServiceTest: public SdkServerTest
{
protected:
    void SetUp() override;

    void TearDown() override;

    std::string mFileContent;

    std::string mFileName;

    size_t mFileSize{4 * 1024};
};

void SdkFileServiceTest::SetUp()
{
    SdkServerTest::SetUp();

    mFileContent = randomBytes(mFileSize);

    mFileName = randomName();

    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    MegaApi* api = megaApi[0].get();

    // Upload
    std::unique_ptr<MegaNode> uploadedNode = uploadFile(0, mFileName, mFileContent);
    ASSERT_NE(uploadedNode, nullptr);

    // Use http server to fetch the file to file service
    const auto httpServer = scopedHttpServer(api);
    ASSERT_TRUE(httpServer);

    std::unique_ptr<char[]> link(api->httpServerGetLocalLink(uploadedNode.get()));
    ASSERT_NE(link, nullptr);
    std::string url = link.get();

    auto response = HttpClient::get(url);
    EXPECT_EQ(200, response.statusCode);
    EXPECT_EQ(mFileContent, response.body);
}

void SdkFileServiceTest::TearDown()
{
    SdkServerTest::TearDown();
}

TEST_F(SdkFileServiceTest, FileServiceGetStorageInfoSuccessfully)
{
    MegaApi* api = megaApi[0].get();

    // Get storage info using file service's current reclaim options
    std::unique_ptr<MegaFileServiceStorageInfo> info{api->fileServiceGetStorageInfo(nullptr)};
    ASSERT_TRUE(info != nullptr);
    ASSERT_GE(info->getAllocatedSize(), mFileSize);
    ASSERT_EQ(info->getReclaimableSize(), 0);

    // Get storage info using input reclaim options
    std::unique_ptr<MegaFileServiceReclaimOptions> options{MegaFileServiceReclaimOptions::create()};
    options->setReclaimTarget(0);
    options->setReclaimThreshold(0);
    options->setAgeThreshold(0);
    info.reset(api->fileServiceGetStorageInfo(options.get()));
    ASSERT_GE(info->getAllocatedSize(), mFileSize);
    ASSERT_GE(info->getReclaimableSize(), mFileSize);
}

TEST_F(SdkFileServiceTest, FileServiceReclaimSuccessfully)
{
    MegaApi* api = megaApi[0].get();

    // Create a reclaim options object to claim all storages
    std::unique_ptr<MegaFileServiceReclaimOptions> options{MegaFileServiceReclaimOptions::create()};
    options->setReclaimTarget(0);
    options->setReclaimThreshold(0);
    options->setAgeThreshold(0);

    // Reclaim it
    RequestTracker rt(api);
    api->fileServiceReclaim(options.get(), &rt);
    ASSERT_EQ(API_OK, rt.waitForResult());

    // No storage afterwards
    std::unique_ptr<MegaFileServiceStorageInfo> info{api->fileServiceGetStorageInfo(nullptr)};
    ASSERT_TRUE(info != nullptr);
    ASSERT_EQ(info->getAllocatedSize(), 0);
    ASSERT_EQ(info->getReclaimableSize(), 0);
}

#ifdef MEGASDK_DEBUG_TEST_HOOKS_ENABLED
/**
 * @brief TEST_F FileServiceStreamingOverquotaEvent
 *
 * Reads served by the local HTTP server go through the file service, which is not backed by a
 * MegaTransfer, so a bandwidth overquota cannot be reported through
 * MegaTransferListener::onTransferTemporaryError the way regular transfers do. Check that:
 *
 *  - the app is told through the global MegaEvent::EVENT_STREAM_OVERQUOTA event, and
 *  - the blocked read gives up instead of waiting out the (far longer) overquota state, so the
 *    HTTP request fails promptly rather than hanging until the client times out.
 */
TEST_F(SdkFileServiceTest, FileServiceStreamingOverquotaEvent)
{
    using std::chrono::seconds;
    using std::chrono::steady_clock;

    // Number of seconds the simulated overquota state lasts.
    constexpr int OVERQUOTA_SECONDS = 30;

    MegaApi* api = megaApi[0].get();

    // A file of its own: the one uploaded by SetUp() is already cached by the file service, and
    // would be served from disk without ever reaching a storage server.
    const std::string content = randomBytes(mFileSize);
    std::unique_ptr<MegaNode> node = uploadFile(0, randomName(), content);
    ASSERT_NE(node, nullptr);

    const auto httpServer = scopedHttpServer(api);
    ASSERT_TRUE(httpServer);

    std::unique_ptr<char[]> link(api->httpServerGetLocalLink(node.get()));
    ASSERT_NE(link, nullptr);

    // Make the first chunk request of the streaming read come back as bandwidth overquota. The
    // upload above is already done, so only the read can be hit.
    // Declared before the hook is cleared, so it outlives every call to the hook.
    std::atomic<bool> injected{false};

    const auto restoreHooks = makeScopedDestructor(
        []()
        {
            globalMegaTestHooks.onHttpReqPost = nullptr;
        });

    globalMegaTestHooks.onHttpReqPost = [&injected](HttpReq* req)
    {
        if (!req || req->type != REQ_BINARY || injected.exchange(true))
            return false;

        req->httpstatus = 509;
        req->timeleft = OVERQUOTA_SECONDS;
        req->status = REQ_FAILURE;
        LOG_info << "SIMULATING OVERQUOTA for a file service streaming read";
        return true;
    };

    mApi[0].resetlastEvent();

    const auto start = steady_clock::now();
    const auto response = HttpClient::get(link.get());
    const auto elapsed = steady_clock::now() - start;

    ASSERT_TRUE(injected.load()) << "No chunk request was made, so no overquota could be simulated";

    // The file must not have been served in full.
    ASSERT_NE(content, response.body) << "The streaming read succeeded despite the overquota";

    // ... and the read must have given up rather than parking until the overquota state ends.
    ASSERT_LT(elapsed, seconds(OVERQUOTA_SECONDS))
        << "The blocked streaming read did not fail promptly";

    ASSERT_TRUE(mApi[0].lastEventsContain(MegaEvent::EVENT_STREAM_OVERQUOTA))
        << "EVENT_STREAM_OVERQUOTA was not delivered for a file service streaming read";

    ASSERT_GT(api->getBandwidthOverquotaDelay(), 0)
        << "The overquota state was not recorded by the client";
}
#endif // MEGASDK_DEBUG_TEST_HOOKS_ENABLED
