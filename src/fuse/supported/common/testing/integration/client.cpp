#include <mega/common/error_or.h>
#include <mega/common/normalized_path.h>
#include <mega/common/testing/path.h>
#include <mega/fuse/common/inode_info.h>
#include <mega/fuse/common/logging.h>
#include <mega/fuse/common/mount_event.h>
#include <mega/fuse/common/mount_flags.h>
#include <mega/fuse/common/mount_info.h>
#include <mega/fuse/common/mount_result.h>
#include <mega/fuse/common/service.h>
#include <mega/fuse/common/testing/client.h>
#include <mega/fuse/common/testing/mount_event_observer.h>

#include <future>

namespace mega
{
namespace fuse
{
namespace testing
{

using common::ErrorOr;
using common::NormalizedPath;
using common::testing::Path;

Client::Client(const std::string& clientName, const Path& databasePath, const Path& storagePath):
    common::testing::Client(clientName, databasePath, storagePath),
    mMountEventObservers(),
    mMountEventObserversLock()
{}

Client::~Client() {}

void Client::mountEvent(const MountEvent& event)
{
    std::lock_guard<std::mutex> guard(mMountEventObserversLock);

    // Inform observers that an event has been emitted.
    auto i = mMountEventObservers.begin();

    while (i != mMountEventObservers.end())
    {
        // Check if the observer's still alive.
        auto observer = i->lock();

        // Observer's dead.
        if (!observer)
        {
            i = mMountEventObservers.erase(i);
            continue;
        }

        // Let the observer know an event has been emitted.
        observer->emitted(event);

        // Move to the next observer.
        ++i;
    }
}

MountResult Client::addMount(const MountInfo& info)
{
    return fuseService().add(info);
}

ErrorOr<InodeInfo> Client::describe(const Path& path) const
{
    return fuseService().describe(path.localPath());
}

MountResult Client::disableMount(const std::string& name, bool remember)
{
    // So we can wait for the mount to be disabled.
    std::promise<MountResult> notifier;

    // Called when the mount has been disabled.
    auto disabled = [&notifier](MountResult result)
    {
        notifier.set_value(result);
    }; // disabled

    // Try and disable the mount.
    fuseService().disable(std::move(disabled), name, remember);

    // Wait for the mount to be disabled.
    auto result = notifier.get_future().get();

    // Couldn't disable the mount.
    if (result != MOUNT_SUCCESS)
        FUSEErrorF("Couldn't disable mount: %s: %s", name.c_str(), toString(result));

    // Return the result to the caller.
    return result;
}

MountResult Client::disableMounts(bool remember)
{
    // What mounts are currently enabled?
    auto mounts = this->mounts(true);

    // No mounts are enabled.
    if (mounts.empty())
        return MOUNT_SUCCESS;

    // How long should we wait for a mount to become idle?
    constexpr auto idleTime = std::chrono::seconds(4);

    // How many times should we try and disable a mount?
    constexpr auto numAttempts = 4;

    // Try and disable each mount.
    while (!mounts.empty())
    {
        // Pick a mount to disable.
        auto& mount = mounts.back();

        // Keep trying to disable the mount if necessary.
        auto disabled = [](MountResult result)
        {
            return result == MOUNT_UNKNOWN || result == MOUNT_SUCCESS;
        }; // disabled

        // Try and disable the mount.
        auto result = disableMount(mount.name(), remember);

        // Keep trying to disable the mount if necessary.
        for (auto attempts = 0; !disabled(result) && attempts < numAttempts; ++attempts)
        {
            // Give the mount a little time to become idle.
            std::this_thread::sleep_for(idleTime);

            // Try and disable the mount again.
            result = disableMount(mount.name(), remember);
        }

        // Try and disable the next mount.
        mounts.pop_back();
    }

    // We weren't able to disable all the mounts.
    if (!mounts.empty())
        return MOUNT_BUSY;

    // All mounts have been disabled.
    return MOUNT_SUCCESS;
}

MountResult Client::discard(bool discard)
{
    return fuseService().discard(discard);
}

MountResult Client::enableMount(const std::string& name, bool remember)
{
    const auto ret = fuseService().enable(name, remember);
    // Tell FUSE mount that we need to access mount
    if (const auto flags = fuseService().flags(name); flags)
    {
        flags->mAllowSelfAccess = true;
        fuseService().flags(name, *flags);
    }
    return ret;
}

bool Client::isCached(const Path& path) const
{
    return fuseService().cached(path.localPath());
}

MountEventObserverPtr Client::mountEventObserver()
{
    auto observer = MountEventObserver::create();

    std::lock_guard<std::mutex> guard(mMountEventObserversLock);

    mMountEventObservers.emplace(observer);

    return observer;
}

bool Client::mountEnabled(const std::string& name) const
{
    return fuseService().enabled(name);
}

MountResult Client::mountFlags(const std::string& name, const MountFlags& flags)
{
    return fuseService().flags(name, flags);
}

MountFlagsPtr Client::mountFlags(const std::string& name) const
{
    return fuseService().flags(name);
}

MountInfoPtr Client::mountInfo(const std::string& name) const
{
    return fuseService().get(name);
}

NormalizedPath Client::mountPath(const std::string& name) const
{
    return fuseService().path(name);
}

MountInfoVector Client::mounts(bool onlyEnabled) const
{
    return fuseService().get(onlyEnabled);
}

MountResult Client::removeMount(const std::string& name)
{
    // Try and remove the mount.
    auto result = fuseService().remove(name);

    // Couldn't remove the mount.
    if (result != MOUNT_SUCCESS)
        FUSEErrorF("Unable to remove mount: %s: %s", name.c_str(), toString(result));

    // Return result to caller.
    return result;
}

MountResult Client::removeMounts(bool disable)
{
    auto result = MOUNT_SUCCESS;

    // Disable enabled mounts if requested.
    if (disable)
        result = disableMounts(true);

    // Mounts couldn't be disabled.
    if (result != MOUNT_SUCCESS)
        return result;

    // What mounts are known to us?
    auto mounts = this->mounts(false);

    // Try and remove each mount.
    while (!mounts.empty())
    {
        // Select a mount to remove.
        auto& mount = mounts.back();

        // Try and remove the mount.
        auto result = removeMount(mount.name());

        // Couldn't remove the mount.
        if (result != MOUNT_SUCCESS)
            return result;

        // Mount's been removed.
        mounts.pop_back();
    }

    // All mounts have been removed.
    return result;
}

} // testing
} // fuse
} // mega
