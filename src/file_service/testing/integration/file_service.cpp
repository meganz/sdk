#include <mega/common/error_or.h>
#include <mega/common/node_info.h>
#include <mega/common/utility.h>
#include <mega/file_service/file.h>
#include <mega/file_service/file_id.h>
#include <mega/file_service/file_info.h>
#include <mega/file_service/file_range.h>
#include <mega/file_service/file_read_result.h>
#include <mega/file_service/file_result.h>
#include <mega/file_service/file_result_or.h>
#include <mega/file_service/file_service_result.h>
#include <mega/file_service/file_service_result_or.h>
#include <mega/file_service/source.h>
#include <mega/fuse/common/testing/client.h>
#include <mega/fuse/common/testing/cloud_path.h>
#include <mega/fuse/common/testing/file.h>
#include <mega/fuse/common/testing/path.h>
#include <mega/fuse/common/testing/test.h>
#include <mega/fuse/common/testing/utility.h>
#include <mega/logging.h>

namespace mega
{
namespace file_service
{
namespace testing
{

struct FileServiceTests: fuse::testing::Test
{
    // Perform instance-specific setup.
    void SetUp() override;

    // Perform fixture-wide setup.
    static void SetUpTestSuite();

    // The content of the file we want to read.
    static std::string mFileContent;

