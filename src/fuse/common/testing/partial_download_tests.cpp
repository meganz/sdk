#include <mega/common/error_or.h>
#include <mega/common/partial_download.h>
#include <mega/common/partial_download_callback.h>
#include <mega/common/utility.h>
#include <mega/fuse/common/testing/client.h>
#include <mega/fuse/common/testing/cloud_path.h>
#include <mega/fuse/common/testing/file.h>
#include <mega/fuse/common/testing/path.h>
#include <mega/fuse/common/testing/test.h>
#include <mega/fuse/common/testing/utility.h>

#include <cstring>
#include <future>

namespace mega::fuse::testing
{

struct FUSEPartialDownloadTests: Test
{
    // Perform instance-specific setup.
    void SetUp() override;

    // Perform fixture-wide setup.
    static void SetUpTestSuite();

    // The content of the file we want to partially download.
    static std::string mFileContent;

    // The handle of the file we want to partially download.
    static NodeHandle mFileHandle;
}; // FUSEPartialDownloadTests

class PartialDownloadCallback: public common::PartialDownloadCallback
{
    // The content we managed to download.
    std::string mContent;

    // The result of our download.
    std::promise<Error> mResult;

    // Called when the download has completed.
    void completed(Error result) override
    {
        mResult.set_value(result);
    }

    // Called when we've received some content.
    void data(const void* buffer, std::uint64_t, std::uint64_t length)
    {
        // Convenience.
        auto* buffer_ = static_cast<const char*>(buffer);

        // Copy the content we've received for later validation.
        mContent.insert(mContent.end(), buffer_, buffer_ + length);
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

    // Return the result of our download.
    Error result()
    {
        return common::waitFor(mResult.get_future());
    }
}; // PartialDownloadCallback

std::string FUSEPartialDownloadTests::mFileContent;

NodeHandle FUSEPartialDownloadTests::mFileHandle;

// For clarity.
static std::uint64_t operator""_KiB(unsigned long long value);
static std::uint64_t operator""_MiB(unsigned long long value);

TEST_F(FUSEPartialDownloadTests, cancel_on_download_destruction_succeeds)
{
    // Create a download.
    auto download = ClientW()->partialDownload(mFileHandle);
    ASSERT_EQ(download.errorOr(API_OK), API_OK);

    PartialDownloadCallback callback;

    // Try and download the entire file.
    (*download)->begin(callback, 0, 1_MiB);

    // Destroy the download.
    download->reset();

    // Wait for the download to complete.
    EXPECT_EQ(callback.result(), API_EINCOMPLETE);
}

TEST_F(FUSEPartialDownloadTests, cancel_on_logout_succeeds)
{
    // Create a client that we can destroy.
    auto client = CreateClient("partial_" + randomName());
    ASSERT_TRUE(client);

    // Log the client in.
    ASSERT_EQ(client->login(1), API_OK);

    // Create a download.
    auto download = client->partialDownload(mFileHandle);
    ASSERT_EQ(download.errorOr(API_OK), API_OK);

    PartialDownloadCallback callback;

    // Try and download the entire file.
    (*download)->begin(callback, 0, 1_MiB);

    // Destroy the client.
    client.reset();

    // Wait for the download to complete.
    ASSERT_EQ(callback.result(), API_EINCOMPLETE);

    // The download consider itself cancelled.
    EXPECT_TRUE((*download)->cancelled());

    // And completed.
    EXPECT_TRUE((*download)->completed());
}

TEST_F(FUSEPartialDownloadTests, cancel_succeeds)
{
    // Try and create a download for us to cancel.
    auto download = ClientW()->partialDownload(mFileHandle);
    ASSERT_EQ(download.errorOr(API_OK), API_OK);

    // Downloads that aren't in progress shouldn't be cancellable.
    ASSERT_FALSE((*download)->cancellable());

    PartialDownloadCallback callback;

    // Try and download the entire file.
    (*download)->begin(callback, 0, 1_MiB);

    // Make sure the download's cancellable.
    ASSERT_TRUE((*download)->cancellable());

    // Try and cancel the download.
    EXPECT_TRUE((*download)->cancel());

    // Wait for the download to complete.
    EXPECT_EQ(callback.result(), API_EINCOMPLETE);

    // The download should report itself as cancelled.
    EXPECT_TRUE((*download)->cancelled());

    // And completed.
    EXPECT_TRUE((*download)->completed());
}

TEST_F(FUSEPartialDownloadTests, download_succeeds)
{
    // Download some content from our test file.
    auto download = [this](std::uint64_t begin, std::uint64_t end, std::uint64_t length)
    {
        // Try and create a new partial download.
        auto download = ClientW()->partialDownload(mFileHandle);
        ASSERT_EQ(download.errorOr(API_OK), API_OK);

        PartialDownloadCallback callback;

        // Try and download some content from our test file.
        (*download)->begin(callback, begin, end - begin);

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

void FUSEPartialDownloadTests::SetUp()
{
    // Make sure our clients are still sane.
    Test::SetUp();
}

void FUSEPartialDownloadTests::SetUpTestSuite()
{
    // Convenience.
    using ::testing::AnyOf;

    // Make sure our clients are set up.
    Test::SetUpTestSuite();

    // Make sure the test root is clean.
    ASSERT_THAT(ClientW()->remove("/y"), AnyOf(API_ENOENT, API_OK));

    // Recreate the test root.
    auto rootHandle = ClientW()->makeDirectory("y", "/");
    ASSERT_EQ(rootHandle.errorOr(API_OK), API_OK);

    // Generate content for our test file.
    mFileContent = randomBytes(1_MiB);

    // Create a file so we can upload our content to the cloud.
    File file(mFileContent, randomName(), mScratchPath);

    // Upload the file to the cloud.
    auto fileHandle = ClientW()->upload(*rootHandle, file.path());
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

} // mega::fuse::testing
