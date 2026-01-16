#include "mega/megaapp.h"
#include "mega/megaclient.h"
#include "sdk_test_utils.h"
#include "utils.h"

#include <gtest/gtest.h>

#include <filesystem>

using namespace mega;
using namespace sdk_test;

namespace
{
class MegaClientTest: public ::testing::Test
{
protected:
    void SetUp() override
    {
        app = std::make_shared<MegaApp>();
        client = mt::makeClient(*app);
    }

    void TearDown() override
    {
        client.reset();
        app.reset();
    }

    std::shared_ptr<MegaApp> app;
    std::shared_ptr<MegaClient> client;
    handle testHandle = 0x1234;
};

TEST_F(MegaClientTest, isValidLocalSyncRoot_OK)
{
    const fs::path dirPath = fs::current_path() / "megaclient_test_valid_local_sync_root";
    LocalTempDir tempDir(dirPath);
    LocalPath localPath = LocalPath::fromAbsolutePath(path_u8string(dirPath));
    const auto [err, sErr, sWarn] = client->isValidLocalSyncRoot(localPath, testHandle);
    EXPECT_EQ(err, API_OK);
    EXPECT_EQ(sErr, NO_SYNC_ERROR);
    EXPECT_EQ(sWarn, NO_SYNC_WARNING);
}

TEST_F(MegaClientTest, isValidLocalSyncRoot_NotAbsolutePath)
{
    const fs::path relPath = fs::path("relative") / "path" / "to" / "dir";
    LocalPath localPath = LocalPath::fromRelativePath(path_u8string(relPath));
    const auto [err, sErr, sWarn] = client->isValidLocalSyncRoot(localPath, testHandle);
    EXPECT_EQ(err, API_EARGS);
    EXPECT_EQ(sErr, NO_SYNC_ERROR);
    EXPECT_EQ(sWarn, NO_SYNC_WARNING);
}

TEST_F(MegaClientTest, isValidLocalSyncRoot_NonExistentPath)
{
    const fs::path dirPath = fs::current_path() / "megaclient_test_non_existent_path";
    LocalPath localPath = LocalPath::fromAbsolutePath(path_u8string(dirPath));
    const auto [err, sErr, sWarn] = client->isValidLocalSyncRoot(localPath, testHandle);
    EXPECT_EQ(err, API_ENOENT);
    EXPECT_EQ(sErr, LOCAL_PATH_UNAVAILABLE);
    EXPECT_EQ(sWarn, NO_SYNC_WARNING);
}

TEST_F(MegaClientTest, isValidLocalSyncRoot_NotAFolder)
{
    const fs::path filePath = fs::current_path() / "megaclient_test_not_a_folder.txt";
    LocalTempFile tempFile(filePath, "Temporary file content");
    LocalPath localPath = LocalPath::fromAbsolutePath(path_u8string(filePath));
    const auto [err, sErr, sWarn] = client->isValidLocalSyncRoot(localPath, testHandle);
    EXPECT_EQ(err, API_EACCESS);
    EXPECT_EQ(sErr, INVALID_LOCAL_TYPE);
    EXPECT_EQ(sWarn, NO_SYNC_WARNING);
}

} // namespace