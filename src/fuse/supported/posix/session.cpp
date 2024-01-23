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

static Mount& mount(fuse_req_t request);

static Mount& mount(void* context);

const fuse_lowlevel_ops Session::mOperations = {
    /*           init */ &init,
    /*        destroy */ nullptr,
    /*         lookup */ &lookup,
    /*         forget */ &forget,
    /*        getattr */ &getattr,
    /*        setattr */ &setattr,
    /*       readlink */ nullptr,
    /*          mknod */ &mknod,
    /*          mkdir */ &mkdir,
    /*         unlink */ &unlink,
    /*          rmdir */ &rmdir,
    /*        symlink */ nullptr,
    /*         rename */ &rename,
    /*           link */ nullptr,
    /*           open */ &open,
    /*           read */ &read,
    /*          write */ &write,
    /*          flush */ &flush,
    /*        release */ &release,
    /*          fsync */ &fsync,
    /*        opendir */ &opendir,
    /*        readdir */ &readdir,
    /*     releasedir */ &releasedir,
    /*       fsyncdir */ nullptr,
    /*         statfs */ &statfs,
    /*       setxattr */ nullptr,
    /*       getxattr */ nullptr,
    /*      listxattr */ nullptr,
    /*    removexattr */ nullptr,
    /*         access */ &access,
    /*         create */ nullptr,
    /*          getlk */ nullptr,
    /*          setlk */ nullptr,
    /*           bmap */ nullptr,
    /*          ioctl */ nullptr,
    /*           poll */ nullptr,
    /*      write_buf */ nullptr,
    /* retrieve_reply */ nullptr,
    /*   forget_multi */ &forget_multi,
    /*          flock */ nullptr,
    /*      fallocate */ nullptr,
#ifdef __APPLE__
    /*     reserved00 */ nullptr,
    /*     reserved01 */ nullptr,
    /*     reserved02 */ nullptr,
    /*        renamex */ nullptr,
    /*     setvolname */ nullptr,
    /*       exchange */ nullptr,
    /*      getxtimes */ nullptr,
    /*      setattr_x */ nullptr,
#endif // __APPLE__
}; // mOperations

void Session::access(fuse_req_t request,
                     fuse_ino_t inode,
                     int mask)
{
    MountInodeID inode_(inode);

    FUSEDebugF("access: inode: %s, mask: %x, request: %p",
               toString(inode_).c_str(),
               mask,
               request);

    mount(request).execute(&Mount::access,
                           true,
                           Request(request),
                           inode_,
                           mask);
}

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

void Session::lookup(fuse_req_t request,
                     fuse_ino_t parent,
                     const char* name)
{
    MountInodeID parent_(parent);

    FUSEDebugF("lookup: parent: %s, name: %s, request: %p",
               toString(parent_).c_str(),
               name,
               request);

    mount(request).execute(&Mount::lookup,
                           true,
                           Request(request),
                           parent_,
                           std::string(name));
}

void Session::flush(fuse_req_t request,
                    fuse_ino_t inode,
                    fuse_file_info* info)
{
    MountInodeID inode_(inode);

    FUSEDebugF("flush: inode: %s, request: %p",
               toString(inode_).c_str(),
               request);

    mount(request).execute(&Mount::flush,
                           true,
                           Request(request),
                           inode_,
                           *info);
}

void Session::forget(fuse_req_t request,
                     fuse_ino_t inode,
                     std::size_t num)
{
    MountInodeID inode_(inode);

    FUSEDebugF("forget: inode: %s, num: %zu, request: %p",
               toString(inode_).c_str(),
               num,
               request);

    mount(request).execute(&Mount::forget,
                           false,
                           Request(request),
                           inode_,
                           num);
}

void Session::forget_multi(fuse_req_t request,
                           std::size_t count,
                           fuse_forget_data* forgets)
{
    FUSEDebugF("forget_multi: count: %zu, forgets: %p, request: %p",
               count,
               forgets,
               request);

    std::vector<fuse_forget_data> forgets_(forgets, forgets + count);

    mount(request).execute(&Mount::forget_multi,
                           false,
                           Request(request),
                           std::move(forgets_));
}

void Session::fsync(fuse_req_t request,
                    fuse_ino_t inode,
                    int onlyData,
                    fuse_file_info* info)
{
    MountInodeID inode_(inode);

    FUSEDebugF("fsync: inode: %s, onlyData: %d, request: %p",
               toString(inode_).c_str(),
               onlyData,
               request);

    mount(request).execute(&Mount::fsync,
                           true,
                           Request(request),
                           inode_,
                           onlyData,
                           *info);
}

