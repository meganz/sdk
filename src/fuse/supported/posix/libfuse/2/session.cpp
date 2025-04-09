#include <cassert>
#include <cstring>
#include <vector>

#include <mega/common/task_executor.h>
#include <mega/common/utility.h>
#include <mega/fuse/common/logging.h>
#include <mega/fuse/common/mount_inode_id.h>
#include <mega/fuse/platform/constants.h>
#include <mega/fuse/platform/mount.h>
#include <mega/fuse/platform/mount_db.h>
#include <mega/fuse/platform/platform.h>
#include <mega/fuse/platform/request.h>
#include <mega/fuse/platform/service_context.h>
#include <mega/fuse/platform/session.h>
#include <mega/fuse/platform/utility.h>

namespace mega
{
namespace fuse
{
namespace platform
{

Session::ChannelDeleter::ChannelDeleter(Mount& mount)
  : mMount(&mount)
{
}

void Session::ChannelDeleter::operator()(fuse_chan* channel)
{
    if (!channel)
        return;

    fuse_session_remove_chan(channel);
    fuse_unmount(mMount->path().toPath(false).c_str(), channel);
}

void Session::forget(fuse_req_t request,
                     fuse_ino_t inode,
                     unsigned long num)
{
    SessionBase::forget(request, inode, num);
}

std::string Session::nextRequest()
{
    assert(mChannel);
    assert(mSession);

    auto channel = mChannel.get();
    std::string buffer(fuse_chan_bufsize(channel), '\0');

    while (true)
    {
        auto result = fuse_chan_recv(&channel, &buffer[0], buffer.size());

        if (!result)
            return std::string();

        if (result > 0)
        {
            buffer.resize(static_cast<std::size_t>(result));

            return buffer;
        }

        // We can hit this case when poll(...) tells us that our channel
        // has data available for reading but it really doesn't.
        //
        // Put differently, this is here to guard against spurious wakeups.
        if (result == -EAGAIN)
            return std::string();

        if (result == -EINTR)
            continue;

        throw FUSEErrorF("Unable to read request from session: %s",
                         std::strerror(-result));
    }
}

void Session::populateOperations(fuse_lowlevel_ops& operations)
{
    SessionBase::populateOperations(operations);

    operations.forget = &Session::forget;
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
                           std::string(newName),
                           0);
}

Session::Session(Mount& mount)
  : SessionBase(mount)
  , mChannel(nullptr, mount)
{
    auto arguments = Arguments(mount.name());
    auto path = mMount.path().toPath(false);

    ChannelPtr channel(fuse_mount(path.c_str(), arguments.get()), mount);
    if (!channel)
        throw FUSEErrorF("Unable to construct channel: %s", path.c_str());

    nonblocking(fuse_chan_fd(channel.get()), true);

    SessionPtr session(fuse_lowlevel_new(arguments.get(),
                                         &operations(),
                                         sizeof(fuse_lowlevel_ops),
                                         this));
    if (!session)
        throw FUSEErrorF("Unable to construct session: %s", path.c_str());

    fuse_session_add_chan(session.get(), channel.get());

    mChannel = std::move(channel);
    mSession = std::move(session);

    FUSEDebugF("Session constructed: %s", path.c_str());
}

int Session::descriptor() const
{
    assert(mChannel);

    return fuse_chan_fd(mChannel.get());
}

void Session::dispatch()
{
    // Sanity.
    assert(mChannel);
    assert(mSession);

    // Dispatch the request.
    if (auto request = nextRequest(); !request.empty())
        fuse_session_process(mSession.get(),
                             request.data(),
                             request.size(),
                             mChannel.get());
}

void Session::invalidateData(MountInodeID id, off_t offset, off_t length)
{
    assert(mChannel);
    assert(mSession);

    while (!exited())
    {
        auto result = fuse_lowlevel_notify_inval_inode(mChannel.get(),
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

    while (!exited())
    {
        auto result = fuse_lowlevel_notify_delete(mChannel.get(),
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

    while (!exited())
    {
        auto result = fuse_lowlevel_notify_inval_entry(mChannel.get(),
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

