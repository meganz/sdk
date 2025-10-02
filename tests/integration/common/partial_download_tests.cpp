#include <gmock/gmock.h>
#include <mega/common/error_or.h>
#include <mega/common/partial_download.h>
#include <mega/common/partial_download_callback.h>
#include <mega/common/testing/client.h>
#include <mega/common/testing/cloud_path.h>
#include <mega/common/testing/file.h>
#include <mega/common/testing/path.h>
#include <mega/common/testing/real_client.h>
#include <mega/common/testing/single_client_test.h>
#include <mega/common/testing/utility.h>
#include <mega/common/utility.h>
#include <mega/logging.h>

#include <array>
#include <chrono>
#include <cstring>
#include <future>

namespace mega
{
namespace common
{
namespace testing
{

struct PartialDownloadTestTraits
{
    using AbstractClient = Client;
    using ConcreteClient = RealClient;

    static constexpr const char* mName = "partial_download";
}; // PartialDownloadTestTraits

struct PartialDownloadTests: public SingleClientTest<PartialDownloadTestTraits>
{
    // Perform instance-specific setup.
    void SetUp() override;

    // Perform fixture-wide setup.
    static void SetUpTestSuite();

    // The content of the file we want to partially download.
    static std::string mFileContent;

    // The handle of the file we want to partially download.
    static NodeHandle mFileHandle;
}; // PartialDownloadTests

class PartialDownloadCallback: public common::PartialDownloadCallback
{
    // The content we managed to download.
    std::string mContent;

    // The download this callback relates to.
    //
    // Should be set only for some cancellation tests.
    common::PartialDownloadWeakPtr mDownload;

    // The result of our download.
    std::promise<Error> mResult;

    // Called when the download has completed.
    void completed(Error result) override
    {
        mResult.set_value(result);
    }

    // Called when we've received some content.
    auto data(const void* buffer, std::uint64_t, std::uint64_t length)
        -> std::variant<Abort, Continue> override
    {
        // Convenience.
        auto* buffer_ = static_cast<const char*>(buffer);

        // Copy the content we've received for later validation.
        mContent.insert(mContent.end(), buffer_, buffer_ + length);

        // Cancel the download if it's been injected.
        if (auto download = mDownload.lock())
            return Abort();

        // Continue the download.
        return Continue();
    }

    // Called when the download has experienced some failure.
    auto failed(Error, int) -> std::variant<Abort, Retry> override
    {
        // Always abort the download.
        return Abort();
    }

public:
    // Return a reference to our downloaded content.
    const std::string& content() const
    {
        return mContent;
    }

    // Specify which download this callback relates to.
    void download(common::PartialDownloadPtr download)
    {
        mDownload = std::move(download);
    }