    // The handle of the file we want to read.
    static NodeHandle mFileHandle;
}; // FUSEPartialDownloadTests

std::string FileServiceTests::mFileContent;

NodeHandle FileServiceTests::mFileHandle;

// For clarity.
static std::uint64_t operator""_KiB(unsigned long long value);
static std::uint64_t operator""_MiB(unsigned long long value);

// Read some content from the specified file.
static auto read(File file, std::uint64_t offset, std::uint64_t length)
    -> std::future<FileResultOr<std::string>>;

TEST_F(FileServiceTests, info_directory_fails)
{
    // Can't get info about a directory.
    EXPECT_EQ(ClientW()->fileInfo("/z").errorOr(FILE_SERVICE_SUCCESS), FILE_SERVICE_UNKNOWN_FILE);
}

TEST_F(FileServiceTests, info_unknown_fails)
{
    // Can't get info about a file the service isn't managing.
    EXPECT_EQ(ClientW()->fileInfo(mFileHandle).errorOr(FILE_SERVICE_SUCCESS),
              FILE_SERVICE_UNKNOWN_FILE);
}

TEST_F(FileServiceTests, open_directory_fails)
{
    // Can't open a directory.
    EXPECT_EQ(ClientW()->fileOpen("/z").errorOr(FILE_SERVICE_SUCCESS),
              FILE_SERVICE_FILE_IS_A_DIRECTORY);
}

TEST_F(FileServiceTests, open_file_succeeds)
{
    // We should be able to open a file.
    auto file = ClientW()->fileOpen(mFileHandle);
    EXPECT_EQ(file.errorOr(FILE_SERVICE_SUCCESS), FILE_SERVICE_SUCCESS);

    // We should be able to get information about that file.
    auto fileInfo = ClientW()->fileInfo(mFileHandle);
    EXPECT_EQ(fileInfo.errorOr(FILE_SERVICE_SUCCESS), FILE_SERVICE_SUCCESS);

    // Get our hands on the node's information.
    auto nodeInfo = ClientW()->get(mFileHandle);
    ASSERT_EQ(nodeInfo.errorOr(API_OK), API_OK);

    // Make sure the file's information matches the node's.
    EXPECT_EQ(fileInfo->id(), FileID::from(mFileHandle));
    EXPECT_EQ(fileInfo->modified(), nodeInfo->mModified);
    EXPECT_EQ(fileInfo->size(), static_cast<std::uint64_t>(nodeInfo->mSize));
}

TEST_F(FileServiceTests, open_unknown_fails)
{
    // Can't open a file that doesn't exist.
    EXPECT_EQ(ClientW()->fileOpen("/bogus").errorOr(FILE_SERVICE_SUCCESS),
              FILE_SERVICE_FILE_DOESNT_EXIST);
}

TEST_F(FileServiceTests, read_cancel_on_client_logout_succeeds)
{
    // Convenience.
    using fuse::testing::randomName;

    // Create a client that we can safely logout.
    auto client = CreateClient("file_service_" + randomName());
    ASSERT_TRUE(client);

    // Log the client in.
    ASSERT_EQ(client->login(1), API_OK);

    // Open a file for reading.
    auto file = client->fileOpen(mFileHandle);
    ASSERT_EQ(file.errorOr(FILE_SERVICE_SUCCESS), FILE_SERVICE_SUCCESS);

    // Kick off a read.
    auto waiter = read(std::move(*file), 512_KiB, 256_KiB);

    // Log out the client.
    client.reset();

    // Convenience.
    auto timeout = std::future_status::timeout;

    // Wait for the read to complete.
    ASSERT_NE(waiter.wait_for(mDefaultTimeout), timeout);

    // Make sure the read was cancelled.
    EXPECT_EQ(waiter.get().errorOr(FILE_SUCCESS), FILE_CANCELLED);
}

TEST_F(FileServiceTests, read_cancel_on_file_destruction_succeeds)
{
    // Convenience.
    using common::makeSharedPromise;

    // Open a file for reading.
    auto file = ClientW()->fileOpen(mFileHandle);
    ASSERT_EQ(file.errorOr(FILE_SERVICE_SUCCESS), FILE_SERVICE_SUCCESS);

    // So our callback can tell us when the read's been cancelled.
    auto notifier = makeSharedPromise<FileResult>();

    // So we can wait until the read's been cancelled.
    auto waiter = notifier->get_future();

    // Called when our read's been cancelled.
    auto callback = [=](FileResultOr<FileReadResult> result)
    {
        // Let the waiter know whether the read's been cancelled.
        notifier->set_value(result.errorOr(FILE_SUCCESS));
    }; // callback

    // Begin the read, taking care to drop our file reference.
    [&callback](File file)
    {
        file.read(std::move(callback), 768_KiB, 256_KiB);
    }(std::move(*file));

    // Convenience.
    auto timeout = std::future_status::timeout;

    // Wait for the read to complete.
    ASSERT_NE(waiter.wait_for(mDefaultTimeout), timeout);

    // Make sure the read was cancelled.
    EXPECT_EQ(waiter.get(), FILE_CANCELLED);
}

TEST_F(FileServiceTests, read_succeeds)
{
    // Convenience.
    auto timeout = std::future_status::timeout;

    // Compare file content.
    auto compare = [this](std::string& content, std::uint64_t offset, std::uint64_t length)
    {
        // Convenience.
        std::uint64_t size = mFileContent.size();

        // Offset and/or length is out of bounds.
        if (offset + length > size)
            return false;

        // Content is smaller than expected.
        if (content.size() < length)
            return false;

        // Make sure the content matches our file.
        return !mFileContent.compare(offset, length, content);
    }; // compare

    // Open a file for reading.
    auto file = ClientW()->fileOpen(mFileHandle);
    ASSERT_EQ(file.errorOr(FILE_SERVICE_SUCCESS), FILE_SERVICE_SUCCESS);

    // We should be able to read 64KiB from the beginning of the file.
    auto waiter = read(*file, 0, 64_KiB);

    // Wait for the read to complete.
    ASSERT_NE(waiter.wait_for(mDefaultTimeout), timeout);

    auto result = waiter.get();

    // Make sure the read completed successfully.
    ASSERT_EQ(result.errorOr(FILE_SUCCESS), FILE_SUCCESS);

    // And that it provided the data we expected.
    EXPECT_TRUE(compare(*result, 0, 64_KiB));

    // Make sure the range is considered to be in storage.
    auto ranges = file->ranges();

    ASSERT_FALSE(ranges.empty());
    EXPECT_EQ(ranges.front(), FileRange(0, 64_KiB));

    // Read another 64KiB.
    waiter = read(*file, 64_KiB, 64_KiB);

    // Wait for the read to complete...
    ASSERT_NE(waiter.wait_for(mDefaultTimeout), timeout);

    result = waiter.get();

    // Make sure the read completed successfully.
    ASSERT_EQ(result.errorOr(FILE_SUCCESS), FILE_SUCCESS);

    // And that it provided the data we expected.
    EXPECT_TRUE(compare(*result, 64_KiB, 64_KiB));

    // We should have one 128KiB range in storage.
    ranges = file->ranges();

    ASSERT_EQ(ranges.size(), 1u);
    EXPECT_EQ(ranges.front(), FileRange(0, 128_KiB));

    // Kick off two reads in parallel.
    auto waiter0 = read(*file, 128_KiB, 64_KiB);
    auto waiter1 = read(*file, 192_KiB, 64_KiB);

    // Wait for our reads to complete.
    ASSERT_NE(waiter0.wait_for(mDefaultTimeout), timeout);
    ASSERT_NE(waiter1.wait_for(mDefaultTimeout), timeout);

    // Make sure both reads succeeded.
    auto result0 = waiter0.get();
    auto result1 = waiter1.get();

    EXPECT_EQ(result0.errorOr(FILE_SUCCESS), FILE_SUCCESS);
    EXPECT_EQ(result1.errorOr(FILE_SUCCESS), FILE_SUCCESS);

    ASSERT_FALSE(HasFailure());

    // Make sure both reads gave us what we expected.
    EXPECT_TRUE(compare(*result0, 128_KiB, 64_KiB));
    EXPECT_TRUE(compare(*result1, 192_KiB, 64_KiB));

    // We should have one 256KiB range in storage.
    ranges = file->ranges();

    ASSERT_EQ(ranges.size(), 1u);
    EXPECT_EQ(ranges.front(), FileRange(0, 256_KiB));

    // Make sure zero length reads are handled correctly.
    waiter = read(*file, 0, 0);
    ASSERT_NE(waiter.wait_for(mDefaultTimeout), timeout);

    result = waiter.get();
    ASSERT_EQ(result.errorOr(FILE_SUCCESS), FILE_SUCCESS);
    EXPECT_TRUE(result->empty());

    // Make sure reads are clamped.
    waiter = read(*file, 768_KiB, 512_KiB);
    ASSERT_NE(waiter.wait_for(mDefaultTimeout), timeout);

    result = waiter.get();
    ASSERT_EQ(result.errorOr(FILE_SUCCESS), FILE_SUCCESS);
    EXPECT_TRUE(compare(*result, 768_KiB, 256_KiB));
}

TEST_F(FileServiceTests, ref_succeeds)
{
    // Convenience.
    auto SUCCESS = FILE_SERVICE_SUCCESS;
    auto UNKNOWN = FILE_SERVICE_UNKNOWN_FILE;

    // Open a file for reading without retaining a reference.
    EXPECT_EQ(ClientW()->fileOpen(mFileHandle).errorOr(SUCCESS), SUCCESS);

    // The service should've removed the file as it has no references.
    EXPECT_EQ(ClientW()->fileInfo(mFileHandle).errorOr(UNKNOWN), UNKNOWN);

    // Open a file and establish a reference.
    {
        // Open the file.
        auto file = ClientW()->fileOpen(mFileHandle);
        ASSERT_EQ(file.errorOr(SUCCESS), SUCCESS);

        // Let the service know it should keep the file.
        file->ref();
    }

    // Make sure the service hasn't removed the file.
    EXPECT_EQ(ClientW()->fileInfo(mFileHandle).errorOr(SUCCESS), SUCCESS);

    // Get our hands on the file again.
    auto file = ClientW()->fileOpen(mFileHandle);
    ASSERT_EQ(file.errorOr(SUCCESS), SUCCESS);

    // Let the service know it can remove the file.
    file->unref();
}

void FileServiceTests::SetUp()
{
    // Make sure our clients are still sane.
    Test::SetUp();
}

void FileServiceTests::SetUpTestSuite()
{
    // Convenience.
    using fuse::testing::File;
    using fuse::testing::randomBytes;
    using fuse::testing::randomName;
    using ::testing::AnyOf;

    // Make sure our clients are set up.
    Test::SetUpTestSuite();

    // Make sure the test root is clean.
    ASSERT_THAT(ClientW()->remove("/z"), AnyOf(API_ENOENT, API_OK));

    // Recreate the test root.
    auto rootHandle = ClientW()->makeDirectory("z", "/");
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

auto read(File file, std::uint64_t offset, std::uint64_t length)
    -> std::future<FileResultOr<std::string>>
{
    struct ReadContext;

    // Convenience.
    using ReadContextPtr = std::shared_ptr<ReadContext>;

    // Tracks state necessary for our read.
    struct ReadContext
    {
        ReadContext(File file, std::uint64_t length):
            mBuffer(),
            mFile(std::move(file)),
            mLength(length),
            mNotifier(),
            mOffset(0u)
        {}

        // Called when we've received content.
        void onRead(ReadContextPtr& context, FileResultOr<FileReadResult> result)
        {
            // Couldn't read content.
            if (!result)
                return mNotifier.set_value(unexpected(result.error()));

            // Convenience.
            auto& source = result->mSource;

            // No more content to read.
            if (!result->mLength)
                return mNotifier.set_value(std::move(mBuffer));

            // Extend buffer as needed.
            mBuffer.resize(mOffset + result->mLength);

            // Couldn't copy the content to our buffer.
            if (!source.read(&mBuffer[mOffset], 0, result->mLength))
                return mNotifier.set_value(unexpected(FILE_FAILED));

            // Bump our offset and length.
            mOffset += result->mLength;
            mLength -= result->mLength;

            // Read remaining content, if any.
            mFile.read(
                std::bind(&ReadContext::onRead, this, std::move(context), std::placeholders::_1),
                result->mOffset + result->mLength,
                mLength);
        }

        // Where we'll store content.
        std::string mBuffer;

        // What file are we reading from?
        File mFile;

        // How much content are we reading?
        std::uint64_t mLength;

        // Who should we notify when the read is complete?
        std::promise<FileResultOr<std::string>> mNotifier;

        // Where in the file are we reading from?
        std::uint64_t mOffset;
    }; // ReadContext

    // Create a context to track our read state.
    auto context = std::make_shared<ReadContext>(file, length);

    // Get our hands on the context's future.
    auto waiter = context->mNotifier.get_future();

    // Kick off the read.
    file.read(
        std::bind(&ReadContext::onRead, context.get(), std::move(context), std::placeholders::_1),
        offset,
        length);

    // Return waiter to our caller.
    return waiter;
}

} // testing
} // file_service
} // mega
