#include <mega/common/error_or.h>
#include <mega/fuse/common/mount_event.h>
#include <mega/fuse/common/mount_event_type.h>
#include <mega/fuse/common/mount_info.h>
#include <mega/fuse/common/mount_result.h>
#include <mega/fuse/common/testing/client.h>
#include <mega/fuse/common/testing/mount_event_observer.h>
#include <mega/fuse/common/testing/parameters.h>
#include <mega/fuse/common/testing/test_base.h>
#include <mega/fuse/common/testing/utility.h>

namespace mega
{
namespace fuse
{
namespace testing
{

using namespace common::testing;

bool TestBase::DoSetUp(const Parameters& parameters)
{
    // Make sure the basic stuff is up and running.
    EXPECT_TRUE(Test::DoSetUp(parameters));

    // Basic stuff isn't up and running.
    if (HasFailure())
        return false;

    // Adds and enables a mount.
    auto setupMount = [&](Client& client, const MountInfo& mount)
    {
        // So we can check if events are emitted.
        auto observer = client.mountEventObserver();

        // Try and add the mount.
        observer->expect({mount.name(), MOUNT_SUCCESS, MOUNT_ADDED});

        EXPECT_EQ(client.addMount(mount), MOUNT_SUCCESS);

        // Couldn't add the mount.
        if (HasFailure())
            return false;

        // Try and enable the mount.
        observer->expect({mount.name(), MOUNT_SUCCESS, MOUNT_ENABLED});

        EXPECT_EQ(client.enableMount(mount.name(), false), MOUNT_SUCCESS);

        // Couldn't enable the mount.
        if (HasFailure())
            return false;

        // Convenience.
        constexpr auto timeout = std::chrono::seconds(8);

        // Wait for events to be emitted.
        EXPECT_TRUE(observer->wait(timeout));

        // Events were never emitted.
        if (HasFailure())
            return false;

        // Get the mount's path.
        auto path = client.mountPath(mount.name());

        // Sanity.
        EXPECT_FALSE(path.empty());

        if (HasFailure())
            return false;

        // Wait for sentinel to be visible.
        auto sentinel = Path(path).path() / "sentinel";

        EXPECT_TRUE(waitFor(
            [&]()
            {
                std::error_code error;
                return fs::exists(sentinel, error);
            },
            timeout));

        // Sentinels were never visible.
        if (HasFailure())
            return false;

        // Mount should be up and running.
        return true;
    }; // setupMount

    auto handle = ClientW()->handle("/x/s");
    EXPECT_EQ(handle.errorOr(API_OK), API_OK);

    if (HasFailure())
        return false;

    MountInfo mount;

    // Direct mounts.

    // Establish read-only mount.
    mount.name("sr");
    mount.mFlags.mReadOnly = true;
    mount.mHandle = *handle;
    mount.mPath = MountPathR();

    EXPECT_TRUE(setupMount(*ClientW(), mount));

    if (HasFailure())
        return false;

    // Establish read-write observer mount.
    mount.name("so");
    mount.mFlags.mReadOnly = false;
    mount.mPath = MountPathO();

    EXPECT_TRUE(setupMount(*ClientW(), mount));

    if (HasFailure())
        return false;

    // Establish read-write actor mount.
    mount.name("sw");
    mount.mPath = MountPathW();

    EXPECT_TRUE(setupMount(*ClientW(), mount));

    if (HasFailure())
        return false;

    // Share mounts.

    // Read-write observer mount.
    mount.mHandle = *handle;
    mount.name("So");
    mount.mPath = MountPathOS();

    EXPECT_TRUE(setupMount(*ClientS(), mount));

    if (HasFailure())
        return false;

    // Read-write mount.
    mount.name("Sw");
    mount.mPath = MountPathWS();

    EXPECT_TRUE(setupMount(*ClientS(), mount));

    if (HasFailure())
        return false;

    handle = ClientR()->handle("/x/s");
    EXPECT_EQ(handle.errorOr(API_OK), API_OK);

    if (HasFailure())
        return false;

    // Read-only mount.
    mount.mHandle = *handle;
    mount.name("Sr");
    mount.mPath = MountPathRS();

    EXPECT_TRUE(setupMount(*ClientS(), mount));

    return !HasFailure();
}

} // testing
} // fuse
} // mega
