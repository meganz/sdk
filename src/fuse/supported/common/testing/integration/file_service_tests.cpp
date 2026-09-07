#include <mega/common/testing/path.h>
#include <mega/common/utility.h>
#include <mega/file_service/file.h>
#include <mega/file_service/file_id.h>
#include <mega/file_service/file_result.h>
#include <mega/file_service/file_result_or.h>
#include <mega/file_service/file_service_result.h>
#include <mega/file_service/file_service_result_or.h>
#include <mega/file_service/file_write_result.h>
#include <mega/fuse/common/testing/test_base.h>
#include <mega/fuse/common/testing/utility.h>

#include <chrono>
#include <filesystem>

namespace mega
{
namespace fuse
{
namespace testing
{

struct FUSEFileServiceTests: TestBase
{}; // FUSEFileServiceTests

// Convenience.
using common::DateTime;
using common::makeSharedPromise;
using common::testing::lastWriteTime;
using common::testing::Path;
using common::testing::waitFor;
using namespace file_service;

TEST_F(FUSEFileServiceTests, perceives_file_service_remove_succeeds)
{
    // Get the file's handle.
    auto handle = ClientW()->handle("/x/s/sf1");
    ASSERT_EQ(handle.errorOr(API_OK), API_OK);

    // Open the file for IO.
    auto file = ClientW()->fileService().open(FileID::from(*handle));
    ASSERT_EQ(file.errorOr(FILE_SERVICE_SUCCESS), FILE_SERVICE_SUCCESS);

    // Remove the file.
    auto result = [&file]()
    {
        auto notifier = makeSharedPromise<FileResult>();

        file->remove(
            [notifier](auto result)
            {
                notifier->set_value(result);
            });

        return notifier->get_future();
    }();

    // Wait for the remove to complete.
    ASSERT_NE(result.wait_for(mDefaultTimeout), std::future_status::timeout);

    // Make sure the remove succeeded.
    ASSERT_EQ(result.get(), FILE_SUCCESS);

    // Wait for FUSE to see the File Service's changes.
    EXPECT_TRUE(waitFor(
        [&]()
        {
            return !fs::exists(MountPathW() / "sf1");
        },
        mDefaultTimeout));
}

TEST_F(FUSEFileServiceTests, perceives_file_service_touch_succeeds)
{
    // Convenience.
    const auto filePath = MountPathW() / "sf1";

    // Read the file's current modification time.
    m_time_t modifiedBefore = lastWriteTime(filePath);

    // Get the file's handle.
    auto handle = ClientW()->handle("/x/s/sf1");
    ASSERT_EQ(handle.errorOr(API_OK), API_OK);

    // Open the file for IO.
    auto file = ClientW()->fileService().open(FileID::from(*handle));
    ASSERT_EQ(file.errorOr(FILE_SERVICE_SUCCESS), FILE_SERVICE_SUCCESS);

    // Changes the file's modification time.
    auto touch = [&file](auto modified)
    {
        auto notifier = makeSharedPromise<FileResult>();

        file->touch(
            [notifier](auto result)
            {
                notifier->set_value(result);
            },
            modified);

        return notifier->get_future();
    };

    // Move the file's modification time into the near past.
    auto result = touch(modifiedBefore - 20);

    // Wait for the touch to complete.
    ASSERT_NE(result.wait_for(mDefaultTimeout), std::future_status::timeout);

    // Make sure the touch was successful.
    ASSERT_EQ(result.get(), FILE_SUCCESS);

    m_time_t modifiedAfter;

    // Wait for FUSE to see the File Service's change.
    EXPECT_TRUE(waitFor(
        [&]()
        {
            modifiedAfter = lastWriteTime(filePath);
            return modifiedAfter == modifiedBefore - 20;
        },
        mDefaultTimeout));

    ASSERT_EQ(modifiedAfter, modifiedBefore - 20);

    // Move the file's modification time into the near future.
    result = touch(modifiedBefore + 20);

    // Wait for the touch to complete successfully.
    ASSERT_NE(result.wait_for(mDefaultTimeout), std::future_status::timeout);
    ASSERT_EQ(result.get(), FILE_SUCCESS);

    // Wait for FUSE to see the File Service's change.
    EXPECT_TRUE(waitFor(
        [&]()
        {
            modifiedAfter = lastWriteTime(filePath);
            return modifiedAfter == modifiedBefore + 20;
        },
        mDefaultTimeout));

    ASSERT_EQ(modifiedAfter, modifiedBefore + 20);
}

TEST_F(FUSEFileServiceTests, perceives_file_service_truncates_succeeds)
{
    // Convenience.
    const auto filePath = MountPathW() / "sf1";

    // Read the file's current data.
    auto dataBefore = readFile(filePath);

    // Get the file's handle.
    auto handle = ClientW()->handle("/x/s/sf1");
    ASSERT_EQ(handle.errorOr(API_OK), API_OK);

    // Open the file for IO.
    auto file = ClientW()->fileService().open(FileID::from(*handle));
    ASSERT_EQ(file.errorOr(FILE_SERVICE_SUCCESS), FILE_SERVICE_SUCCESS);

    // Changes the file's size.
    auto truncate = [&file](auto newSize)
    {
        auto notifier = makeSharedPromise<FileResult>();

        file->truncate(
            [notifier](auto result)
            {
                notifier->set_value(result);
            },
            newSize);

        return notifier->get_future();
    };

    // Get the current time.
    auto now = common::now();

    // Make sure the file's modification time changes.
    std::this_thread::sleep_for(std::chrono::seconds(2));

    // Extend the file's size.
    auto result = truncate(5);

    // Wait for our truncation to complete.
    ASSERT_NE(result.wait_for(mDefaultTimeout), std::future_status::timeout);

    // Make sure the truncation succeeded.
    ASSERT_EQ(result.get(), FILE_SUCCESS);

    std::string dataAfter;
    const std::string expected("sf1\0\0", 5);
    DateTime modifiedAfter;

    // Wait for FUSE to see the File Service's changes.
    EXPECT_TRUE(waitFor(
        [&]()
        {
            dataAfter = readFile(filePath);
            modifiedAfter = lastWriteTime(filePath);

            return dataAfter == expected && modifiedAfter > now;
        },
        mDefaultTimeout));

    EXPECT_EQ(dataAfter, expected);
    EXPECT_GT(modifiedAfter, now);

    if (HasFailure())
        return;

    // Track the file's previous modification time
    auto modifiedBefore = modifiedAfter;

    // Make sure the file's modification time changes.
    std::this_thread::sleep_for(std::chrono::seconds(2));

    // Shrink the file's size.
    result = truncate(2);

    // Wait for our truncation to complete.
    ASSERT_NE(result.wait_for(mDefaultTimeout), std::future_status::timeout);

    // Make sure the truncation succeeded.
    ASSERT_EQ(result.get(), FILE_SUCCESS);

    dataBefore = dataAfter;
    modifiedBefore = modifiedAfter;

    // Wait for FUSE to see the File Service's changes.
    EXPECT_TRUE(waitFor(
        [&]()
        {
            dataAfter = readFile(filePath);
            modifiedAfter = lastWriteTime(filePath);
            return dataAfter == "sf" && modifiedBefore < modifiedAfter;
        },
        mDefaultTimeout));

    EXPECT_EQ(dataAfter, "sf");
    EXPECT_GT(modifiedAfter, modifiedBefore);
}

TEST_F(FUSEFileServiceTests, perceives_file_service_writes_succeeds)
{
    // Convenience.
    const auto filePath = MountPathW() / "sf0";

    // Retrieve the file's current data and modification time.
    auto dataBefore = readFile(filePath);
    auto modifiedBefore = fs::last_write_time(filePath);

    // Make sure the file's data is as we expect.
    ASSERT_EQ(dataBefore, "sf0");

    // Get the handle of our test file.
    auto handle = ClientW()->handle("/x/s/sf0");
    ASSERT_EQ(handle.errorOr(API_OK), API_OK);

    // Open the file for IO.
    auto file = ClientW()->fileService().open(FileID::from(*handle));
    ASSERT_EQ(file.errorOr(FILE_SERVICE_SUCCESS), FILE_SERVICE_SUCCESS);

    // Writes some data to our test file.
    auto write = [&file](const auto* data, auto offset, auto length)
    {
        auto notifier = makeSharedPromise<FileResultOr<FileWriteResult>>();

        file->write(
            data,
            [notifier](auto result)
            {
                notifier->set_value(result);
            },
            offset,
            length);

        return notifier->get_future();
    }; // write

    // Wait a couple seconds to make sure the file's modification time changes.
    std::this_thread::sleep_for(std::chrono::seconds(2));

    // Overwrite some data in our test file.
    auto result = write("ab", 0, 2);

    // Wait for our write to complete.
    ASSERT_NE(result.wait_for(mDefaultTimeout), std::future_status::timeout);

    // Make sure our write succeeded.
    ASSERT_EQ(result.get().errorOr(FILE_SUCCESS), FILE_SUCCESS);

    std::string dataAfter;
    fs::file_time_type modifiedAfter;

    // Wait for FUSE to see the File Service's changes.
    EXPECT_TRUE(waitFor(
        [&]()
        {
            dataAfter = readFile(filePath);
            modifiedAfter = fs::last_write_time(filePath);
            return dataAfter == "ab0" && modifiedBefore < modifiedAfter;
        },
        mDefaultTimeout));

    // Make sure FUSE sees the File Service's modifications.
    EXPECT_EQ(dataAfter, "ab0");
    EXPECT_GT(modifiedAfter, modifiedBefore);

    if (HasFailure())
        return;

    // Make sure the file's modification time changes.
    std::this_thread::sleep_for(std::chrono::seconds(2));

    // Write some data to the end of our test file.
    result = write("cd", 3, 2);

    // Make sure our write has completed.
    ASSERT_NE(result.wait_for(mDefaultTimeout), std::future_status::timeout);

    // Make sure our write succeeded.
    ASSERT_EQ(result.get().errorOr(FILE_SUCCESS), FILE_SUCCESS);

    dataBefore = std::move(dataAfter);
    modifiedBefore = modifiedAfter;

    // Wait for FUSE to see the File Service's changes.
    EXPECT_TRUE(waitFor(
        [&]()
        {
            dataAfter = readFile(filePath);
            modifiedAfter = fs::last_write_time(filePath);
            return dataAfter == "ab0cd" && modifiedBefore < modifiedAfter;
        },
        mDefaultTimeout));

    // Make sure FUSE sees the File Service's changes.
    EXPECT_EQ(dataAfter, "ab0cd");
    EXPECT_GT(modifiedAfter, modifiedBefore);
}

} // testing
} // fuse
} // mega
