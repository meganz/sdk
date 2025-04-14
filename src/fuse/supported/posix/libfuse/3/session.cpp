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
#include <mega/fuse/platform/utility.h>
#include <mega/scoped_helpers.h>

namespace mega
{
namespace fuse
{
namespace platform
{

void Session::populateCapabilities(fuse_conn_info* connection)
{
    SessionBase::populateCapabilities(connection);

    connection->want |= FUSE_CAP_EXPLICIT_INVAL_DATA;
#ifdef FUSE_CAP_NO_EXPORT
    connection->want |= FUSE_CAP_NO_EXPORT;
#endif // FUSE_CAP_NO_EXPORT
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
                     const char* newName,
                     unsigned int flags)
{
#define ENTRY(name) {#name, name}
    static const std::map<std::string, int> names = {
        ENTRY(RENAME_EXCHANGE),
        ENTRY(RENAME_NOREPLACE)
    }; // names
#undef ENTRY

    MountInodeID parent_(parent);
    MountInodeID newParent_(newParent);

    FUSEDebugF("rename: parent: %s, name: %s, newParent: %s, "
               "newName: %s, request: %p",
               toString(parent_).c_str(),
               name,
               toString(newParent_).c_str(),
               newName,
               request);

    for (const auto& i : names)
    {
        if ((flags & i.second))
            FUSEDebugF("rename: flag: %s", i.first.c_str());
    }

    mount(request).execute(&Mount::rename,
                           true,
                           Request(request),
                           parent_,
                           std::string(name),
                           newParent_,
                           std::string(newName),
                           flags);
}

Session::Session(Mount& mount)
  : SessionBase(mount)
{
    auto arguments = Arguments(mount.name());
    auto path = mMount.path().toPath(false);

    SessionPtr session(fuse_session_new(arguments.get(),
                                        &operations(),
                                        sizeof(fuse_lowlevel_ops),
                                        this));
    if (!session)
        throw FUSEErrorF("Unable to construct session: %s", path.c_str());

    auto result = fuse_session_mount(session.get(), path.c_str());

    if (result < 0)
        throw FUSEErrorF("Unable to bind session to mount point: %s: %s",
                         path.c_str(),
                         std::strerror(-result));

    nonblocking(fuse_session_fd(session.get()), true);

    mSession = std::move(session);

    FUSEDebugF("Session constructed: %s", path.c_str());
}

int Session::descriptor() const
{
    assert(mSession);

    return fuse_session_fd(mSession.get());
}

void Session::dispatch()
{
    // Sanity.
    assert(mSession);

    struct fuse_buf buffer{};
    
    // Make sure our buffer is always released.
    auto releaser = makeScopedDestructor([&buffer]() {
        free(buffer.mem);
    }); // releaser

    // Try and read a request from libfuse.
    while (true)
    {
        auto result = fuse_session_receive_buf(mSession.get(), &buffer);

        // Ignore zero length requests.
        if (!result)
            return;

        // We've got a request.
        if (result > 0)
            break;

        // Spurious wakeup.
        if (result == -EAGAIN)
            return;

        // Call was interrupted, retry.
        if (result == -EINTR)
            continue;

        // Couldn't get a request.
        throw FUSEErrorF("Unable to read request from session: %s",
                         std::strerror(-result));
    }

    // Dispatch the request.
    fuse_session_process_buf(mSession.get(), &buffer);
}

void Session::invalidateData(MountInodeID id, off_t offset, off_t length)
{
    assert(mSession);

    while (!exited())
    {
        auto result = fuse_lowlevel_notify_inval_inode(mSession.get(),
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
    assert(mSession);

    while (!exited())
    {
        auto result = fuse_lowlevel_notify_delete(mSession.get(),
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
    assert(mSession);

    while (!exited())
    {
        auto result = fuse_lowlevel_notify_inval_entry(mSession.get(),
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

