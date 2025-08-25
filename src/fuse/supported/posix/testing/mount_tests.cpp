#include <mega/common/error_or.h>
#include <mega/common/testing/directory.h>
#include <mega/fuse/common/mount_event.h>
#include <mega/fuse/common/mount_event_type.h>
#include <mega/fuse/common/mount_info.h>
#include <mega/fuse/common/mount_result.h>
#include <mega/fuse/common/testing/client.h>
#include <mega/fuse/common/testing/mount_event_observer.h>
#include <mega/fuse/common/testing/mount_tests.h>

namespace mega
{
namespace fuse
{
namespace testing
{

using namespace common;
using namespace common::testing;

TEST_F(FUSEMountTests, add_fails_when_target_is_unknown)
{
    auto handle = ClientW()->handle("/x/s");
    ASSERT_EQ(handle.errorOr(API_OK), API_OK);

    MountInfo info;

    info.mHandle = *handle;
    info.name("s");
    info.mPath = Path(MountPathW().path() / "bogus");

    auto observer = ClientW()->mountEventObserver();

    observer->expect({
        info.name(),
        MOUNT_LOCAL_UNKNOWN,
        MOUNT_ADDED
    });

    ASSERT_EQ(ClientW()->addMount(info), MOUNT_LOCAL_UNKNOWN);

    ASSERT_TRUE(observer->wait(mDefaultTimeout));

    ASSERT_TRUE(ClientW()->mounts(false).empty());
}

TEST_F(FUSEMountTests, add_fails_when_target_is_unspecified)
{
    auto handle = ClientW()->handle("/x/s");
    ASSERT_EQ(handle.errorOr(API_OK), API_OK);

    MountInfo info;

    info.mHandle = *handle;
    info.name("s");
    info.mPath = NormalizedPath();

    auto observer = ClientW()->mountEventObserver();

    observer->expect({
        info.name(),
        MOUNT_LOCAL_UNKNOWN,
        MOUNT_ADDED
    });

    ASSERT_EQ(ClientW()->addMount(info), MOUNT_LOCAL_UNKNOWN);

    ASSERT_TRUE(observer->wait(mDefaultTimeout));

    ASSERT_TRUE(ClientW()->mounts(false).empty());
}

TEST_F(FUSEMountTests, enable_fails_when_target_is_unknown)
{
    auto handle = ClientW()->handle("/x/s");
    ASSERT_EQ(handle.errorOr(API_OK), API_OK);

    MountInfo mount;

    auto observer = ClientW()->mountEventObserver();

    {
        Directory sd0("sd0", mScratchPath);

        mount.name("s");
        mount.mHandle = *handle;
        mount.mPath = sd0.path();

        observer->expect({
            mount.name(),
            MOUNT_SUCCESS,
            MOUNT_ADDED
        });

        ASSERT_EQ(ClientW()->addMount(mount), MOUNT_SUCCESS);
    }

    observer->expect({
        mount.name(),
        MOUNT_LOCAL_UNKNOWN,
        MOUNT_ENABLED
    });

    ASSERT_EQ(ClientW()->enableMount(mount.name(), false),
              MOUNT_LOCAL_UNKNOWN);

    ASSERT_TRUE(ClientW()->mounts(true).empty());

    ASSERT_TRUE(observer->wait(mDefaultTimeout));

    ASSERT_FALSE(fs::exists(SentinelPathW()));
}

} // testing
} // fuse
} // mega