void Session::getattr(fuse_req_t request,
                      fuse_ino_t inode,
                      fuse_file_info*)
{
    MountInodeID inode_(inode);

    FUSEDebugF("getattr: inode: %s, request: %p",
               toString(inode_).c_str(),
               request);

    mount(request).execute(&Mount::getattr,
                           true,
                           Request(request),
                           inode_);
}

void Session::mkdir(fuse_req_t request,
                    fuse_ino_t parent,
                    const char* name,
                    mode_t mode)
{
    MountInodeID parent_(parent);

    FUSEDebugF("mkdir: mode: %jo, name: %s, parent: %s, request: %p",
               mode,
               name,
               toString(parent_).c_str(),
               request);

    mount(request).execute(&Mount::mkdir,
                           true,
                           Request(request),
                           parent_,
                           std::string(name),
                           static_cast<std::uintmax_t>(mode));
}

void Session::mknod(fuse_req_t request,
                    fuse_ino_t parent,
                    const char* name,
                    mode_t mode,
                    dev_t)
{
    MountInodeID parent_(parent);

    FUSEDebugF("mknod: mode: %jo, name: %s, parent: %s, request: %p",
               mode,
               name,
               toString(parent_).c_str(),
               request);

    mount(request).execute(&Mount::mknod,
                           true,
                           Request(request),
                           parent_,
                           std::string(name),
                           static_cast<std::uintmax_t>(mode));
}

void Session::open(fuse_req_t request,
                   fuse_ino_t inode,
                   fuse_file_info* info)
{
    MountInodeID inode_(inode);

    FUSEDebugF("open: inode: %s, request: %p",
               toString(inode_).c_str(),
               request);

    mount(request).execute(&Mount::open,
                           true,
                           Request(request),
                           inode_,
                           *info);
}

void Session::opendir(fuse_req_t request,
                      fuse_ino_t inode,
                      fuse_file_info* info)
{
    MountInodeID inode_(inode);

    FUSEDebugF("opendir: info: %p, inode: %s, request: %p",
               info,
               toString(inode_).c_str(),
               request);

    mount(request).execute(&Mount::opendir,
                           true,
                           Request(request),
                           inode_,
                           *info);
}

void Session::read(fuse_req_t request,
                   fuse_ino_t inode,
                   size_t size,
                   off_t offset,
                   fuse_file_info* info)
{
    MountInodeID inode_(inode);

    FUSEDebugF("read: inode: %s, offset: %ld, request: %p, size: %zu",
               toString(inode_).c_str(),
               offset,
               request,
               size);

    mount(request).execute(&Mount::read,
                           true,
                           Request(request),
                           inode_,
                           size,
                           offset,
                           *info);
}

void Session::readdir(fuse_req_t request,
                      fuse_ino_t inode,
                      std::size_t size,
                      off_t offset,
                      fuse_file_info* info)
{
    MountInodeID inode_(inode);

    FUSEDebugF("readdir: info: %p, inode: %s, offset: %d, size: %zu, request: %p",
               info,
               toString(inode_).c_str(),
               offset,
               size,
               request);

    mount(request).execute(&Mount::readdir,
                           true,
                           Request(request),
                           inode_,
                           size,
                           offset,
                           *info);
}

void Session::release(fuse_req_t request,
                      fuse_ino_t inode,
                      fuse_file_info* info)
{
    MountInodeID inode_(inode);

    FUSEDebugF("release: inode: %s, request: %p",
               toString(inode_).c_str(),
               request);

    mount(request).execute(&Mount::release,
                           true,
                           Request(request),
                           inode_,
                           *info);
}

