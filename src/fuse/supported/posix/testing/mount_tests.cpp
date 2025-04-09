#include <mega/fuse/common/mount_event_type.h>
#include <mega/fuse/common/mount_event.h>
#include <mega/fuse/common/mount_info.h>
#include <mega/fuse/common/mount_result.h>
#include <mega/fuse/common/testing/client.h>
#include <mega/fuse/common/testing/directory.h>
#include <mega/fuse/common/testing/mount_event_observer.h>
#include <mega/fuse/common/testing/mount_tests.h>

namespace mega
{
namespace fuse
{
namespace testing
{

using namespace common;

TEST_F(FUSEMountTests, add_fails_when_target_is_unknown)
{
    MountInfo info;

    info.mHandle = ClientW()->handle("/x/s");
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
    MountInfo info;

    info.mHandle = ClientW()->handle("/x/s");
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
    MountInfo mount;

    auto observer = ClientW()->mountEventObserver();

    {
        Directory sd0("sd0", mScratchPath);

        mount.name("s");
        mount.mHandle = ClientW()->handle("/x/s");
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