    // Return the result of our download.
    Error result()
    {
        return common::waitFor(mResult.get_future());
    }
}; // PartialDownloadCallback

// For clarity.
static std::uint64_t operator""_KiB(unsigned long long value);
static std::uint64_t operator""_MiB(unsigned long long value);

std::string PartialDownloadTests::mFileContent;

NodeHandle PartialDownloadTests::mFileHandle;

using common::testing::File;
using common::testing::randomBytes;
using common::testing::randomName;

TEST_F(PartialDownloadTests, DISABLED_measure_average_fetch_times)
{
    // Lets us fetch a file without actually storing its data anywhere.
    class FetchCallback: public PartialDownloadCallback
    {
        // Who should we notify when the download completes.
        std::promise<Error> mNotifier;

        // Called when the download has completed.
        void completed(Error result) override
        {
            mNotifier.set_value(result);
        }

        // Called when we've received some content.
        auto data(const void*, std::uint64_t, std::uint64_t)
            -> std::variant<Abort, Continue> override
        {
            return Continue();
        }

        // Called when the download has experienced some failure.
        auto failed(Error result, int retries) -> std::variant<Abort, Retry> override
        {
            // Retry a maximum of five times.
            if (result != API_EAGAIN || retries >= 5)
                return Abort();

            // Convenience.
            using ::mega::common::deciseconds;

            // Retry after 200ms.
            return Retry(deciseconds(20));
        }

    public:
        FetchCallback(std::promise<Error> notifier):
            mNotifier(std::move(notifier))
        {}
    }; // FetchCallback

    // Maximum read size specified as a power of two.
    constexpr auto maximumReadSize = 24ul;

    // Minimum read size specified as a power of two.
    constexpr auto minimumReadSize = 8ul;

    // Sanity.
    static_assert(maximumReadSize > minimumReadSize);

    // How many sizes are we testing?
    constexpr auto numReadSizes = maximumReadSize - minimumReadSize + 1;

    // How many times we should sample a given read size.
    constexpr auto numSamplesPerReadSize = 10ul;

    // Convenience.
    using std::chrono::milliseconds;

    // Allocate space to store our measurements.
    std::array<std::uint64_t, numReadSizes> measurements{};

    // Try and create a file for us to test against.
    auto handle = mClient->upload(randomBytes((1 << maximumReadSize) + 4096), randomName(), "/y");

    // Make sure we could create our test file.
    ASSERT_EQ(handle.errorOr(API_OK), API_OK);

    // Measure the average fetch time for each read size.
    for (auto i = 0ul; i < numReadSizes; ++i)
    {
        // Get a reference to this read size's measurements.
        auto& measurement = measurements[i];

        // Compute our read size.
        auto size = 1ul << (i + minimumReadSize);

        // Measure the average fetch time for a given read size.
        for (auto j = 0ul; j < numSamplesPerReadSize; ++j)
        {
            // Convenience.
            using std::chrono::duration_cast;
            using std::chrono::milliseconds;
            using std::chrono::steady_clock;

            // So we can signal when our fetch has completed.
            std::promise<Error> notifier;

            // So we can wait until our fetch has completed.
            auto waiter = notifier.get_future();

            // So we can receive updates as our fetch progresses.
            FetchCallback callback(std::move(notifier));

            // Try and create a partial download for our test file.
            auto download = mClient->partialDownload(callback, *handle, 0, size);

            // Make sure we could create a partial download.
            ASSERT_EQ(download.errorOr(API_OK), API_OK);

            // Figure out when this sample began.
            auto began = steady_clock::now();

            // Start the download.
            (*download)->begin();

            // Wait for the fetch to complete.
            ASSERT_NE(waiter.wait_for(mDefaultTimeout), std::future_status::timeout);

            // Make sure the fetch was successful.
            ASSERT_EQ(waiter.get(), API_OK);

            // How much time did the fetch take?
            auto elapsed = duration_cast<milliseconds>(steady_clock::now() - began);

            LOG_debug << size << " sample #" << j << " took " << elapsed.count()
                      << " millisecond(s).";

            // Add our fetch time to this size's measurement.
            measurement += static_cast<std::uint64_t>(elapsed.count());
        }

        // Compute this read size's average fetch time.
        measurement /= numSamplesPerReadSize;
    }

    // Display profile results.
    for (auto i = 0ul; i < numReadSizes; ++i)
    {
        LOG_debug << "Average fetch time for " << (1ul << (i + minimumReadSize)) << " is "
                  << measurements[i] << " millisecond(s)";
    }
}

TEST_F(PartialDownloadTests, cancel_completed_fails)
{
    PartialDownloadCallback callback;

    // Create a download.
    auto download = mClient->partialDownload(callback, mFileHandle, 0, 1_KiB);
    ASSERT_EQ(download.errorOr(API_OK), API_OK);

    // Begin the download.
    (*download)->begin();

    // Wait for the download to complete.
    EXPECT_EQ(callback.result(), API_OK);

    // Make sure you can't cancel a download that's already completed.
    EXPECT_FALSE((*download)->cancel());
}

TEST_F(PartialDownloadTests, cancel_on_download_destruction_succeeds)
{
    PartialDownloadCallback callback;

    // Create a download.
    auto download = mClient->partialDownload(callback, mFileHandle, 0, 1_MiB);
    ASSERT_EQ(download.errorOr(API_OK), API_OK);

    // Make sure the download isn't completed before we can cancel it.
    mClient->setDownloadSpeed(4096);

    // Try and download the entire file.
    (*download)->begin();

    // Destroy the download.
    download->reset();

    // Wait for the download to complete.
    EXPECT_EQ(callback.result(), API_EINCOMPLETE);
}

TEST_F(PartialDownloadTests, cancel_during_data_succeeds)
{
    PartialDownloadCallback callback;

    // Create a download.
    auto download = mClient->partialDownload(callback, mFileHandle, 0, 1_MiB);
    ASSERT_EQ(download.errorOr(API_OK), API_OK);

    // Specify which download our callback is associated with.
    callback.download(*download);

    // Begin the download.
    (*download)->begin();

    // Wait for the download to complete.
    EXPECT_EQ(callback.result(), API_EINCOMPLETE);
}

TEST_F(PartialDownloadTests, cancel_on_logout_succeeds)
{
    // Create a client that we can destroy.
    auto client = CreateClient("partial_" + randomName());
    ASSERT_TRUE(client);

    // Log the client in.
    ASSERT_EQ(client->login(0), API_OK);

    PartialDownloadCallback callback;

    // Create a download.
    auto download = client->partialDownload(callback, mFileHandle, 0, 1_MiB);
    ASSERT_EQ(download.errorOr(API_OK), API_OK);

    // Make sure the download isn't completed before we can cancel it.
    client->setDownloadSpeed(4096);

    // Try and download the entire file.
    (*download)->begin();

    // Logout the client.
    EXPECT_EQ(client->logout(true), API_OK);

    // Wait for the download to complete.
    ASSERT_EQ(callback.result(), API_EINCOMPLETE);

    // The download consider itself cancelled.
    EXPECT_TRUE((*download)->cancelled());

    // And completed.
    EXPECT_TRUE((*download)->completed());
}

TEST_F(PartialDownloadTests, cancel_succeeds)
{
    PartialDownloadCallback callback;

    // Try and create a download for us to cancel.
    auto download = mClient->partialDownload(callback, mFileHandle, 0, 1_MiB);
    ASSERT_EQ(download.errorOr(API_OK), API_OK);

    // Downloads are cancellable until they've been completed.
    EXPECT_TRUE((*download)->cancellable());

    // Make sure the download isn't completed before we can cancel it.
    mClient->setDownloadSpeed(4096);

    // Try and download the entire file.
    (*download)->begin();

    // Try and cancel the download.
    EXPECT_TRUE((*download)->cancel());

    // Wait for the download to complete.
    EXPECT_EQ(callback.result(), API_EINCOMPLETE);

    // The download should report itself as cancelled.
    EXPECT_TRUE((*download)->cancelled());

    // And completed.
    EXPECT_TRUE((*download)->completed());
}

TEST_F(PartialDownloadTests, download_directory_fails)
{
    PartialDownloadCallback callback;

    // You shouldn't be able to download a directory.
    auto download = mClient->partialDownload(callback, "/y", 0, 1_MiB);
    ASSERT_EQ(download.errorOr(API_OK), API_FUSE_EISDIR);
}

TEST_F(PartialDownloadTests, download_succeeds)
{
    // Download some content from our test file.
    auto download = [](std::uint64_t begin, std::uint64_t end, std::uint64_t length)
    {
        // Sanity.
        ASSERT_LE(begin, end);

        PartialDownloadCallback callback;

        // Try and create a new partial download.
        auto download = mClient->partialDownload(callback, mFileHandle, begin, end - begin);
        ASSERT_EQ(download.errorOr(API_OK), API_OK);

        // Try and download some content from our test file.
        (*download)->begin();

        // Wait for our download to complete.
        ASSERT_EQ(callback.result(), API_OK);

        // The download should report itself as completed.
        EXPECT_TRUE((*download)->completed());

        auto& content = callback.content();

        // Convenience.
        auto begin_ = static_cast<std::size_t>(begin);
        auto length_ = static_cast<std::size_t>(length);

        // Make sure the content we downloaded is as we expected.
        ASSERT_EQ(content.size(), length_);
        ASSERT_FALSE(mFileContent.compare(begin_, length_, content));
    }; // download

    // Try and download some data from the beginning of the file.
    EXPECT_NO_FATAL_FAILURE(download(0, 256_KiB, 256_KiB));

    // Try and download some data from the middle of the file.
    EXPECT_NO_FATAL_FAILURE(download(256_KiB, 768_KiB, 512_KiB));

    // Try and download some data from the end of the file.
    EXPECT_NO_FATAL_FAILURE(download(768_KiB, 1_MiB, 256_KiB));

    // Make sure a download's range is properly clamped.
    EXPECT_NO_FATAL_FAILURE(download(768_KiB, 2_MiB, 256_KiB));

    // Make sure zero-length ranges are handled properly.
    EXPECT_NO_FATAL_FAILURE(download(0, 0, 0));
    EXPECT_NO_FATAL_FAILURE(download(1_MiB, 1_MiB, 0));
    EXPECT_NO_FATAL_FAILURE(download(1_MiB, 2_MiB, 0));
}

void PartialDownloadTests::SetUp()
{
    SingleClientTest::SetUp();

    // Make sure downloads proceed at full speed.
    mClient->setDownloadSpeed(0);
}

void PartialDownloadTests::SetUpTestSuite()
{
    // Convenience.
    using ::testing::AnyOf;

    // Make sure our clients are set up.
    SingleClientTest::SetUpTestSuite();

    // Make sure the test root is clean.
    ASSERT_THAT(mClient->remove("/y"), AnyOf(API_FUSE_ENOTFOUND, API_OK));

    // Recreate the test root.
    auto rootHandle = mClient->makeDirectory("y", "/");
    ASSERT_EQ(rootHandle.errorOr(API_OK), API_OK);

    // Generate content for our test file.
    mFileContent = randomBytes(1_MiB);

    // Create a file so we can upload our content to the cloud.
    File file(mFileContent, randomName(), mScratchPath);

    // Upload the file to the cloud.
    auto fileHandle = mClient->upload(*rootHandle, file.path());
    ASSERT_EQ(fileHandle.errorOr(API_OK), API_OK);

    // Latch the file's handle for later use.
    mFileHandle = *fileHandle;
}

std::uint64_t operator""_KiB(unsigned long long value)
{
    return value * 1024;
}

std::uint64_t operator""_MiB(unsigned long long value)
{
    return value * 1024_KiB;
}

} // testing
} // common
} // mega
