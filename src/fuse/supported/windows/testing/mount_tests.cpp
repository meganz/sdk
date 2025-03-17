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

TEST_F(FUSEMountTests, add_succeeds_when_target_is_unspecified)
{
    MountInfo info;

    info.mHandle = ClientW()->handle("/x/s");
    info.name("s");

    auto observer = ClientW()->mountEventObserver();

    observer->expect({
        info.name(),
        MOUNT_SUCCESS,
        MOUNT_ADDED
    });

    ASSERT_EQ(ClientW()->addMount(info), MOUNT_SUCCESS);

    ASSERT_TRUE(observer->wait(mDefaultTimeout));

    ASSERT_FALSE(ClientW()->mounts(false).empty());
}

TEST_F(FUSEMountTests, enable_succeeds_when_target_is_empty)
{
    MountInfo info;

    info.mHandle = ClientW()->handle("/x/s");
    info.name("s");

    auto observer = ClientW()->mountEventObserver();

    observer->expect({
        info.name(),
        MOUNT_SUCCESS,
        MOUNT_ADDED
    });

    ASSERT_EQ(ClientW()->addMount(info), MOUNT_SUCCESS);

    ASSERT_TRUE(observer->wait(mDefaultTimeout));

    observer->expect({
        info.name(),
        MOUNT_SUCCESS,
        MOUNT_ENABLED
    });

    ASSERT_EQ(ClientW()->enableMount(info.name(), false), MOUNT_SUCCESS);

    auto path = ClientW()->mountPath(info.name());

    ASSERT_TRUE(path);
    ASSERT_FALSE(path->empty());
}

} // testing
} // fuse
} // mega

