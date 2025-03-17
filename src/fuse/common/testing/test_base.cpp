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

bool TestBase::DoSetUp(const Parameters& parameters)
{
    auto result = false;

    // Make sure the basic stuff is up and running.
    EXPECT_TRUE((result = Test::DoSetUp(parameters)));

    // Basic stuff isn't up and running.
    if (!result)
        return false;

    // Adds and enables a mount.
    auto mount = [&](Client& client, const MountInfo& mount) {
        // So we can check if events are emitted.
        auto observer = client.mountEventObserver();

        // Try and add the mount.
        observer->expect({
            mount.mFlags.mName,
            MOUNT_SUCCESS,
            MOUNT_ADDED
        });

        auto result = MOUNT_SUCCESS;
        
        EXPECT_EQ((result = client.addMount(mount)), MOUNT_SUCCESS);

        // Couldn't add the mount.
        if (result != MOUNT_SUCCESS)
            return false;

        // Try and enable the mount.
        observer->expect({
            mount.mFlags.mName,
            MOUNT_SUCCESS,
            MOUNT_ENABLED
        });

        EXPECT_EQ((result = client.enableMount(mount.mFlags.mName, false)),
                  MOUNT_SUCCESS);

        // Couldn't enable the mount.
        if (result != MOUNT_SUCCESS)
            return false;

        // Convenience.
        constexpr auto timeout = std::chrono::seconds(8);

        // Wait for events to be emitted.
        auto emitted = false;

        EXPECT_TRUE((emitted = observer->wait(timeout)));

        // Events were never emitted.
        if (!emitted)
            return false;

        // Wait for sentinel to be visible.
        auto sentinel = Path(mount.mPath).path() / "sentinel";
        auto visible = false;

        EXPECT_TRUE((visible = waitFor([&]() {
            std::error_code error;
            return fs::exists(sentinel, error);
        }, timeout)));

        // Sentinels were never visible.
        if (!visible)
            return false;

        // Mount should be up and running.
        return true;
    }; // mount

    // Direct mounts.
    {
        MountInfo m;

        m.mHandle = ClientW()->handle("/x/s");

        // Establish read-only mount.
        m.mFlags.mName = "sr";
        m.mFlags.mReadOnly = true;
        m.mPath = MountPathR();

        EXPECT_TRUE((result = mount(*ClientW(), m)));

        if (!result)
            return false;

        // Establish read-write observer mount.
        m.mFlags.mName = "so";
        m.mFlags.mReadOnly = false;
        m.mPath = MountPathO();

        EXPECT_TRUE((result = mount(*ClientW(), m)));

        if (!result)
            return false;

        // Establish read-write actor mount.
        m.mFlags.mName = "sw";
        m.mPath = MountPathW();

        EXPECT_TRUE((result = mount(*ClientW(), m)));

        if (!result)
            return false;
    }

    // Share mounts.
    {
        MountInfo m;

        // Read-only mount.
        m.mHandle = ClientR()->handle("/x/s");
        m.mFlags.mName = "Sr";
        m.mPath = MountPathRS();

        EXPECT_TRUE((result = mount(*ClientS(), m)));

        if (!result)
            return false;

        // Read-write observer mount.
        m.mHandle = ClientW()->handle("/x/s");
        m.mFlags.mName = "So";
        m.mPath = MountPathOS();

        EXPECT_TRUE((result = mount(*ClientS(), m)));

        if (!result)
            return false;

        // Read-write mount.
        m.mFlags.mName = "Sw";
        m.mPath = MountPathWS();

        EXPECT_TRUE((result = mount(*ClientS(), m)));
    }

    return result;
}

} // testing
} // fuse
} // mega

