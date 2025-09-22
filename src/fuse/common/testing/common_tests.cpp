#include "megafs.h"
#include "sdk_test_data_provider.h"

#include <mega/common/error_or.h>
#include <mega/common/node_info.h>
#include <mega/fuse/common/mount_event.h>
#include <mega/fuse/common/mount_event_type.h>
#include <mega/fuse/common/mount_info.h>
#include <mega/fuse/common/mount_result.h>
#include <mega/fuse/common/testing/client.h>
#include <mega/fuse/common/testing/cloud_path.h>
#include <mega/fuse/common/testing/file.h>
#include <mega/fuse/common/testing/mount_event_observer.h>
#include <mega/fuse/common/testing/printers.h>
#include <mega/fuse/common/testing/test_base.h>
#include <mega/fuse/common/testing/utility.h>
#include <mega/fuse/platform/platform.h>
#include <mega/fuse/platform/testing/wrappers.h>

#include <filesystem>
#include <fstream>

namespace mega
{
namespace fuse
{
namespace testing
{

using namespace common;

struct FUSECommonTests
  : TestBase
{
}; // FUSECommonTests

static handle fsidOf(const Path& path);

static bool makeFile(const Path& path, const std::string& data);

static bool makeFile(const Path& path, std::size_t size);

static std::string readFile(const Path& path);

TEST_F(FUSECommonTests, cached_false_when_directory)
{
    ASSERT_FALSE(ClientW()->isCached(MountPathW()));
    ASSERT_FALSE(ClientW()->isCached(MountPathW() / "sd0"));
}

TEST_F(FUSECommonTests, cached_false_when_not_cached)
{
    ASSERT_FALSE(ClientW()->isCached(MountPathW() / "sf0"));
}

TEST_F(FUSECommonTests, cached_false_when_unknown)
{
    ASSERT_FALSE(ClientW()->isCached(MountPathW() / "sfx"));
    ASSERT_FALSE(ClientW()->isCached(MountPathW() / "sf0" / "sd0"));
}

TEST_F(FUSECommonTests, cached_true_when_cached)
{
    ASSERT_FALSE(ClientW()->isCached(MountPathW() / "sf0"));

    auto data = readFile(MountPathW() / "sf0");
    ASSERT_FALSE(data.empty());

    ASSERT_TRUE(ClientW()->isCached(MountPathW() / "sf0"));

    std::error_code error;

    EXPECT_TRUE(fs::remove(MountPathW() / "sf0", error));
    ASSERT_FALSE(error);

    ASSERT_TRUE(waitFor([&]() {
        return !ClientW()->isCached(MountPathW() / "sf0");
    }, mDefaultTimeout));
}

TEST_F(FUSECommonTests, cloud_add)
{
    auto handle = ClientW()->makeDirectory("sdx", "/x/s");
    ASSERT_TRUE(handle);

    std::error_code error;

    EXPECT_TRUE(waitFor([&]() {
        return fs::is_directory(MountPathW() / "sdx", error)
               && fsidOf(MountPathW() / "sdx") == handle->as8byte();
    }, mDefaultTimeout));

    EXPECT_TRUE(fs::is_directory(MountPathW() / "sdx", error));
    EXPECT_FALSE(error);
    EXPECT_EQ(fsidOf(MountPathW() / "sdx"), handle->as8byte());
}

TEST_F(FUSECommonTests, cloud_add_replace)
{
    ASSERT_TRUE(makeFile(MountPathW() / "sfx", 32));

    auto handle = ClientW()->makeDirectory("sfx", "/x/s");
    ASSERT_TRUE(handle);

    std::error_code error;

    EXPECT_TRUE(waitFor([&]() {
        return fs::is_directory(MountPathW() / "sfx", error)
               && fsidOf(MountPathW() / "sfx") == handle->as8byte();
    }, mDefaultTimeout));

    EXPECT_TRUE(fs::is_directory(MountPathW() / "sfx", error));
    EXPECT_FALSE(error);
    EXPECT_EQ(fsidOf(MountPathW() / "sfx"), handle->as8byte());
}

TEST_F(FUSECommonTests, cloud_move)
{
    auto id = fsidOf(MountPathW() / "sf0");
    ASSERT_NE(id, UNDEF);

    ASSERT_EQ(ClientW()->move("sf0", "/x/s/sf0", "/x/s/sd0"), API_OK);

    std::error_code error;

    EXPECT_TRUE(waitFor([&]() {
        return !fs::exists(MountPathW() / "sf0", error)
               && fs::is_regular_file(MountPathW() / "sd0" / "sf0", error)
               && fsidOf(MountPathW() / "sd0" / "sf0") == id;
    }, mDefaultTimeout));

    EXPECT_FALSE(fs::exists(MountPathW() / "sf0", error));
    EXPECT_FALSE(error);
    EXPECT_TRUE(fs::is_regular_file(MountPathW() / "sd0" / "sf0", error));
    EXPECT_FALSE(error);
    EXPECT_EQ(fsidOf(MountPathW() / "sd0" / "sf0"), id);
}

TEST_F(FUSECommonTests, cloud_move_rename_replace)
{
    ASSERT_TRUE(makeFile(MountPathW() / "sfx", 32));

    auto info = ClientW()->get("/x/s/sd0/sd0d0");
    ASSERT_TRUE(info);

    ASSERT_EQ(ClientW()->move("sfx", "/x/s/sd0/sd0d0", "/x/s"), API_OK);

    std::error_code error;

    EXPECT_TRUE(waitFor([&]() {
        return !fs::exists(MountPathW() / "sd0" / "sd0d0", error)
               && fs::is_directory(MountPathW() / "sfx", error)
               && fsidOf(MountPathW() / "sfx") == info->mHandle.as8byte();
    }, mDefaultTimeout));

    EXPECT_FALSE(fs::exists(MountPathW() / "sd0" / "sd0d0", error));
    EXPECT_FALSE(error);
    EXPECT_TRUE(fs::is_directory(MountPathW() / "sfx", error));
    EXPECT_FALSE(error);
    EXPECT_EQ(fsidOf(MountPathW() / "sfx"), info->mHandle.as8byte());

}

TEST_F(FUSECommonTests, cloud_move_replace)
{
    ASSERT_TRUE(makeFile(MountPathW() / "sfx", 32));

    auto info = ClientW()->get("/x/s/sd0");
    ASSERT_TRUE(info);

    ASSERT_EQ(ClientW()->move("sfx", "/x/s/sd0", "/x/s"), API_OK);

    std::error_code error;

    EXPECT_TRUE(waitFor([&]() {
        return fs::is_directory(MountPathW() / "sfx", error)
               && fsidOf(MountPathW() / "sfx") == info->mHandle.as8byte();
    }, mDefaultTimeout));

    EXPECT_TRUE(fs::is_directory(MountPathW() / "sfx", error));
    EXPECT_FALSE(error);
    EXPECT_EQ(fsidOf(MountPathW() / "sfx"), info->mHandle.as8byte());
}

TEST_F(FUSECommonTests, cloud_remove)
{
    ASSERT_EQ(ClientW()->remove("/x/s/sf0"), API_OK);

    ASSERT_TRUE(waitFor([&]() {
        std::error_code error;
        return !fs::exists(MountPathW() / "sf0", error);
    }, mDefaultTimeout));
}

TEST_F(FUSECommonTests, cloud_rename)
{
    auto info = ClientW()->get("/x/s/sf0");
    ASSERT_TRUE(info);

    ASSERT_EQ(ClientW()->move("sfx", "/x/s/sf0", "/x/s"), API_OK);

    std::error_code error;

    EXPECT_TRUE(waitFor([&]() {
        return !fs::exists(MountPathW() / "sf0", error)
               && fs::exists(MountPathW() / "sfx", error)
               && fsidOf(MountPathW() / "sfx") == info->mHandle.as8byte();
    }, mDefaultTimeout));

    EXPECT_FALSE(fs::exists(MountPathW() / "sf0", error));
    EXPECT_FALSE(error);
    EXPECT_TRUE(fs::exists(MountPathW() / "sfx", error));
    EXPECT_FALSE(error);
    EXPECT_EQ(fsidOf(MountPathW() / "sfx"), info->mHandle.as8byte());
}

TEST_F(FUSECommonTests, cloud_rename_replace)
{
    ASSERT_TRUE(makeFile(MountPathW() / "sfx", 64));

    auto info = ClientW()->get("/x/s/sd0");
    ASSERT_TRUE(info);

    ASSERT_EQ(ClientW()->move("sfx", "/x/s/sd0", "/x/s"), API_OK);
    
    std::error_code error;

    EXPECT_TRUE(waitFor([&]() {
        return !fs::exists(MountPathW() / "sd0", error)
               && fs::is_directory(MountPathW() / "sfx", error)
               && fsidOf(MountPathW() / "sfx") == info->mHandle.as8byte();
    }, mDefaultTimeout));

    EXPECT_FALSE(fs::exists(MountPathW() / "sd0", error));
    EXPECT_FALSE(error);
    EXPECT_TRUE(fs::is_directory(MountPathW() / "sfx", error));
    EXPECT_FALSE(error);
    EXPECT_EQ(fsidOf(MountPathW() / "sfx"), info->mHandle.as8byte());
}

TEST_F(FUSECommonTests, cloud_replace)
{
    ASSERT_TRUE(makeFile(MountPathW() / "sfx", 32));

    auto handle = ClientW()->makeDirectory("sfx", "/x/s");
    ASSERT_TRUE(handle);

    std::error_code error;

    EXPECT_TRUE(waitFor([&]() {
        return fs::is_directory(MountPathW() / "sfx", error)
               && fsidOf(MountPathW() / "sfx") == handle->as8byte();
    }, mDefaultTimeout));

    EXPECT_TRUE(fs::is_directory(MountPathW() / "sfx", error));
    EXPECT_FALSE(error);
    EXPECT_EQ(fsidOf(MountPathW() / "sfx"), handle->as8byte());
}

TEST_F(FUSECommonTests, duplicate_names)
{
    // Add a few duplicate directories.
    ASSERT_EQ(ClientW()->makeDirectory("sd0", "/x/s").errorOr(API_OK), API_OK);
    ASSERT_EQ(ClientW()->makeDirectory("sd0", "/x/s").errorOr(API_OK), API_OK);

    // Sanity.
    EXPECT_EQ(ClientW()->childNames("/x/s").count("sd0"), 0u);

    // Wait for the directory to become inaccessible.
    std::error_code error;

    EXPECT_TRUE(waitFor([&]() {
        return !fs::exists(MountPathW() / "sd0", error);
    }, mDefaultTimeout));

    EXPECT_FALSE(fs::exists(MountPathW() / "sd0", error));
    EXPECT_FALSE(error);
}

TEST_F(FUSECommonTests, file_cache_load)
{
    // Create a new client so not to interfere with later tests.
    auto client = CreateClient("filecache_" + randomName());
    ASSERT_TRUE(client);

    // Log the client in.
    ASSERT_EQ(client->login(1), API_OK);

    // Add a new mount.
    MountInfo mount;

    mount.mHandle = client->handle("/x/s");
    mount.name("s");
    mount.mFlags.mPersistent = true;
    mount.mPath = client->storagePath() / "s";

    UNIX_ONLY(ASSERT_TRUE(fs::create_directories(Path(mount.mPath))));

    ASSERT_EQ(client->addMount(mount), MOUNT_SUCCESS);

    // Enable the mount.
    ASSERT_EQ(client->enableMount(mount.name(), false), MOUNT_SUCCESS);

    // Create a new file.
    auto sfxData = randomBytes(32);
    auto sfxPath = client->storagePath() / "s" / "sfx";

    ASSERT_TRUE(makeFile(sfxPath, sfxData));

    // Modify an existing file.
    auto sf0Path = client->storagePath() / "s" / "sf0";

    ASSERT_TRUE(makeFile(sf0Path, 32));

    // Capture the file's ID.
    auto id = fsidOf(sfxPath);
    ASSERT_NE(id, UNDEF);

    // Disable the mount.
    auto observer = client->mountEventObserver();

    observer->expect({
        mount.name(),
        MOUNT_SUCCESS,
        MOUNT_DISABLED
    });

    ASSERT_EQ(client->disableMounts(false), MOUNT_SUCCESS);
    ASSERT_TRUE(observer->wait(mDefaultTimeout));

    // Log the client out and back in again (to prevent file upload.)
    {
        auto sessionToken = client->sessionToken();

        ASSERT_EQ(client->logout(true), API_OK);
        ASSERT_EQ(client->login(sessionToken), API_OK);
    }

    // Re-enable the mount.
    ASSERT_EQ(client->enableMount(mount.name(), false), MOUNT_SUCCESS);

    // Try and read the file's data back.
    ASSERT_EQ(readFile(sfxPath), sfxData);

    // Erase sf0.
    ASSERT_EQ(client->remove("/x/s/sf0"), API_OK);

    // Make sure we can't access sf0.
    std::error_code error;

    ASSERT_FALSE(fs::exists(sf0Path, error));
    ASSERT_FALSE(error);

    // Disable the mount.
    observer->expect({
        mount.name(),
        MOUNT_SUCCESS,
        MOUNT_DISABLED
    });

    ASSERT_EQ(client->disableMounts(false), MOUNT_SUCCESS);
    ASSERT_TRUE(observer->wait(mDefaultTimeout));

    // Log out the client.
    ASSERT_EQ(client->logout(false), API_OK);

    std::filebuf x;
}

TEST_F(FUSECommonTests, reload)
{
    // Create a new client so not to interfere with future tests.
    auto client = CreateClient("reload_" + randomName());

    // Log the client in.
    ASSERT_EQ(client->login(1), API_OK);

    // Add a new mount.
    MountInfo mount;

    mount.mHandle = client->handle("/x/s");
    mount.name("s");
    mount.mPath = client->storagePath() / "s";

    UNIX_ONLY(ASSERT_TRUE(fs::create_directories(Path(mount.mPath))));

    ASSERT_EQ(client->addMount(mount), MOUNT_SUCCESS);

    // Enable the mount.
    ASSERT_EQ(client->enableMount(mount.name(), false), MOUNT_SUCCESS);

    // Bring a few inodes into memory.

    // sd0 will be renamed to sdx.
    auto sd0i = fsidOf(client->storagePath() / "s" / "sd0");
    ASSERT_NE(sd0i, UNDEF);

    // sd1 will be removed.
    ASSERT_NE(fsidOf(client->storagePath() / "s" / "sd1"), UNDEF);

    // sd2 will be added.
    ASSERT_EQ(fsidOf(client->storagePath() / "s" / "sd2"), UNDEF);

    // sf0 will be moved to sdx/sf0.
    auto sf0i = fsidOf(client->storagePath() / "s" / "sf0");
    ASSERT_NE(sf0i, UNDEF);

    // sf2 will be added.
    ASSERT_EQ(fsidOf(client->storagePath() / "s" / "sf2"), UNDEF);

    // Tell FUSE to ignore node events.
    ASSERT_EQ(client->discard(true), MOUNT_SUCCESS);

    // Rename sd0 to sdx.
    ASSERT_EQ(client->move("sdx", "/x/s/sd0", "/x/s"), API_OK);

    // Remove sd1.
    ASSERT_EQ(client->remove("/x/s/sd1"), API_OK);

    // Add sd2.
    ASSERT_EQ(client->makeDirectory("sd2", "/x/s").errorOr(API_OK), API_OK);

    // Move sf0 to sdx/sf0.
    ASSERT_EQ(client->move("sf0", "/x/s/sf0", "/x/s/sdx"), API_OK);

    // Add sf2.
    {
        // Create a file to be uploaded.
        File sf2("sf2", "sf2", mScratchPath);

        // Upload the file.
        ASSERT_EQ(client->upload("/x/s", sf2.path()).errorOr(API_OK), API_OK);
    }

    // Simulate ETOOMANY by reloading the cloud tree.
    ASSERT_EQ(client->reload(), API_OK);

    EXPECT_TRUE(waitFor([&]() {
        // sd0 should no longer be visible.
        if (fsidOf(client->storagePath() / "s" / "sd0") != UNDEF)
            return false;

        // sd1 should no longer be visible.
        if (fsidOf(client->storagePath() / "s" / "sd1") != UNDEF)
            return false;

        // sd2 should be visible.
        if (fsidOf(client->storagePath() / "s" / "sd2") == UNDEF)
            return false;

        // sf0 should no longer be visible.
        if (fsidOf(client->storagePath() / "s" / "sf0") != UNDEF)
            return false;

        // sf2 should now be visible.
        if (fsidOf(client->storagePath() / "s" / "sf2") == UNDEF)
            return false;

        // sd0 should now be known as sdx.
        if (fsidOf(client->storagePath() / "s" / "sdx") != sd0i)
            return false;

        // sf0 should be visible under sdx.
        return fsidOf(client->storagePath() / "s" / "sdx" / "sf0") == sf0i;
    }, mDefaultTimeout));

    EXPECT_EQ(fsidOf(client->storagePath() / "s" / "sd0"), UNDEF);
    EXPECT_EQ(fsidOf(client->storagePath() / "s" / "sd1"), UNDEF);
    EXPECT_NE(fsidOf(client->storagePath() / "s" / "sd2"), UNDEF);
    EXPECT_EQ(fsidOf(client->storagePath() / "s" / "sf0"), UNDEF);
    EXPECT_NE(fsidOf(client->storagePath() / "s" / "sf2"), UNDEF);
    EXPECT_EQ(fsidOf(client->storagePath() / "s" / "sdx"), sd0i);
    EXPECT_EQ(fsidOf(client->storagePath() / "s" / "sdx" / "sf0"), sf0i);
}

TEST_F(FUSECommonTests, share_changes_permissions)
{
    // Convenience.
    constexpr auto U_R =
      UNIX_OR_WINDOWS(fs::perms::owner_read,
                      fs::perms::_File_attribute_readonly);

    UNIX_ONLY(constexpr auto U_W = fs::perms::owner_write);
    constexpr auto U_X = fs::perms::owner_exec;

    constexpr auto U_RW  = UNIX_OR_WINDOWS(U_R | U_W, fs::perms::all);
    constexpr auto U_RWX = U_RW | U_X;
    constexpr auto U_RX  = UNIX_OR_WINDOWS(U_R | U_X, U_RWX);

    // Conveniently queries a file's permissions.
    auto permissions = [](const Path& path) -> ErrorOr<fs::perms> {
        auto error  = std::error_code();
        auto status = fs::status(path, error);

        if (!error)
            return status.permissions();

        return unexpected(API_EREAD);
    }; // permissions

    // Verify initial permissions are as we expect.
    auto perms = permissions(MountPathRS() / "sd0");
    ASSERT_EQ(perms.errorOr(API_OK), API_OK);
    EXPECT_EQ(perms.value(), U_RX);

    perms = permissions(MountPathRS() / "sf0");
    ASSERT_EQ(perms.errorOr(API_OK), API_OK);
    EXPECT_EQ(perms.value(), U_R);

    perms = permissions(MountPathWS() / "sd0");
    ASSERT_EQ(perms.errorOr(API_OK), API_OK);
    EXPECT_EQ(perms.value(), U_RWX);

    perms = permissions(MountPathWS() / "sf0");
    ASSERT_EQ(perms.errorOr(API_OK), API_OK);
    EXPECT_EQ(perms.value(), U_RW);

    // Change permissions of shares.
    {
        auto email = ClientS()->email();

        auto rs = ClientR()->handle("/x/s");
        ASSERT_FALSE(rs.isUndef());

        auto ws = ClientW()->handle("/x/s");
        ASSERT_FALSE(ws.isUndef());

        // Make read-only share writable.
        ASSERT_EQ(ClientR()->share(email, rs, FULL), API_OK);

        // Make writalbe share read-only.
        ASSERT_EQ(ClientW()->share(email, ws, RDONLY), API_OK);

        // Wait for sharee to recognize permission changes.
        ASSERT_TRUE(waitFor([&]() {
            auto rs_ = ClientS()->get(rs);
            auto ws_ = ClientS()->get(ws);

            return (rs_ && rs_->mPermissions == FULL)
                   && (ws_ && ws_->mPermissions == RDONLY);
        }, mDefaultTimeout));
    }

    // Wait for mounts to recognize new permissions.
    ASSERT_TRUE(waitFor([&]() {
        auto perms = permissions(MountPathRS() / "sd0");

        if (!perms || *perms != U_RWX)
            return false;

        perms = permissions(MountPathRS() / "sf0");

        if (!perms || *perms != U_RW)
            return false;

        perms = permissions(MountPathWS() / "sd0");

        if (!perms || *perms != U_RX)
            return false;

        perms = permissions(MountPathWS() / "sf0");

        return perms && *perms == U_R;
    }, mDefaultTimeout));
}

TEST_F(FUSECommonTests, supports_entities_with_international_names)
{
    static const std::string directoryName = "測試目錄";
    static const std::string fileName = "測試文件";

    // Convenience.
    auto sd0 = ClientW()->handle("x/s/sd0");
    ASSERT_FALSE(sd0.isUndef());

    auto sd0f0 = ClientW()->handle("x/s/sd0/sd0f0");
    ASSERT_FALSE(sd0f0.isUndef());

    // Give some cloud entities an internationalized name.
    ASSERT_EQ(ClientW()->move(fileName, "x/s/sd0/sd0f0", "x/s/sd0"), API_OK);
    ASSERT_EQ(ClientW()->move(directoryName, "x/s/sd0", "x/s"), API_OK);

    // Wait for our changes to be recognized by the SDK.
    ASSERT_TRUE(waitFor([&]() {
        auto info = ClientW()->get(sd0);

        if (!info || info->mName != directoryName)
            return false;

        info = ClientW()->get(sd0f0);

        return info && info->mName == fileName;
    }, mDefaultTimeout));

    // Wait for our changes to be visible via our mount.
    ASSERT_TRUE(waitFor([&]() {
        // Convenience.
        auto dp = MountPathW() / directoryName;
        auto fp = dp / fileName;

        // Check for presence of directory and file.
        std::error_code error;

        return fs::is_directory(dp, error)
               && !error
               && fs::is_regular_file(fp, error)
               && !error;
    }, mDefaultTimeout));

    // Make sure we can actually operate on these entities.
    ASSERT_EQ(readFile(MountPathW() / directoryName / fileName), "sd0f0");

    // Restore original names.
    std::error_code error;

    fs::rename(MountPathW() / directoryName / fileName,
               MountPathW() / directoryName / "sd0f0",
               error);

    ASSERT_FALSE(error);

    fs::rename(MountPathW() / directoryName, MountPathW() / "sd0", error);
    ASSERT_FALSE(error);

    // Wait for changes to be visible in the cloud.
    ASSERT_TRUE(waitFor([&]() {
        return ClientW()->handle("x/s/sd0/sd0f0") == sd0f0
               && ClientW()->handle("x/s/sd0") == sd0;
    }, mDefaultTimeout));
}

TEST_F(FUSECommonTests, supports_gfx)
{
    constexpr fatype THUMBNAIL = 0;
    constexpr fatype PREVIEW = 1;
    static constexpr const char* imageName = "logo.png";
    static constexpr const char* imagePath = "/x/s/logo.png";

    // Download a test image from artifactory.
    auto mountPath = MountPathW() / imageName;
    ASSERT_TRUE(getFileFromArtifactory(std::string{"test-data/"} + imageName, mountPath));

    // Make sure the file is flushed to the cloud.
    ASSERT_TRUE(flushFile(mountPath));

    EXPECT_TRUE(waitFor(
        [&]()
        {
            auto info = ClientW()->get(imagePath);

            // File isn't in the cloud.
            if (!info)
                return false;

            // File doesn't have expected attributes.
            if (!ClientW()->hasFileAttribute(info->mHandle, THUMBNAIL))
                return false;

            if (!ClientW()->hasFileAttribute(info->mHandle, PREVIEW))
                return false;

            return true;
        },
        mDefaultTimeout));
}

handle fsidOf(const Path& path)
{
    static FSACCESS_CLASS fsAccess;

    return fsAccess.fsidOf(path.localPath(),
                           false,
                           true,
                           FSLogging::logOnError);
}

bool makeFile(const Path& path, const std::string& data)
{
    std::ofstream ostream(path.string(), std::ios::binary | std::ios::trunc);

    if (!ostream)
        return false;

    ostream.write(data.data(), static_cast<std::streamsize>(data.size()));

    if (!ostream)
        return false;

    ostream.flush();

    if (!ostream)
        return false;

    ostream.close();

    return !!ostream;
}

bool makeFile(const Path& path, std::size_t size)
{
    return makeFile(path, randomBytes(size));
}

std::string readFile(const Path& path)
{
    std::error_code error;

    auto size = fs::file_size(path, error);

    if (error)
        return std::string();

    std::ifstream istream(path.path(), std::ios::binary);

    if (!istream)
        return std::string();

    std::string buffer(static_cast<std::size_t>(size), '\0');

    istream.read(&buffer[0], static_cast<std::streamsize>(size));

    buffer.resize(static_cast<std::size_t>(istream.gcount()));

    return buffer;
}

} // testing
} // fuse
} // mega

