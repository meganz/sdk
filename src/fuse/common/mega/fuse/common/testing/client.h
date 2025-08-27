#pragma once

#include <mega/common/normalized_path_forward.h>
#include <mega/common/testing/client.h>
#include <mega/common/testing/path_forward.h>
#include <mega/fuse/common/inode_info_forward.h>
#include <mega/fuse/common/mount_event_forward.h>
#include <mega/fuse/common/mount_flags_forward.h>
#include <mega/fuse/common/mount_info_forward.h>
#include <mega/fuse/common/mount_result_forward.h>
#include <mega/fuse/common/service_forward.h>
#include <mega/fuse/common/testing/client_forward.h>
#include <mega/fuse/common/testing/mount_event_observer_forward.h>

#include <mutex>
#include <string>

namespace mega
{
namespace fuse
{
namespace testing
{

class Client: public virtual common::testing::Client
{
    // What observers are monitoring mount events?
    MountEventObserverWeakPtrSet mMountEventObservers;

    // Serializes access to mMountEventObservers.
    std::mutex mMountEventObserversLock;

protected:
    Client(const std::string& clientName,
           const common::testing::Path& databasePath,
           const common::testing::Path& storagePath);

    // Called when a mount event has been emitted.
    void mountEvent(const MountEvent& event);

public:
    virtual ~Client();

    // Add a new mount to the database.
    MountResult addMount(const MountInfo& info);

    // Describe the inode associated with the specified path.
    common::ErrorOr<InodeInfo> describe(const common::testing::Path& path) const;

    // Disable a previously enabled mount.
    MountResult disableMount(const std::string& name, bool remember);

    // Disable all enabled mounts.
    MountResult disableMounts(bool remember);

    // Discard node events.
    MountResult discard(bool discard);

    // Enable a previously added mount.
    MountResult enableMount(const std::string& name, bool remember);

    // Get our hands on the client's FUSE interface.
    virtual Service& fuseService() const = 0;

    // Check if a file is cached.
    bool isCached(const common::testing::Path& path) const;

    // Return a reference to a new mount event observer.
    MountEventObserverPtr mountEventObserver();

    // Query whether a mount is enabled.
    bool mountEnabled(const std::string& name) const;

    // Update a mount's flags.
    MountResult mountFlags(const std::string& name, const MountFlags& flags);

    // Retrieve a mount's flags.
    MountFlagsPtr mountFlags(const std::string& name) const;

    // Retrieve a mount's description.
    MountInfoPtr mountInfo(const std::string& name) const;

    // Retrieve the path associated with the specified name.
    common::NormalizedPath mountPath(const std::string& name) const;

    // Retrieve a description of each (enabled) mount.
    MountInfoVector mounts(bool onlyEnabled) const;

    // Remove a mount from the database.
    MountResult removeMount(const std::string& name);

    // Remove all mounts from the database.
    MountResult removeMounts(bool disable);

}; // Client

} // testing
} // fuse
} // mega

