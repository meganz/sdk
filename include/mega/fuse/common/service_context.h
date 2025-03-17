#pragma once

#include <cstddef>

#include <mega/fuse/common/client_forward.h>
#include <mega/fuse/common/error_or_forward.h>
#include <mega/fuse/common/inode_info_forward.h>
#include <mega/fuse/common/mount_flags_forward.h>
#include <mega/fuse/common/mount_info_forward.h>
#include <mega/fuse/common/mount_result_forward.h>
#include <mega/fuse/common/node_event_queue_forward.h>
#include <mega/fuse/common/normalized_path_forward.h>
#include <mega/fuse/common/service_callbacks.h>
#include <mega/fuse/common/service_context_forward.h>
#include <mega/fuse/common/service_flags.h>
#include <mega/fuse/common/service_forward.h>
#include <mega/fuse/common/task_queue_forward.h>

#include <mega/types.h>

namespace mega
{
namespace fuse
{

class ServiceContext
{
protected:
    explicit ServiceContext(Service& service);

public:
    virtual ~ServiceContext();

    // Add a mount to the database.
    virtual MountResult add(const MountInfo& info) = 0;

    // Check if a file exists in the cache.
    virtual bool cached(NormalizedPath path) const = 0;

    // Retrieve the client that owns this context.
    Client& client() const;

    // Called by the client when its view of the cloud is current.
    virtual void current() = 0;

    // Describe the inode representing the file at the specified path.
    virtual ErrorOr<InodeInfo> describe(const NormalizedPath& path) const = 0;

    // Disable an enabled mount.
    virtual void disable(MountDisabledCallback callback,
                         const std::string& name,
                         bool remember) = 0;

    // Discard node events.
    virtual MountResult discard(bool discard) = 0;

    // Downgrade the FUSE database to the specified version.
    virtual MountResult downgrade(const LocalPath& path,
                                  std::size_t target) = 0;

    // Enable a disabled mount.
    virtual MountResult enable(const std::string& name,
                               bool remember) = 0;

    // Query whether a specified mount is enabled.
    virtual bool enabled(const std::string& name) const = 0;

    // Execute a function on some thread.
    virtual Task execute(std::function<void(const Task&)> function) = 0;

    // Update a mount's flags.
    virtual MountResult flags(const std::string& name,
                              const MountFlags& flags) = 0;

    // Query a mount's flags.
    virtual MountFlagsPtr flags(const std::string& name) const = 0;

    // Describe the mount associated with name.
    virtual MountInfoPtr get(const std::string& name) const = 0;

    // Describe all (enabled) mounts.
    virtual MountInfoVector get(bool enabled) const = 0;

    // Retrieve the path the mount associated with this name.
    virtual std::optional<NormalizedPath> path(const std::string& name) const = 0;

    // Remove a disabled mount from the database.
    virtual MountResult remove(const std::string& name) = 0;

    // Update the service's flags.
    virtual void serviceFlags(const ServiceFlags& flags);

    // Query the service's flags.
    ServiceFlags serviceFlags() const;

    // Check whether the specified path is "syncable."
    virtual bool syncable(const NormalizedPath& path) const = 0;

    // Called by the client when nodes have been changed in the cloud.
    virtual void updated(NodeEventQueue& events) = 0;

    // Update the FUSE database to the specified version.
    virtual MountResult upgrade(const LocalPath& path,
                                std::size_t target) = 0;

    Service& mService;
}; // ServiceContext

} // fuse
} // mega

