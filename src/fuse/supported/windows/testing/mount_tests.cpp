#include <mega/common/error_or.h>
#include <mega/fuse/common/mount_event.h>
#include <mega/fuse/common/mount_event_type.h>
#include <mega/fuse/common/mount_info.h>
#include <mega/fuse/common/mount_result.h>
#include <mega/fuse/common/testing/client.h>
#include <mega/fuse/common/testing/directory.h>
#include <mega/fuse/common/testing/mount_event_observer.h>
#include <mega/fuse/common/testing/mount_tests.h>
#include <mega/fuse/platform/constants.h>

namespace mega
{
namespace fuse
{
namespace testing
{

using platform::MaxMountNameLength;

TEST_F(FUSEMountTests, add_fails_when_name_contains_illegal_characters)
{
    auto handle = ClientW()->handle("/x/s");
    ASSERT_EQ(handle.errorOr(API_OK), API_OK);

    MountInfo info;

    info.mHandle = *handle;
    info.name("s|a");

    auto observer = ClientW()->mountEventObserver();

    observer->expect({info.name(), MOUNT_NAME_INVALID_CHAR, MOUNT_ADDED});

    ASSERT_EQ(ClientW()->addMount(info), MOUNT_NAME_INVALID_CHAR);

    ASSERT_TRUE(observer->wait(mDefaultTimeout));

    ASSERT_TRUE(ClientW()->mounts(false).empty());
}

TEST_F(FUSEMountTests, add_fails_when_name_is_too_long)
{
    auto handle = ClientW()->handle("/x/s");
    ASSERT_EQ(handle.errorOr(API_OK), API_OK);

    MountInfo info;

    info.mHandle = *handle;
    info.mFlags.mName = std::string(MaxMountNameLength + 1, 'a');

    auto observer = ClientW()->mountEventObserver();

    observer->expect({info.name(), MOUNT_NAME_TOO_LONG, MOUNT_ADDED});

    ASSERT_EQ(ClientW()->addMount(info), MOUNT_NAME_TOO_LONG);
    ASSERT_TRUE(observer->wait(mDefaultTimeout));
    ASSERT_TRUE(ClientW()->mounts(false).empty());
}

TEST_F(FUSEMountTests, add_succeeds_when_target_is_unspecified)
{
    auto handle = ClientW()->handle("/x/s");
    ASSERT_EQ(handle.errorOr(API_OK), API_OK);

    MountInfo info;

    info.mHandle = *handle;
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

TEST_F(FUSEMountTests, enable_succeeds_with_long_name)
{
    auto handle = ClientW()->handle("/x/s");
    ASSERT_EQ(handle.errorOr(API_OK), API_OK);

    MountInfo info;

    info.mHandle = *handle;
    info.mFlags.mName = std::string(MaxMountNameLength, 'a');

    auto observer = ClientW()->mountEventObserver();

    observer->expect({info.name(), MOUNT_SUCCESS, MOUNT_ADDED});
    ASSERT_EQ(ClientW()->addMount(info), MOUNT_SUCCESS);

    observer->expect({info.name(), MOUNT_SUCCESS, MOUNT_ENABLED});
    ASSERT_EQ(ClientW()->enableMount(info.name(), false), MOUNT_SUCCESS);
}

TEST_F(FUSEMountTests, enable_succeeds_when_target_is_empty)
{
    auto handle = ClientW()->handle("/x/s");
    ASSERT_EQ(handle.errorOr(API_OK), API_OK);

    MountInfo info;

    info.mHandle = *handle;
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

    ASSERT_FALSE(path.empty());
}

} // testing
} // fuse
} // mega

