#include <mega/fuse/common/client.h>
#include <mega/fuse/common/error_or.h>
#include <mega/fuse/common/inode_info.h>
#include <mega/fuse/common/mount_event_type.h>
#include <mega/fuse/common/mount_event.h>
#include <mega/fuse/common/mount_info.h>
#include <mega/fuse/common/mount_result.h>
#include <mega/fuse/common/normalized_path.h>
#include <mega/fuse/common/service.h>
#include <mega/fuse/common/task_queue.h>
#include <mega/fuse/platform/service_context.h>

namespace mega
{
namespace fuse
{
namespace platform
{

ServiceContext::ServiceContext(const ServiceFlags&, Service& service)
  : fuse::ServiceContext(service)
{
}

ServiceContext::~ServiceContext()
{
}

MountResult ServiceContext::add(const MountInfo&)
{
    return MOUNT_UNSUPPORTED;
}

bool ServiceContext::cached(NormalizedPath) const
{
    return false;
}

void ServiceContext::current()
{
}

ErrorOr<InodeInfo> ServiceContext::describe(const NormalizedPath&) const
{
    return API_ENOENT;
}

void ServiceContext::disable(MountDisabledCallback callback,
                             const LocalPath& path,
                             bool remember)
{
    callback(MOUNT_UNKNOWN);

    MountEvent event;

    event.mPath = path;
    event.mResult = MOUNT_UNKNOWN;
    event.mType = MOUNT_DISABLED;

    client().emitEvent(event);
}

MountResult ServiceContext::discard(bool)
{
    return MOUNT_UNSUPPORTED;
}

MountResult ServiceContext::downgrade(const LocalPath&, std::size_t)
{
    return MOUNT_UNSUPPORTED;
}

MountResult ServiceContext::enable(const LocalPath&, bool)
{
    return MOUNT_UNKNOWN;
}

bool ServiceContext::enabled(const LocalPath&) const
{
    return false;
}

Task ServiceContext::execute(std::function<void(const Task&)> function)
{
    Task task(std::move(function));

    task.cancel();

    return task;
}

MountResult ServiceContext::flags(const LocalPath&, const MountFlags&)
{
    return MOUNT_UNKNOWN;
}

MountFlagsPtr ServiceContext::flags(const LocalPath&) const
{
    return nullptr;
}

MountInfoPtr ServiceContext::get(const LocalPath&) const
{
    return nullptr;
}

MountInfoVector ServiceContext::get(bool) const
{
    return MountInfoVector();
}

NormalizedPathVector ServiceContext::paths(const std::string&) const
{
    return NormalizedPathVector();
}

MountResult ServiceContext::remove(const LocalPath&)
{
    return MOUNT_UNKNOWN;
}

bool ServiceContext::syncable(const NormalizedPath&) const
{
    return true;
}

void ServiceContext::updated(NodeEventQueue&)
{
}

MountResult ServiceContext::upgrade(const LocalPath&, std::size_t)
{
    return MOUNT_UNSUPPORTED;
}

} // platform
} // fuse
} // mega

