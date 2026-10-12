#include "mega/auto_file_handle.h"
#include "mega/filesystem.h"

#include <gtest/gtest.h>

#include <chrono>
#include <megafs.h>

namespace mega
{
namespace testing
{

struct FileAccessTests: ::testing::Test
{
    FileAccessTests():
        Test(),
        mFilesystem(),
        mFilePath(LocalPath::fromAbsolutePath("file")),
        mFileAccess(mFilesystem.newfileaccess(false))
    {}

    // Called before each test in our fixture executes.
    void SetUp() override
    {
        // Make sure we have no state from a prior test run.
        auto unlinked = mFilesystem.unlinklocal(mFilePath);

        // Unlinking was successful or the file didn't exist.
        ASSERT_TRUE(unlinked || !mFilesystem.target_exists);

        // Convenience.
        auto logOnError = FSLogging::logOnError;

        // Make sure our test file is open for IO.
        ASSERT_TRUE(mFileAccess->fopen(mFilePath, OPEN_RDWR, logOnError));
    }

    // How we interface with the local filesystem.
    FSACCESS_CLASS mFilesystem;

    // The path to our test file.
    LocalPath mFilePath;

    // How we read and write our test file.
    FileAccessPtr mFileAccess;
}; // FileAccessTests

TEST_F(FileAccessTests, frawread_fwrite)
{
    // Data for us to write to disk.
    static const std::string expected = "AAAABBBBCCCCDDDD";

    // Write data in reverse order, in groups of four characters.
    for (std::size_t i = 0, j = expected.size(); i != j; i += 4)
    {
        // Compute offset.
        auto offset = static_cast<m_off_t>(j - i - 4);

        // Tracks how much data we wrote to disk.
        auto count = 0ul;

        // Try and write four characters to disk.
        ASSERT_TRUE(mFileAccess->fwrite(expected.data() + offset, 4, offset, &count));

        // Make sure we actually wrote four characters to disk.
        ASSERT_EQ(count, 4ul);
    }

    // Where we'll store the data we've read.
    std::string computed(4, '\0');

    // Read data in order, in groups of four characters.
    for (std::size_t i = 0ul, j = expected.size() - 4; i != j; i += 4)
    {
        // Convenience.
        auto logOnError = FSLogging::logOnError;

        // Convenience.
        auto offset = static_cast<m_off_t>(i);

        // Try and read four characters from disk.
        ASSERT_TRUE(mFileAccess->frawread(computed.data(), 4, offset, true, logOnError));

        // Make sure we've read what we expected.
        ASSERT_FALSE(expected.compare(i, 4, computed));
    }

    // Convenience.
    auto noLogging = FSLogging::noLogging;

    // Make sure frawread(...) fails if it can't read everything.
    ASSERT_FALSE(mFileAccess->frawread(computed.data(), 4, 14, true, noLogging));
}

TEST_F(FileAccessTests, fread)
{
    // Data we want to write to disk.
    static const std::string expected = "ABCD";

    // Convenience.
    auto logOnError = FSLogging::logOnError;

    // Try and write the data to disk.
    ASSERT_TRUE(mFileAccess->fwrite(expected.data(), 4, 0));

    // Where we'll store the data we've read from disk.
    std::string computed;

    // Read without padding.
    ASSERT_TRUE(mFileAccess->fread(&computed, 4, 0, 0, logOnError));

    // Make sure we've read what we expected.
    EXPECT_EQ(computed, expected);

    // Clear the string.
    computed = std::string(6, '!');

    // Read with padding.
    ASSERT_TRUE(mFileAccess->fread(&computed, 2, 6, 2, logOnError));

    // Make sure the string was correctly padded.
    EXPECT_EQ(computed, std::string("CD\0\0\0\0\0\0", 8));
}

TEST_F(FileAccessTests, ftouch)
{
    // Convenience.
    using std::chrono::minutes;
    using std::chrono::system_clock;

    // Compute some time point in the near future.
    auto after = system_clock::to_time_t(system_clock::now() + minutes(5));

    // Try and update the file's modification time.
    ASSERT_TRUE(mFileAccess->ftouch(after));

    // Reload the file's modification time and size.
    ASSERT_TRUE(mFileAccess->fstat());

    // Make sure the file's modification time is what we set.
    ASSERT_EQ(after, mFileAccess->mtime);
}

TEST_F(FileAccessTests, dupFileDescripttor)
{
    const auto fd = mFileAccess->dupFileDescriptor();
    ASSERT_TRUE(fd.isSet());

    // Data we want to write to disk.
    static const std::string expected = "ABCD";

    // Try and write the data to disk.
    ASSERT_TRUE(mFileAccess->fwrite(expected.data(), expected.size(), 0));

    // Where we'll store the data we've read from disk.
    std::string computed(expected.size(), '\0');

    // Read
    sysread(fd.get(), computed.data(), computed.size(), 0, nullptr, nullptr);

    // Make sure we've read what we expected.
    EXPECT_EQ(computed, expected);
}

struct PlatformAvailableDiskSpaceGuard
{
    const bool previous{FileSystemAccess::usePlatformAvailableDiskSpaceQuery()};

    ~PlatformAvailableDiskSpaceGuard()
    {
        FileSystemAccess::setUsePlatformAvailableDiskSpaceQuery(previous);
    }
};

TEST(FileSystemAccessSwitch, DefaultsToDisabled)
{
    PlatformAvailableDiskSpaceGuard guard;

    FileSystemAccess::setUsePlatformAvailableDiskSpaceQuery(false);
    EXPECT_FALSE(FileSystemAccess::usePlatformAvailableDiskSpaceQuery());

    FileSystemAccess::setUsePlatformAvailableDiskSpaceQuery(true);
    EXPECT_TRUE(FileSystemAccess::usePlatformAvailableDiskSpaceQuery());
}

TEST(FileSystemAccessAvailableDiskSpace, SwitchOffReturnsPositive)
{
    PlatformAvailableDiskSpaceGuard guard;

    FSACCESS_CLASS fs;
    const auto path = LocalPath::fromAbsolutePath(std::filesystem::temp_directory_path().string());

    FileSystemAccess::setUsePlatformAvailableDiskSpaceQuery(false);
    const auto off = fs.availableDiskSpace(path);

    EXPECT_GT(off, 0);
    EXPECT_LT(off, std::numeric_limits<m_off_t>::max());
}

} // testing
} // mega
