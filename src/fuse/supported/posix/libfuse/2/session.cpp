#include <cassert>
#include <cstring>
#include <vector>

#include <mega/fuse/common/logging.h>
#include <mega/fuse/common/mount_inode_id.h>
#include <mega/fuse/common/task_executor.h>
#include <mega/fuse/common/utility.h>
#include <mega/fuse/platform/constants.h>
#include <mega/fuse/platform/mount.h>
#include <mega/fuse/platform/mount_db.h>
#include <mega/fuse/platform/platform.h>
#include <mega/fuse/platform/request.h>
#include <mega/fuse/platform/service_context.h>
#include <mega/fuse/platform/session.h>

namespace mega
{
namespace fuse
{
namespace platform
{

void Session::init(void*, fuse_conn_info* connection)
{
#define ENTRY(name) {#name, name}
    const std::map<std::string, unsigned int> capabilities = {
        ENTRY(FUSE_CAP_ASYNC_READ),
        ENTRY(FUSE_CAP_ATOMIC_O_TRUNC),
        ENTRY(FUSE_CAP_BIG_WRITES),
        ENTRY(FUSE_CAP_DONT_MASK),
        ENTRY(FUSE_CAP_EXPORT_SUPPORT),
        ENTRY(FUSE_CAP_FLOCK_LOCKS),
        ENTRY(FUSE_CAP_IOCTL_DIR),
        ENTRY(FUSE_CAP_POSIX_LOCKS),
        ENTRY(FUSE_CAP_SPLICE_MOVE),
        ENTRY(FUSE_CAP_SPLICE_READ),
        ENTRY(FUSE_CAP_SPLICE_WRITE)
    }; // capabilities
#undef ENTRY

    connection->want |= FUSE_CAP_ATOMIC_O_TRUNC;

    for (auto& entry : capabilities)
    {
        auto capable = (connection->capable & entry.second) > 0;
        auto wanted  = (connection->want & entry.second) > 0;

        FUSEDebugF("init: %u%u %s", capable, wanted, entry.first.c_str());
    }
}

std::string Session::nextRequest()
{
    assert(mChannel);
    assert(mSession);

    std::string buffer(fuse_chan_bufsize(mChannel), '\0');

    while (true)
    {
        auto result = fuse_chan_recv(&mChannel, &buffer[0], buffer.size());

        if (!result)
            return std::string();

        if (result > 0)
        {
            buffer.resize(static_cast<std::size_t>(result));

            return buffer;
        }

        if (result == -EINTR)
            continue;

        throw FUSEErrorF("Unable to read request from session: %d",
                         std::strerror(-result));
    }
}

void Session::populateOperations(fuse_lowlevel_ops& operations)
{
    SessionBase::populateOperations(operations);

    operations.init   = &Session::init;
    operations.rename = &Session::rename;
}

void Session::rename(fuse_req_t request,
                     fuse_ino_t parent,
                     const char* name,
                     fuse_ino_t newParent,
                     const char* newName)
{
    MountInodeID parent_(parent);
    MountInodeID newParent_(newParent);

    FUSEDebugF("rename: parent: %s, name: %s, newParent: %s, "
               "newName: %s, request: %p",
               toString(parent_).c_str(),
               name,
               toString(newParent_).c_str(),
               newName,
               request);

    mount(request).execute(&Mount::rename,
                           true,
                           Request(request),
                           parent_,
                           std::string(name),
                           newParent_,
                           std::string(newName));
}

Session::Session(Mount& mount)
  : SessionBase(mount)
  , mChannel(nullptr)
{
    auto arguments = Arguments(mount.name());
    auto path = mMount.path().toPath(false);

    mChannel = fuse_mount(path.c_str(), arguments.get());
    if (!mChannel)
        throw FUSEErrorF("Unable to construct channel: %s", path.c_str());

    mSession = fuse_lowlevel_new(arguments.get(),
                                 &operations(),
                                 sizeof(fuse_lowlevel_ops),
                                 &mMount);
    if (!mSession)
    {
        fuse_unmount(path.c_str(), mChannel);

        throw FUSEErrorF("Unable to construct session: %s", path.c_str());
    }

    fuse_session_add_chan(mSession, mChannel);

    FUSEDebugF("Session constructed: %s", path.c_str());
}

Session::~Session()
{
    assert(mChannel);
    assert(mSession);

    fuse_session_remove_chan(mChannel);
    fuse_session_destroy(mSession);

    auto path = mMount.path().toPath(false);

    fuse_unmount(path.c_str(), mChannel);

    FUSEDebugF("Session destroyed: %s", path.c_str());
}

int Session::descriptor() const
{
    assert(mChannel);

    return fuse_chan_fd(mChannel);
}

void Session::dispatch()
{
    // Sanity.
    assert(mChannel);
    assert(mSession);

    // Dispatch the request.
    if (auto request = nextRequest(); !request.empty())
        fuse_session_process(mSession,
                             request.data(),
                             request.size(),
                             mChannel);
}

void Session::invalidateData(MountInodeID id, off_t offset, off_t length)
{
    assert(mChannel);
    assert(mSession);

    while (!fuse_session_exited(mSession))
    {
        auto result = fuse_lowlevel_notify_inval_inode(mChannel,
                                                       id.get(),
                                                       offset,
                                                       length);

        if (!result || result == -ENOENT || result == -ENOTCONN)
            return;

        if (result == -EINTR)
            continue;

        throw FUSEErrorF("Unable to invalidate inode: %s: %s",
                         toString(id).c_str(),
                         std::strerror(-result));
    }
}

void Session::invalidateEntry(const std::string& name,
                              MountInodeID child,
                              MountInodeID parent)
{
    assert(!name.empty());
    assert(mChannel);
    assert(mSession);

    while (!fuse_session_exited(mSession))
    {
        auto result = fuse_lowlevel_notify_delete(mChannel,
                                                  parent.get(),
                                                  child.get(),
                                                  name.c_str(),
                                                  name.size());

        if (!result || result == -ENOENT || result == -ENOTCONN)
            return;

        if (result == -EINTR)
            continue;

        throw FUSEErrorF("Unable to invalidate entry: %s %s %s: %s",
                         toString(child).c_str(),
                         toString(parent).c_str(),
                         name.c_str(),
                         std::strerror(-result));
    }
}

void Session::invalidateEntry(const std::string& name, MountInodeID parent)
{
    assert(!name.empty());
    assert(mChannel);
    assert(mSession);

    while (!fuse_session_exited(mSession))
    {
        auto result = fuse_lowlevel_notify_inval_entry(mChannel,
                                                       parent.get(),
                                                       name.c_str(),
                                                       name.size());

        if (!result || result == -ENOENT || result == -ENOTCONN)
            return;

        if (result == -EINTR)
            continue;

        throw FUSEErrorF("Unable to invalidate entry: %s %s: %s",
                         toString(parent).c_str(),
                         name.c_str(),
                         std::strerror(-result));
    }
}

} // platform
} // fuse
} // mega

