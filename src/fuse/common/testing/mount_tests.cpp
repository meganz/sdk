#include <mega/common/error_or.h>
#include <mega/common/node_info.h>
#include <mega/fuse/common/mount_event.h>
#include <mega/fuse/common/mount_event_type.h>
#include <mega/fuse/common/mount_info.h>
#include <mega/fuse/common/mount_result.h>
#include <mega/fuse/common/testing/client.h>
#include <mega/fuse/common/testing/cloud_path.h>
#include <mega/fuse/common/testing/directory.h>
#include <mega/fuse/common/testing/file.h>
#include <mega/fuse/common/testing/mount_event_observer.h>
#include <mega/fuse/common/testing/mount_tests.h>
#include <mega/fuse/common/testing/utility.h>
#include <mega/fuse/platform/platform.h>

namespace mega
{
namespace fuse
{
namespace testing
{

TEST_F(FUSEMountTests, add_fails_when_name_isnt_specified)
{
    MountInfo info;

    info.mHandle = ClientW()->handle("/x/s");
    info.mPath = MountPathW();

    auto observer = ClientW()->mountEventObserver();

    observer->expect({
        info.name(),
        MOUNT_NO_NAME,
        MOUNT_ADDED
    });

    ASSERT_EQ(ClientW()->addMount(info), MOUNT_NO_NAME);

    ASSERT_TRUE(observer->wait(mDefaultTimeout));

    ASSERT_TRUE(ClientW()->mounts(false).empty());
}

TEST_F(FUSEMountTests, add_fails_when_source_is_file)
{
    MountInfo info;

    info.mHandle = ClientW()->handle("/x/s/sf0");
    info.name("sf0");
    info.mPath = MountPathW();

    auto observer = ClientW()->mountEventObserver();

    observer->expect({
        info.name(),
        MOUNT_REMOTE_FILE,
        MOUNT_ADDED
    });

    ASSERT_EQ(ClientW()->addMount(info), MOUNT_REMOTE_FILE);

    ASSERT_TRUE(observer->wait(mDefaultTimeout));

    ASSERT_TRUE(ClientW()->mounts(false).empty());
}

TEST_F(FUSEMountTests, add_fails_when_source_is_unknown)
{
    MountInfo info;

    info.name("bogus");
    info.mPath = MountPathW();

    auto observer = ClientW()->mountEventObserver();

    observer->expect({
        info.name(),
        MOUNT_REMOTE_UNKNOWN,
        MOUNT_ADDED
    });

    ASSERT_EQ(ClientW()->addMount(info), MOUNT_REMOTE_UNKNOWN);

    ASSERT_TRUE(observer->wait(mDefaultTimeout));

    ASSERT_TRUE(ClientW()->mounts(false).empty());
}

TEST_F(FUSEMountTests, add_fails_when_target_is_file)
{
    File sf0("sf0", "sf0", mScratchPath);

    MountInfo info;

    info.mHandle = ClientW()->handle("/x/s");
    info.name("s");
    info.mPath = sf0.path();

    auto expected = UNIX_OR_WINDOWS(MOUNT_LOCAL_FILE, MOUNT_LOCAL_EXISTS);
    auto observer = ClientW()->mountEventObserver();

    observer->expect({
        info.name(),
        expected,
        MOUNT_ADDED
    });

    ASSERT_EQ(ClientW()->addMount(info), expected);

    ASSERT_TRUE(observer->wait(mDefaultTimeout));

    ASSERT_TRUE(ClientW()->mounts(false).empty());
}

TEST_F(FUSEMountTests, add_succeeds)
{
    MountInfo info;

    info.mHandle = ClientW()->handle("/x/s");
    info.name("s");
    info.mPath = MountPathW();

    auto observer = ClientW()->mountEventObserver();

    observer->expect({
        info.name(),
        MOUNT_SUCCESS,
        MOUNT_ADDED
    });

    ASSERT_EQ(ClientW()->addMount(info), MOUNT_SUCCESS);

    ASSERT_TRUE(observer->wait(mDefaultTimeout));

    ASSERT_EQ(ClientW()->mounts(false), MountInfoVector(1, info));
}

TEST_F(FUSEMountTests, add_fails_when_name_is_not_unique)
{
    MountInfoVector mounts;

    mounts.emplace_back();
    mounts.back().mHandle = ClientW()->handle("/x/s/sd0");
    mounts.back().name("d");
    mounts.back().mPath = MountPathW();

    auto observer = ClientW()->mountEventObserver();

    observer->expect({
        mounts.back().name(),
        MOUNT_SUCCESS,
        MOUNT_ADDED
    });

    ASSERT_EQ(ClientW()->addMount(mounts.back()), MOUNT_SUCCESS);

    mounts.emplace_back(mounts.back());
    mounts.back().mHandle = ClientW()->handle("/x/s/sd1");
    mounts.back().mPath = MountPathO();

    observer->expect({
        mounts.back().name(),
        MOUNT_NAME_TAKEN,
        MOUNT_ADDED
    });

    ASSERT_EQ(ClientW()->addMount(mounts.back()), MOUNT_NAME_TAKEN);

    ASSERT_TRUE(observer->wait(mDefaultTimeout));

    mounts.pop_back();

    ASSERT_EQ(ClientW()->mounts(false), mounts);
}

TEST_F(FUSEMountTests, add_succeeds_when_node_is_read_only_share)
{
    MountInfo info;

    info.mHandle = ClientR()->handle("/x/s");
    info.name("s");
    info.mPath = MountPathR();

    auto observer = ClientS()->mountEventObserver();

    observer->expect({
        info.name(),
        MOUNT_SUCCESS,
        MOUNT_ADDED
    });

    ASSERT_EQ(ClientS()->addMount(info), MOUNT_SUCCESS);

    ASSERT_TRUE(observer->wait(mDefaultTimeout));

    ASSERT_EQ(ClientS()->mounts(false), MountInfoVector(1, info));
}

TEST_F(FUSEMountTests, add_succeeds_when_node_is_read_write_share)
{
    MountInfo info;

    info.mHandle = ClientW()->handle("/x/s");
    info.name("s");
    info.mPath = MountPathW();

    auto observer = ClientS()->mountEventObserver();

    observer->expect({
        info.name(),
        MOUNT_SUCCESS,
        MOUNT_ADDED
    });

    ASSERT_EQ(ClientS()->addMount(info), MOUNT_SUCCESS);

    ASSERT_TRUE(observer->wait(mDefaultTimeout));

    ASSERT_EQ(ClientS()->mounts(false), MountInfoVector(1, info));
}

TEST_F(FUSEMountTests, add_succeeds_when_target_is_not_unique)
{
    MountInfoVector mounts;

    mounts.emplace_back();
    mounts.back().mHandle = ClientW()->handle("/x/s");
    mounts.back().name("s0");
    mounts.back().mPath = MountPathW();

    auto observer = ClientW()->mountEventObserver();

    observer->expect({
        mounts.back().name(),
        MOUNT_SUCCESS,
        MOUNT_ADDED
    });

    ASSERT_EQ(ClientW()->addMount(mounts.back()), MOUNT_SUCCESS);

    mounts.emplace_back(mounts.back());
    mounts.back().name("s1");
    mounts.back().mPath = MountPathO();

    observer->expect({
        mounts.back().name(),
        MOUNT_SUCCESS,
        MOUNT_ADDED
    });

    ASSERT_EQ(ClientW()->addMount(mounts.back()), MOUNT_SUCCESS);

    ASSERT_TRUE(observer->wait(mDefaultTimeout));

    ASSERT_EQ(ClientW()->mounts(false), mounts);
}

TEST_F(FUSEMountTests, disable_fails_when_mount_unknown)
{
    auto observer = ClientW()->mountEventObserver();

    observer->expect({
        std::string(),
        MOUNT_UNKNOWN,
        MOUNT_DISABLED
    });

    ASSERT_EQ(ClientW()->disableMount(std::string(), false),
              MOUNT_UNKNOWN);

    ASSERT_TRUE(observer->wait(mDefaultTimeout));
}

TEST_F(FUSEMountTests, disable_succeeds_when_mount_disabled)
{
    MountInfo mount;

    mount.mHandle = ClientW()->handle("/x/s");
    mount.name("s");
    mount.mPath = MountPathW();

    auto observer = ClientW()->mountEventObserver();

    observer->expect({
        mount.name(),
        MOUNT_SUCCESS,
        MOUNT_ADDED
    });

    ASSERT_EQ(ClientW()->addMount(mount), MOUNT_SUCCESS);

    observer->expect({
        mount.name(),
        MOUNT_SUCCESS,
        MOUNT_DISABLED
    });

    ASSERT_EQ(ClientW()->disableMount(mount.name(), false),
              MOUNT_SUCCESS);

    ASSERT_TRUE(observer->wait(mDefaultTimeout));
}

TEST_F(FUSEMountTests, disable_succeeds)
{
    MountInfo mount;

    mount.mHandle = ClientW()->handle("/x/s");
    mount.name("s");
    mount.mPath = MountPathW();

    auto observer = ClientW()->mountEventObserver();

    observer->expect({
        mount.name(),
        MOUNT_SUCCESS,
        MOUNT_ADDED
    });

    ASSERT_EQ(ClientW()->addMount(mount), MOUNT_SUCCESS);

    observer->expect({
        mount.name(),
        MOUNT_SUCCESS,
        MOUNT_ENABLED
    });

    ASSERT_EQ(ClientW()->enableMount(mount.name(), false),
              MOUNT_SUCCESS);

    ASSERT_EQ(ClientW()->mounts(true),
              MountInfoVector(1, mount));

    std::error_code error;

    ASSERT_TRUE(waitFor([&]() {
        return fs::exists(SentinelPathW(), error);
    }, mDefaultTimeout));

    observer->expect({
        mount.name(),
        MOUNT_SUCCESS,
        MOUNT_DISABLED
    });

    ASSERT_TRUE(waitFor([&]() {
        return ClientW()->disableMount(mount.name(), false)
               == MOUNT_SUCCESS;
    }, mDefaultTimeout));

    ASSERT_TRUE(observer->wait(mDefaultTimeout));

    ASSERT_TRUE(ClientW()->mounts(true).empty());

    EXPECT_FALSE(fs::exists(SentinelPathW(), error));
    EXPECT_FALSE(error);
}

TEST_F(FUSEMountTests, disable_when_source_removed)
{
    auto result = ClientW()->remove("/t");
    ASSERT_TRUE(result == API_ENOENT || result == API_OK);

    auto handle = ClientW()->makeDirectory("t", "/");
    ASSERT_EQ(handle.errorOr(API_OK), API_OK);
    ASSERT_EQ(ClientW()->makeDirectory("sentinel", "/t").errorOr(API_OK), API_OK);

    MountInfo mount;

    mount.mHandle = *handle;
    mount.name("t");
    mount.mPath = MountPathW();

    auto observer = ClientW()->mountEventObserver();

    observer->expect({
        mount.name(),
        MOUNT_SUCCESS,
        MOUNT_ADDED
    });

    ASSERT_EQ(ClientW()->addMount(mount), MOUNT_SUCCESS);

    observer->expect({
        mount.name(),
        MOUNT_SUCCESS,
        MOUNT_ENABLED,
    });

    ASSERT_EQ(ClientW()->enableMount(mount.name(), false), MOUNT_SUCCESS);

    std::error_code error;

    ASSERT_TRUE(waitFor([&]() {
        return fs::exists(SentinelPathW(), error);
    }, mDefaultTimeout));

    observer->expect({
        mount.name(),
        MOUNT_SUCCESS,
        MOUNT_DISABLED
    });

    ASSERT_EQ(ClientW()->remove(*handle), API_OK);

    ASSERT_TRUE(observer->wait(mDefaultTimeout));

    ASSERT_TRUE(ClientW()->mounts(true).empty());

    EXPECT_FALSE(fs::exists(SentinelPathW(), error));
    EXPECT_FALSE(error);
}

TEST_F(FUSEMountTests, enable_fails_when_mount_unknown)
{
    auto observer = ClientW()->mountEventObserver();

    observer->expect({
        std::string(),
        MOUNT_UNKNOWN,
        MOUNT_ENABLED
    });

    ASSERT_EQ(ClientW()->enableMount(std::string(), false), MOUNT_UNKNOWN);

    ASSERT_TRUE(observer->wait(mDefaultTimeout));

    ASSERT_TRUE(ClientW()->mounts(true).empty());
}

TEST_F(FUSEMountTests, enable_fails_when_source_is_unknown)
{
    auto result = ClientW()->remove("/t");
    ASSERT_TRUE(result == API_ENOENT || result == API_OK);

    auto handle = ClientW()->makeDirectory("t", "/");
    ASSERT_EQ(handle.errorOr(API_OK), API_OK);

    MountInfo mount;

    mount.mHandle = *handle;
    mount.name("t");
    mount.mPath = MountPathW();

    auto observer = ClientW()->mountEventObserver();

    observer->expect({
        mount.name(),
        MOUNT_SUCCESS,
        MOUNT_ADDED
    });

    ASSERT_EQ(ClientW()->addMount(mount), MOUNT_SUCCESS);
    ASSERT_EQ(ClientW()->remove(*handle), API_OK);

    observer->expect({
        mount.name(),
        MOUNT_REMOTE_UNKNOWN,
        MOUNT_ENABLED
    });

    ASSERT_EQ(ClientW()->enableMount(mount.name(), false),
              MOUNT_REMOTE_UNKNOWN);

    ASSERT_TRUE(observer->wait(mDefaultTimeout));

    ASSERT_TRUE(ClientW()->mounts(true).empty());
}

TEST_F(FUSEMountTests, enable_fails_when_target_is_file)
{
    MountInfo mount;

    auto observer = ClientW()->mountEventObserver();

    {
        UNIX_ONLY(Directory sd0("sd0", mScratchPath));

        mount.name("s");
        mount.mHandle = ClientW()->handle("/x/s");
        mount.mPath = mScratchPath / "sd0";

        observer->expect({
            mount.name(),
            MOUNT_SUCCESS,
            MOUNT_ADDED
        });

        ASSERT_EQ(ClientW()->addMount(mount), MOUNT_SUCCESS);
    }

    File sd0("sd0", "sd0", mScratchPath);

    auto expected = UNIX_OR_WINDOWS(MOUNT_LOCAL_FILE, MOUNT_LOCAL_EXISTS);

    observer->expect({
        mount.name(),
        expected,
        MOUNT_ENABLED
    });

    ASSERT_EQ(ClientW()->enableMount(mount.name(), false), expected);

    ASSERT_TRUE(observer->wait(mDefaultTimeout));

    ASSERT_TRUE(ClientW()->mounts(true).empty());

    std::error_code error;

    EXPECT_FALSE(fs::exists(SentinelPathW(), error));
    EXPECT_FALSE(error);
}

TEST_F(FUSEMountTests, enable_fails_when_target_is_not_unique)
{
    MountInfoVector mounts;

    mounts.emplace_back();
    mounts.back().mHandle = ClientW()->handle("/x/s/sd0");
    mounts.back().name("s");
    mounts.back().mPath = MountPathW();

    auto observer = ClientW()->mountEventObserver();

    observer->expect({
        mounts.back().name(),
        MOUNT_SUCCESS,
        MOUNT_ADDED
    });

    ASSERT_EQ(ClientW()->addMount(mounts.back()), MOUNT_SUCCESS);

    mounts.emplace_back(mounts.back());
    mounts.back().mHandle = ClientW()->handle("/x/s/sd1");
    mounts.back().name("t");
    mounts.back().mPath = MountPathW();

    observer->expect({
        mounts.back().name(),
        MOUNT_SUCCESS,
        MOUNT_ADDED
    });

    ASSERT_EQ(ClientW()->addMount(mounts.back()), MOUNT_SUCCESS);

    observer->expect({
        mounts.front().name(),
        MOUNT_SUCCESS,
        MOUNT_ENABLED
    });

    ASSERT_EQ(ClientW()->enableMount(mounts.front().name(), false),
              MOUNT_SUCCESS);

    observer->expect({
        mounts.back().name(),
        MOUNT_LOCAL_TAKEN,
        MOUNT_ENABLED
    });

    ASSERT_EQ(ClientW()->enableMount(mounts.back().name(), false),
              MOUNT_LOCAL_TAKEN);

    ASSERT_TRUE(observer->wait(mDefaultTimeout));

    ASSERT_EQ(ClientW()->mounts(true),
              MountInfoVector(1, mounts.front()));

    ASSERT_EQ(ClientW()->removeMounts(true), MOUNT_SUCCESS);
}

TEST_F(FUSEMountTests, enable_succeeds_when_target_is_unique)
{
    MountInfoVector mounts;

    mounts.emplace_back();
    mounts.back().mHandle = ClientW()->handle("/x/s");
    mounts.back().name("s");
    mounts.back().mPath = MountPathW();

    auto observer = ClientW()->mountEventObserver();

    observer->expect({
        mounts.back().name(),
        MOUNT_SUCCESS,
        MOUNT_ADDED
    });

    ASSERT_EQ(ClientW()->addMount(mounts.back()), MOUNT_SUCCESS);

    mounts.emplace_back(mounts.back());
    mounts.back().name("t");
    mounts.back().mPath = MountPathO();

    observer->expect({
        mounts.back().name(),
        MOUNT_SUCCESS,
        MOUNT_ADDED
    });

    ASSERT_EQ(ClientW()->addMount(mounts.back()), MOUNT_SUCCESS);

    observer->expect({
        mounts.back().name(),
        MOUNT_SUCCESS,
        MOUNT_ENABLED,
    });

    ASSERT_EQ(ClientW()->enableMount(mounts.back().name(), false),
              MOUNT_SUCCESS);

    ASSERT_TRUE(observer->wait(mDefaultTimeout));

    ASSERT_EQ(ClientW()->mounts(true),
              MountInfoVector(1, mounts.back()));

    ASSERT_TRUE(waitFor([&]() {
        std::error_code error;
        return fs::exists(SentinelPathO(), error);
    }, mDefaultTimeout));

    ASSERT_EQ(ClientW()->removeMounts(true), MOUNT_SUCCESS);
}

TEST_F(FUSEMountTests, enable_succeeds)
{
    MountInfo mount;

    mount.mHandle = ClientW()->handle("/x/s");
    mount.name("s");
    mount.mPath = MountPathW();

    auto observer = ClientW()->mountEventObserver();

    observer->expect({
        mount.name(),
        MOUNT_SUCCESS,
        MOUNT_ADDED
    });

    ASSERT_EQ(ClientW()->addMount(mount), MOUNT_SUCCESS);

    observer->expect({
        mount.name(),
        MOUNT_SUCCESS,
        MOUNT_ENABLED
    });

    ASSERT_EQ(ClientW()->enableMount(mount.name(), false), MOUNT_SUCCESS);

    ASSERT_TRUE(observer->wait(mDefaultTimeout));

    ASSERT_EQ(ClientW()->mounts(true), MountInfoVector(1, mount));

    ASSERT_TRUE(waitFor([&]() {
        std::error_code error;
        return fs::exists(SentinelPathW(), error);
    }, mDefaultTimeout));
}

TEST_F(FUSEMountTests, enable_succeeds_when_mount_enabled)
{
    MountInfo mount;

    mount.name("s");
    mount.mHandle = ClientW()->handle("/x/s");
    mount.mPath = MountPathW();

    auto observer = ClientW()->mountEventObserver();

    observer->expect({
        mount.name(),
        MOUNT_SUCCESS,
        MOUNT_ADDED
    });

    ASSERT_EQ(ClientW()->addMount(mount), MOUNT_SUCCESS);

    observer->expect({
        mount.name(),
        MOUNT_SUCCESS,
        MOUNT_ENABLED
    });

    ASSERT_EQ(ClientW()->enableMount(mount.name(), false), MOUNT_SUCCESS);

    observer->expect({
        mount.name(),
        MOUNT_SUCCESS,
        MOUNT_ENABLED
    });

    ASSERT_EQ(ClientW()->enableMount(mount.name(), false), MOUNT_SUCCESS);

    ASSERT_TRUE(observer->wait(mDefaultTimeout));
}

TEST_F(FUSEMountTests, enables_enabled_persisent_mounts_after_login)
{
    auto client = CreateClient("enable_" + randomName());
    ASSERT_TRUE(client);

    ASSERT_EQ(client->login(1), API_OK);

    MountInfo mount;
    
    mount.mHandle = client->handle("/x/s");
    mount.mFlags.mEnableAtStartup = true;
    mount.name("s");
    mount.mFlags.mPersistent = true;
    mount.mPath = client->storagePath() / "s";

    UNIX_ONLY(ASSERT_TRUE(fs::create_directories(Path(mount.mPath))));

    auto observer = client->mountEventObserver();

    observer->expect({
        mount.name(),
        MOUNT_SUCCESS,
        MOUNT_ADDED
    });

    ASSERT_EQ(client->addMount(mount), MOUNT_SUCCESS);

    ASSERT_TRUE(observer->wait(mDefaultTimeout));

    auto sessionToken = client->sessionToken();
    ASSERT_FALSE(sessionToken.empty());

    ASSERT_EQ(client->logout(true), API_OK);

    observer->expect({
        mount.name(),
        MOUNT_SUCCESS,
        MOUNT_ENABLED
    });

    ASSERT_EQ(client->login(sessionToken), API_OK);

    ASSERT_TRUE(observer->wait(mDefaultTimeout));
}

TEST_F(FUSEMountTests, enabled_false_when_disabled)
{
    MountInfo mount;

    mount.mHandle = ClientW()->handle("/x/s");
    mount.name("s");
    mount.mPath = MountPathW();

    auto observer = ClientW()->mountEventObserver();

    observer->expect({
        mount.name(),
        MOUNT_SUCCESS,
        MOUNT_ADDED
    });

    ASSERT_EQ(ClientW()->addMount(mount), MOUNT_SUCCESS);

    ASSERT_FALSE(ClientW()->mountEnabled(mount.name()));

    ASSERT_TRUE(observer->wait(mDefaultTimeout));
}

TEST_F(FUSEMountTests, enabled_false_when_unknown)
{
    ASSERT_FALSE(ClientW()->mountEnabled(std::string()));
}

TEST_F(FUSEMountTests, enabled_true_when_enabled)
{
    MountInfo mount;

    mount.mHandle = ClientW()->handle("/x/s");
    mount.name("s");
    mount.mPath = MountPathW();

    auto observer = ClientW()->mountEventObserver();

    observer->expect({
        mount.name(),
        MOUNT_SUCCESS,
        MOUNT_ADDED
    });

    ASSERT_EQ(ClientW()->addMount(mount), MOUNT_SUCCESS);

    observer->expect({
        mount.name(),
        MOUNT_SUCCESS,
        MOUNT_ENABLED
    });

    ASSERT_EQ(ClientW()->enableMount(mount.name(), false), MOUNT_SUCCESS);

    ASSERT_TRUE(ClientW()->mountEnabled(mount.name()));

    ASSERT_TRUE(observer->wait(mDefaultTimeout));
}

TEST_F(FUSEMountTests, flags_fails_when_enabled_name_not_unique)
{
    MountInfoVector mounts;

    mounts.emplace_back();
    mounts.back().mHandle = ClientW()->handle("/x/s/sd0");
    mounts.back().name("sd0");
    mounts.back().mPath = MountPathW();

    auto observer = ClientW()->mountEventObserver();

    observer->expect({
        mounts.back().name(),
        MOUNT_SUCCESS,
        MOUNT_ADDED
    });

    ASSERT_EQ(ClientW()->addMount(mounts.back()), MOUNT_SUCCESS);

    observer->expect({
        mounts.back().name(),
        MOUNT_SUCCESS,
        MOUNT_ENABLED
    });

    ASSERT_EQ(ClientW()->enableMount(mounts.back().name(), false),
              MOUNT_SUCCESS);

    mounts.emplace_back();
    mounts.back().mHandle = ClientW()->handle("/x/s/sd1");
    mounts.back().name("sd1");
    mounts.back().mPath = MountPathO();

    observer->expect({
        mounts.back().name(),
        MOUNT_SUCCESS,
        MOUNT_ADDED
    });

    ASSERT_EQ(ClientW()->addMount(mounts.back()), MOUNT_SUCCESS);

    observer->expect({
        mounts.back().name(),
        MOUNT_SUCCESS,
        MOUNT_ENABLED
    });

    ASSERT_EQ(ClientW()->enableMount(mounts.back().name(), false),
              MOUNT_SUCCESS);

    auto flags = ClientW()->mountFlags(mounts.back().name());

    ASSERT_TRUE(flags);
    ASSERT_EQ(mounts.back().mFlags, *flags);

    flags->mName = mounts.front().name();

    observer->expect({
        mounts.back().name(),
        MOUNT_NAME_TAKEN,
        MOUNT_CHANGED
    });

    ASSERT_EQ(ClientW()->mountFlags(mounts.back().name(), *flags),
              MOUNT_NAME_TAKEN);

    flags = ClientW()->mountFlags(mounts.back().name());

    ASSERT_TRUE(flags);
    ASSERT_EQ(mounts.back().mFlags, *flags);

    ASSERT_TRUE(observer->wait(mDefaultTimeout));
}

TEST_F(FUSEMountTests, flags_fails_when_mount_unknown)
{
    ASSERT_FALSE(ClientW()->mountFlags(MountPathW()));

    auto observer = ClientW()->mountEventObserver();

    observer->expect({
        std::string(),
        MOUNT_UNKNOWN,
        MOUNT_CHANGED
    });

    MountFlags flags;

    flags.mName = "x";

    ASSERT_EQ(ClientW()->mountFlags(std::string(), flags),
              MOUNT_UNKNOWN);

    ASSERT_TRUE(observer->wait(mDefaultTimeout));
}

TEST_F(FUSEMountTests, flags_fails_when_name_isnt_specified)
{
    MountInfo mount;

    mount.mHandle = ClientW()->handle("/x/s");
    mount.name("s");
    mount.mPath = MountPathW();

    auto observer = ClientW()->mountEventObserver();

    observer->expect({
        mount.name(),
        MOUNT_SUCCESS,
        MOUNT_ADDED
    });

    ASSERT_EQ(ClientW()->addMount(mount), MOUNT_SUCCESS);

    auto flags = mount.mFlags;

    flags.mName.clear();

    observer->expect({
        mount.name(),
        MOUNT_NO_NAME,
        MOUNT_CHANGED
    });

    ASSERT_EQ(ClientW()->mountFlags(mount.name(), flags),
              MOUNT_NO_NAME);

    ASSERT_TRUE(observer->wait(mDefaultTimeout));
}

TEST_F(FUSEMountTests, flags_succeeds)
{
    MountInfo mount;

    mount.mHandle = ClientW()->handle("/x/s");
    mount.name("s");
    mount.mPath = MountPathW();

    auto observer = ClientW()->mountEventObserver();

    observer->expect({
        mount.name(),
        MOUNT_SUCCESS,
        MOUNT_ADDED
    });

    ASSERT_EQ(ClientW()->addMount(mount), MOUNT_SUCCESS);

    auto flags0 = ClientW()->mountFlags(mount.name());

    ASSERT_TRUE(flags0);
    ASSERT_EQ(mount.mFlags, *flags0);

    flags0->mEnableAtStartup = true;
    flags0->mName = "t";
    flags0->mReadOnly = true;
    flags0->mPersistent = true;

    observer->expect({
        mount.name(),
        MOUNT_SUCCESS,
        MOUNT_CHANGED
    });

    ASSERT_EQ(ClientW()->mountFlags(mount.name(), *flags0),
              MOUNT_SUCCESS);

    auto flags1 = ClientW()->mountFlags(flags0->mName);

    ASSERT_TRUE(flags1);

    ASSERT_EQ(*flags0, *flags1);

    ASSERT_TRUE(observer->wait(mDefaultTimeout));
}

TEST_F(FUSEMountTests, info_fails_unknown_mount)
{
    MountInfo mount;

    mount.mHandle = ClientW()->handle("/x/s");
    mount.name("s");
    mount.mPath = MountPathW();

    auto observer = ClientW()->mountEventObserver();

    observer->expect({
        mount.name(),
        MOUNT_SUCCESS,
        MOUNT_ADDED
    });

    ASSERT_EQ(ClientW()->addMount(mount), MOUNT_SUCCESS);

    ASSERT_FALSE(ClientW()->mountInfo("bogus"));

    ASSERT_TRUE(observer->wait(mDefaultTimeout));
}

TEST_F(FUSEMountTests, info_succeeds)
{
    MountInfo mount;

    mount.mHandle = ClientW()->handle("/x/s");
    mount.name("s");
    mount.mPath = MountPathW();

    auto observer = ClientW()->mountEventObserver();

    observer->expect({
        mount.name(),
        MOUNT_SUCCESS,
        MOUNT_ADDED
    });

    ASSERT_EQ(ClientW()->addMount(mount), MOUNT_SUCCESS);

    auto info = ClientW()->mountInfo(mount.name());

    ASSERT_TRUE(info);
    ASSERT_EQ(mount, *info);

    ASSERT_TRUE(observer->wait(mDefaultTimeout));
}

TEST_F(FUSEMountTests, list_all)
{
    ASSERT_TRUE(ClientW()->mounts(false).empty());

    MountInfoVector mounts;

    mounts.emplace_back();
    mounts.back().mHandle = ClientW()->handle("/x/s/sd0");
    mounts.back().name("sd0");
    mounts.back().mPath = MountPathW();

    auto observer = ClientW()->mountEventObserver();

    observer->expect({
        mounts.back().name(),
        MOUNT_SUCCESS,
        MOUNT_ADDED
    });

    ASSERT_EQ(ClientW()->addMount(mounts.back()), MOUNT_SUCCESS);

    mounts.emplace_back();
    mounts.back().mHandle = ClientW()->handle("/x/s/sd1");
    mounts.back().name("sd1");
    mounts.back().mPath = MountPathO();

    observer->expect({
        mounts.back().name(),
        MOUNT_SUCCESS,
        MOUNT_ADDED
    });

    ASSERT_EQ(ClientW()->addMount(mounts.back()), MOUNT_SUCCESS);

    ASSERT_TRUE(observer->wait(mDefaultTimeout));

    auto mounts_ = ClientW()->mounts(false);

    ASSERT_EQ(mounts_.size(), 2u);
    ASSERT_EQ(mounts_, mounts);
}

TEST_F(FUSEMountTests, list_enabled)
{
    MountInfoVector mounts;

    mounts.emplace_back();
    mounts.back().mHandle = ClientW()->handle("/x/s/sd0");
    mounts.back().name("sd0");
    mounts.back().mPath = MountPathW();

    auto observer = ClientW()->mountEventObserver();

    observer->expect({
        mounts.back().name(),
        MOUNT_SUCCESS,
        MOUNT_ADDED
    });

    ASSERT_EQ(ClientW()->addMount(mounts.back()), MOUNT_SUCCESS);

    mounts.emplace_back();
    mounts.back().mHandle = ClientW()->handle("/x/s/sd1");
    mounts.back().name("sd1");
    mounts.back().mPath = MountPathO();

    observer->expect({
        mounts.back().name(),
        MOUNT_SUCCESS,
        MOUNT_ADDED
    });

    ASSERT_EQ(ClientW()->addMount(mounts.back()), MOUNT_SUCCESS);

    ASSERT_TRUE(ClientW()->mounts(true).empty());

    observer->expect({
        mounts.back().name(),
        MOUNT_SUCCESS,
        MOUNT_ENABLED
    });

    ASSERT_EQ(ClientW()->enableMount(mounts.back().name(), false),
              MOUNT_SUCCESS);

    ASSERT_TRUE(observer->wait(mDefaultTimeout));

    auto mounts_ = ClientW()->mounts(true);

    ASSERT_EQ(mounts_.size(), 1u);
    ASSERT_EQ(mounts_.back(), mounts.back());
}

TEST_F(FUSEMountTests, path_distinct_names)
{
    MountInfoVector mounts;

    mounts.emplace_back();
    mounts.back().mHandle = ClientW()->handle("/x/s/sd0");
    mounts.back().name("sd0");
    mounts.back().mPath = MountPathW();

    auto observer = ClientW()->mountEventObserver();

    observer->expect({
        mounts.back().name(),
        MOUNT_SUCCESS,
        MOUNT_ADDED
    });

    ASSERT_EQ(ClientW()->addMount(mounts.back()), MOUNT_SUCCESS);

    mounts.emplace_back();
    mounts.back().mHandle = ClientW()->handle("/x/s/sd1");
    mounts.back().name("sd1");
    mounts.back().mPath = MountPathO();

    observer->expect({
        mounts.back().name(),
        MOUNT_SUCCESS,
        MOUNT_ADDED
    });

    ASSERT_EQ(ClientW()->addMount(mounts.back()), MOUNT_SUCCESS);

    ASSERT_TRUE(observer->wait(mDefaultTimeout));

    auto path = ClientW()->mountPath(mounts.back().name());

    EXPECT_EQ(path, mounts.back().mPath);

    mounts.pop_back();

    path = ClientW()->mountPath(mounts.back().name());

    EXPECT_EQ(path, mounts.back().mPath);
}

TEST_F(FUSEMountTests, path_unused_name)
{
    MountInfo mount;

    mount.mHandle = ClientW()->handle("/x/s");
    mount.name("s");
    mount.mPath = MountPathW();

    auto observer = ClientW()->mountEventObserver();

    observer->expect({
        mount.name(),
        MOUNT_SUCCESS,
        MOUNT_ADDED
    });

    ASSERT_EQ(ClientW()->addMount(mount), MOUNT_SUCCESS);

    ASSERT_TRUE(observer->wait(mDefaultTimeout));

    ASSERT_TRUE(ClientW()->mountPath("t").empty());
}

TEST_F(FUSEMountTests, persistent_mounts_are_persistent)
{
    auto client = CreateClient("persistent_" + randomName());
    ASSERT_TRUE(client);

    ASSERT_EQ(client->login(1), API_OK);

    MountInfo mount;

    mount.mHandle = client->handle("/x/s");
    mount.name("s");
    mount.mFlags.mPersistent = true;
    mount.mPath = client->storagePath() / "s";

    UNIX_ONLY(ASSERT_TRUE(fs::create_directories(Path(mount.mPath))));

    auto observer = client->mountEventObserver();

    observer->expect({
        mount.name(),
        MOUNT_SUCCESS,
        MOUNT_ADDED
    });

    ASSERT_EQ(client->addMount(mount), MOUNT_SUCCESS);

    ASSERT_TRUE(observer->wait(mDefaultTimeout));

    auto sessionToken = client->sessionToken();
    ASSERT_FALSE(sessionToken.empty());

    ASSERT_EQ(client->logout(true), API_OK);
    ASSERT_EQ(client->login(sessionToken), API_OK);

    auto mount_ = client->mountInfo(mount.name());
    ASSERT_TRUE(mount_);
    ASSERT_EQ(*mount_, mount);
}

TEST_F(FUSEMountTests, remember_disable_implies_persistence)
{
    MountInfo mount;
    
    mount.mHandle = ClientW()->handle("/x/s");
    mount.name("s");
    mount.mPath = MountPathW();

    auto observer = ClientW()->mountEventObserver();

    observer->expect({
        mount.name(),
        MOUNT_SUCCESS,
        MOUNT_ADDED
    });

    ASSERT_EQ(ClientW()->addMount(mount), MOUNT_SUCCESS);

    observer->expect({
        mount.name(),
        MOUNT_SUCCESS,
        MOUNT_DISABLED
    });

    ASSERT_EQ(ClientW()->disableMount(mount.name(), true), MOUNT_SUCCESS);

    EXPECT_TRUE(observer->wait(mDefaultTimeout));

    auto flags = ClientW()->mountFlags(mount.name());

    ASSERT_TRUE(flags);
    ASSERT_TRUE(flags->mPersistent);
}

TEST_F(FUSEMountTests, remember_enable_implies_persistence)
{
    MountInfo mount;
    
    mount.mHandle = ClientW()->handle("/x/s");
    mount.name("s");
    mount.mPath = MountPathW();

    auto observer = ClientW()->mountEventObserver();

    observer->expect({
        mount.name(),
        MOUNT_SUCCESS,
        MOUNT_ADDED
    });

    ASSERT_EQ(ClientW()->addMount(mount), MOUNT_SUCCESS);

    observer->expect({
        mount.name(),
        MOUNT_SUCCESS,
        MOUNT_ENABLED
    });

    ASSERT_EQ(ClientW()->enableMount(mount.name(), true), MOUNT_SUCCESS);

    auto flags = ClientW()->mountFlags(mount.name());

    ASSERT_TRUE(flags);
    ASSERT_TRUE(flags->mEnableAtStartup);
    ASSERT_TRUE(flags->mPersistent);

    EXPECT_TRUE(observer->wait(mDefaultTimeout));
}

TEST_F(FUSEMountTests, remove_fails_when_mount_enabled)
{
    MountInfo mount;

    mount.mHandle = ClientW()->handle("/x/s");
    mount.name("s");
    mount.mPath = MountPathW();

    auto observer = ClientW()->mountEventObserver();

    observer->expect({
        mount.name(),
        MOUNT_SUCCESS,
        MOUNT_ADDED
    });

    ASSERT_EQ(ClientW()->addMount(mount), MOUNT_SUCCESS);

    observer->expect({
        mount.name(),
        MOUNT_SUCCESS,
        MOUNT_ENABLED
    });

    ASSERT_EQ(ClientW()->enableMount(mount.name(), false), MOUNT_SUCCESS);

    ASSERT_TRUE(waitFor([&]() {
        std::error_code error;
        return fs::exists(SentinelPathW(), error);
    }, mDefaultTimeout));

    observer->expect({
        mount.name(),
        MOUNT_BUSY,
        MOUNT_REMOVED
    });

    ASSERT_EQ(ClientW()->removeMount(mount.name()), MOUNT_BUSY);

    ASSERT_TRUE(observer->wait(mDefaultTimeout));

    ASSERT_EQ(ClientW()->mounts(true), MountInfoVector(1, mount));
}

TEST_F(FUSEMountTests, remove_succeeds)
{
    MountInfo mount;

    mount.mHandle = ClientW()->handle("/x/s");
    mount.name("s");
    mount.mPath = MountPathW();

    auto observer = ClientW()->mountEventObserver();

    observer->expect({
        mount.name(),
        MOUNT_SUCCESS,
        MOUNT_ADDED
    });

    ASSERT_EQ(ClientW()->addMount(mount), MOUNT_SUCCESS);
    ASSERT_FALSE(ClientW()->mounts(false).empty());

    observer->expect({
        mount.name(),
        MOUNT_SUCCESS,
        MOUNT_REMOVED
    });

    ASSERT_EQ(ClientW()->removeMount(mount.name()), MOUNT_SUCCESS);

    ASSERT_TRUE(observer->wait(mDefaultTimeout));

    ASSERT_TRUE(ClientW()->mounts(false).empty());
}

TEST_F(FUSEMountTests, remove_succeeds_when_mount_unknown)
{
    auto observer = ClientW()->mountEventObserver();

    observer->expect({
        std::string(),
        MOUNT_SUCCESS,
        MOUNT_REMOVED
    });

    ASSERT_EQ(ClientW()->removeMount(std::string()), MOUNT_SUCCESS);

    ASSERT_TRUE(observer->wait(mDefaultTimeout));
}

TEST_F(FUSEMountTests, temporary_disable_is_not_remembered)
{
    MountInfo mount;

    mount.mHandle = ClientW()->handle("/x/s");
    mount.mFlags.mEnableAtStartup = true;
    mount.name("s");
    mount.mFlags.mPersistent = true;
    mount.mPath = MountPathW();

    auto observer = ClientW()->mountEventObserver();

    observer->expect({
        mount.name(),
        MOUNT_SUCCESS,
        MOUNT_ADDED
    });

    ASSERT_EQ(ClientW()->addMount(mount), MOUNT_SUCCESS);

    observer->expect({
        mount.name(),
        MOUNT_SUCCESS,
        MOUNT_DISABLED,
    });

    ASSERT_EQ(ClientW()->disableMount(mount.name(), false), MOUNT_SUCCESS);

    EXPECT_TRUE(observer->wait(mDefaultTimeout));

    auto flags = ClientW()->mountFlags(mount.name());

    ASSERT_TRUE(flags);
    ASSERT_EQ(mount.mFlags, *flags);
}

TEST_F(FUSEMountTests, temporary_enable_is_not_remembered)
{
    MountInfo mount;

    mount.mHandle = ClientW()->handle("/x/s");
    mount.name("s");
    mount.mPath = MountPathW();

    auto observer = ClientW()->mountEventObserver();

    observer->expect({
        mount.name(),
        MOUNT_SUCCESS,
        MOUNT_ADDED
    });

    ASSERT_EQ(ClientW()->addMount(mount), MOUNT_SUCCESS);

    observer->expect({
        mount.name(),
        MOUNT_SUCCESS,
        MOUNT_ENABLED,
    });

    ASSERT_EQ(ClientW()->enableMount(mount.name(), false), MOUNT_SUCCESS);

    EXPECT_TRUE(observer->wait(mDefaultTimeout));

    auto flags = ClientW()->mountFlags(mount.name());

    ASSERT_TRUE(flags);
    ASSERT_EQ(mount.mFlags, *flags);
}

TEST_F(FUSEMountTests, transient_mounts_are_transient)
{
    auto client = CreateClient("transient_" + randomName());
    ASSERT_TRUE(client);

    ASSERT_EQ(client->login(1), API_OK);

    MountInfo mount;

    mount.mHandle = client->handle("/x/s");
    mount.name("s");
    mount.mPath = client->storagePath() / "s";

    UNIX_ONLY(ASSERT_TRUE(fs::create_directories(Path(mount.mPath))));

    auto observer = client->mountEventObserver();

    observer->expect({
        mount.name(),
        MOUNT_SUCCESS,
        MOUNT_ADDED
    });

    ASSERT_EQ(client->addMount(mount), MOUNT_SUCCESS);

    ASSERT_TRUE(observer->wait(mDefaultTimeout));

    auto sessionToken = client->sessionToken();
    ASSERT_FALSE(sessionToken.empty());

    ASSERT_EQ(client->logout(true), API_OK);
    ASSERT_EQ(client->login(sessionToken), API_OK);

    ASSERT_FALSE(client->mountInfo(mount.name()));
}

} // testing
} // fuse
} // mega

