#include <gtest/gtest.h>

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
        ASSERT_TRUE(mFileAccess->fopen(mFilePath, true, true, logOnError));
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

} // testing
} // mega