void Session::releasedir(fuse_req_t request,
                         fuse_ino_t inode,
                         fuse_file_info* info)
{
    MountInodeID inode_(inode);

    FUSEDebugF("releasedir: info: %p, inode: %s, request: %p",
               info,
               toString(inode_).c_str(),
               request);

    mount(request).execute(&Mount::releasedir,
                           true,
                           Request(request),
                           inode_,
                           *info);
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

void Session::rmdir(fuse_req_t request,
                    fuse_ino_t parent,
                    const char* name)
{
    MountInodeID parent_(parent);

    FUSEDebugF("rmdir: name: %s, parent: %s, request: %p",
               name,
               toString(parent_).c_str(),
               request);

    mount(request).execute(&Mount::rmdir,
                           true,
                           Request(request),
                           parent_,
                           std::string(name));
}

void Session::setattr(fuse_req_t request,
                      fuse_ino_t inode,
                      struct stat* attributes,
                      int changes,
                      fuse_file_info*)
{
#define ENTRY(name) {#name, name}
    static std::map<std::string, int> names = {
        ENTRY(FUSE_SET_ATTR_ATIME),
        ENTRY(FUSE_SET_ATTR_ATIME_NOW),
        ENTRY(FUSE_SET_ATTR_GID),
        ENTRY(FUSE_SET_ATTR_MODE),
        ENTRY(FUSE_SET_ATTR_MTIME),
        ENTRY(FUSE_SET_ATTR_MTIME_NOW),
        ENTRY(FUSE_SET_ATTR_SIZE),
        ENTRY(FUSE_SET_ATTR_UID)
    }; // names
#undef ENTRY

    MountInodeID inode_(inode);

    FUSEDebugF("setattr: changes: %x, inode: %s, request: %p",
               changes,
               toString(inode_).c_str(),
               request);

    for (auto& i : names)
    {
        if ((changes & i.second))
            FUSEDebugF("setattr: attribute %s", i.first.c_str());
    }

    mount(request).execute(&Mount::setattr,
                           true,
                           Request(request),
                           inode_,
                           *attributes,
                           changes);
}

void Session::statfs(fuse_req_t request, fuse_ino_t inode)
{
    MountInodeID inode_(inode);

    FUSEDebugF("statfs: inode: %s, request: %p",
               toString(inode_).c_str(),
               request);

    mount(request).execute(&Mount::statfs,
                           true,
                           Request(request),
                           inode_);
}

void Session::unlink(fuse_req_t request,
                     fuse_ino_t parent,
                     const char* name)
{
    MountInodeID parent_(parent);

    FUSEDebugF("unlink: name: %s, parent: %s, request: %p",
               name,
               toString(parent_).c_str(),
               request);

    mount(request).execute(&Mount::unlink,
                           true,
                           Request(request),
                           parent_,
                           std::string(name));
}

void Session::write(fuse_req_t request,
                    fuse_ino_t inode,
                    const char* data,
                    size_t size,
                    off_t offset,
                    fuse_file_info* info)
{
    MountInodeID inode_(inode);

    FUSEDebugF("write: inode: %s, offset: %ld, request: %p, size: %zu",
               toString(inode_).c_str(),
               offset,
               request,
               size);

    mount(request).execute(&Mount::write,
                           true,
                           Request(request),
                           inode_,
                           std::string(data, size),
                           offset,
                           *info);
}

Session::Session(Mount& mount)
  : mChannel(nullptr)
  , mMount(mount)
  , mSession(nullptr)
{
    std::vector<char*> pointers;
    std::vector<std::string> values;

    values.emplace_back("mega-fuse");

    // So it's easier to identify which FUSE mounts we created.
    values.emplace_back(format("-ofsname=%s",  FilesystemName.c_str()));
    values.emplace_back(format("-osubtype=%s", FilesystemName.c_str()));

    LINUX_ONLY(values.emplace_back("-ononempty"));
    POSIX_ONLY(values.emplace_back("-ovolname=" + mMount.name()));

    for (auto& value : values)
        pointers.emplace_back(&value[0]);

    pointers.emplace_back(nullptr);

    fuse_args arguments;

    arguments.allocated = 0;
    arguments.argc = static_cast<int>(values.size());
    arguments.argv = &pointers[0];

    auto path = mMount.path().toPath(false);

    mChannel = fuse_mount(path.c_str(), &arguments);
    if (!mChannel)
        throw FUSEErrorF("Unable to construct channel: %s", path.c_str());

    mSession = fuse_lowlevel_new(&arguments,
                                 &mOperations,
                                 sizeof(mOperations),
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

void Session::dispatch(std::string request)
{
    // Sanity.
    assert(mChannel);
    assert(mSession);

    // Session's been terminated.
    if (fuse_session_exited(mSession))
        return mMount.destroy();

    // Sanity.
    assert(!request.empty());

    // Dispatch the request.
    fuse_session_process(mSession,
                         request.data(),
                         request.size(),
                         mChannel);
}

bool Session::exited() const
{
    assert(mSession);

    return fuse_session_exited(mSession);
}

void Session::invalidateAttributes(MountInodeID id)
{
    return invalidateData(id, -1, 0);
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

void Session::invalidateData(MountInodeID id)
{
    return invalidateData(id, 0, 0);
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

        throw FUSEErrorF("Unable to read request from session: %d", -result);
    }
}

Mount& mount(fuse_req_t request)
{
    return mount(fuse_req_userdata(request));
}

Mount& mount(void* context)
{
    assert(context);

    return *static_cast<Mount*>(context);
}

} // platform
} // fuse
} // mega

