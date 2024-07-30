#pragma once

#include <mega/fuse/common/activity_monitor.h>
#include <mega/fuse/common/mount_result_forward.h>
#include <mega/fuse/common/service_callbacks.h>
#include <mega/fuse/platform/mount_forward.h>
#include <mega/fuse/platform/service_context_forward.h>

#include <mega/types.h>

namespace mega
{
namespace fuse
{
namespace platform
{

class Unmounter final
{
    // Report the result of an unmount operation.
    void emitEvent(MountDisabledCallback callback,
                   LocalPath path,
                   MountResult result);

    // Try and unmount the specified mount.
    void unmount(MountDisabledCallback callback,
                 MountWeakPtr mount,
                 LocalPath path);

    // Unmount the specified mount.
    MountResult unmount(Mount& mount,
                        const std::string& path,
                        bool abort);

    // Tracks whether we have any unmounts in progress.
    ActivityMonitor mActivities;

    // Which context contains our mounts?
    platform::ServiceContext& mContext;

public:
    explicit Unmounter(platform::ServiceContext& context);

    ~Unmounter();

    // Unmount the specified mount.
    void unmount(MountDisabledCallback callback,
                 MountPtr mount);
}; // Unmounter

} // platform
} // fuse
} // mega

