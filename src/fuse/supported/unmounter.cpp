#include <mega/common/task_executor.h>
#include <mega/fuse/common/client.h>
#include <mega/fuse/common/logging.h>
#include <mega/fuse/common/mount_event.h>
#include <mega/fuse/common/mount_event_type.h>
#include <mega/fuse/common/mount_result.h>
#include <mega/fuse/platform/mount.h>
#include <mega/fuse/platform/service_context.h>
#include <mega/fuse/platform/unmounter.h>
#include <mega/fuse/platform/utility.h>

namespace mega
{
namespace fuse
{
namespace platform
{

using namespace common;

void Unmounter::emitEvent(MountDisabledCallback callback,
                          const std::string& name,
                          MountResult result)
{

    // Convenience.
    auto& client = mContext.client();

    // Emit event.
    if (result != MOUNT_SUCCESS)
    {
        MountEvent event;

        event.mName = name;
        event.mResult = result;
        event.mType = MOUNT_DISABLED;

        fuse::emitEvent(client, event);
    }

    // Forward result to user callback.
    auto wrapper = [result](MountDisabledCallback& callback, const Task&) {
        callback(result);
    }; // wrapper

    // Queue callback for execution.
    client.execute(
      std::bind(std::move(wrapper),
                std::move(callback),
                std::placeholders::_1));
}

void Unmounter::unmount(MountDisabledCallback callback,
                        MountWeakPtr mount,
                        const std::string& name,
                        const LocalPath& path)
{
    FUSEDebugF("Attempting to unmount mount: %s", name.c_str());

    auto mount_ = mount.lock();

    if (!mount_)
    {
        FUSEDebugF("Mount no longer exists: %s", name.c_str());

        return emitEvent(std::move(callback), name, MOUNT_UNKNOWN);
    }

    auto result = this->unmount(*mount_, path.toPath(false), false);

    if (result != MOUNT_SUCCESS)
        return emitEvent(std::move(callback), name, result);

    auto disabled = mount_->disabled();

    mount_.reset();

    disabled.get();

    FUSEDebugF("Mount %s has been unmounted", name.c_str());

    emitEvent(std::move(callback), name, MOUNT_SUCCESS);
}

Unmounter::Unmounter(platform::ServiceContext& context)
  : mActivities()
  , mContext(context)
{
    FUSEDebug1("Unmounter constructed");
}

Unmounter::~Unmounter()
{
    FUSEDebug1("Unmounter destroyed");
}

void Unmounter::unmount(MountDisabledCallback callback, MountPtr mount)
{
    auto wrapper = [this](Activity&,
                          MountDisabledCallback& callback,
                          MountWeakPtr mount,
                          const std::string& name,
                          const LocalPath& path,
                          const Task& task)
    {
        // Don't bother unmounting as we're being torn down.
        if (task.cancelled())
            return emitEvent(std::move(callback),
                             name,
                             MOUNT_ABORTED);

        // Try and unmount the specified mount.
        unmount(std::move(callback),
                std::move(mount),
                name,
                path);
    }; // wrapper

    auto name = mount->name();

    FUSEDebugF("Queuing unmount of mount %s", name.c_str());

    // Queue unmount for execution.
    mContext.mExecutor.execute(
      std::bind(std::move(wrapper),
                mActivities.begin(),
                std::move(callback),
                MountWeakPtr(mount),
                std::move(name),
                mount->path(),
                std::placeholders::_1),
      true);
}

} // platform
} // fuse
} // mega

