#pragma once

#include <mega/fuse/common/service_context.h>
#include <mega/fuse/common/service_flags_forward.h>

namespace mega
{
namespace fuse
{
namespace platform
{

class ServiceContext
  : public fuse::ServiceContext
{
public:
    ServiceContext(const ServiceFlags&, Service& service);

    ~ServiceContext();

    // Add a mount to the database.
    MountResult add(const MountInfo& info) override;

    // Check if a file exists in the cache.
    bool cached(common::NormalizedPath path) const override;

    // Called by the client when its view of the cloud is current.
    void current() override;

    // Describe the inode representing the file at the specified path.
    common::ErrorOr<InodeInfo> describe(const common::NormalizedPath& path) const override;

    // Disable an enabled mount.
    void disable(MountDisabledCallback callback,
                 const std::string& name,
                 bool remember) override;

    // Discard node events.
    MountResult discard(bool discard) override;

    // Downgrade the FUSE database to the specified version.
    MountResult downgrade(const LocalPath& path,
                          std::size_t target) override;

    // Enable a disabled mount.
    MountResult enable(const std::string& name,
                       bool remember) override;

    // Query whether a specified mount is enabled.
    bool enabled(const std::string& name) const override;

    // Execute a function on some task.
    common::Task execute(std::function<void(const common::Task&)> function) override;

    // Update a mount's flags.
    MountResult flags(const std::string& name,
                      const MountFlags& flags) override;

    // Query a mount's flags.
    MountFlagsPtr flags(const std::string& name) const override;

    // Describe the mount associated with name.
    MountInfoPtr get(const std::string& name) const override;

    // Describe all (enabled) mounts.
    MountInfoVector get(bool onlyEnabled) const override;

    // Retrieve the path of the mount associated with name.
    common::NormalizedPath path(const std::string& name) const override;

    // Remove a disabled mount from the database.
    MountResult remove(const std::string& path) override;

    // Check whether the specified path is "syncable."
    bool syncable(const common::NormalizedPath& path) const override;

    // Called by the client when nodes have been changed in the cloud.
    void updated(common::NodeEventQueue& events) override;

    // Update the FUSE database to the specified version.
    MountResult upgrade(const LocalPath& path,
                        std::size_t target) override;
}; // ServiceContext

} // platform
} // fuse
} // mega

