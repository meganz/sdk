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
#include <mega/fuse/platform/session_base.h>

namespace mega
{
namespace fuse
{
namespace platform
{

SessionBase::Arguments::Arguments([[maybe_unused]] const std::string& name)
  : mArguments()
  , mPointers()
  , mStrings()
{
    mStrings.emplace_back("mega-fuse");

    // So it's easier to identify which FUSE mounts we created.
    mStrings.emplace_back(format("-ofsname=%s", FilesystemName.c_str()));
    mStrings.emplace_back(format("-osubtype=%s", FilesystemName.c_str()));

    POSIX_ONLY(mStrings.emplace_back(format("-ovolname=%s", name.c_str())));

    // Translate strings into a form meaningful to libfuse.
    for (auto& string : mStrings)
        mPointers.emplace_back(&string[0]);

    mPointers.emplace_back(nullptr);

    // Populate libfuse argument block.
    mArguments.allocated = 0;
    mArguments.argc = static_cast<int>(mStrings.size());
    mArguments.argv = &mPointers[0];
}

fuse_args* SessionBase::Arguments::get()
{
    return &mArguments;
}

SessionBase::SessionBase(Mount& mount)
  : mMount(mount)
  , mSession(nullptr)
{
}

SessionBase::~SessionBase()
{
}

fuse_lowlevel_ops SessionBase::mOperations{};
std::once_flag SessionBase::mOperationsInitialized;

void SessionBase::access(fuse_req_t request,
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

void SessionBase::lookup(fuse_req_t request,
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

void SessionBase::flush(fuse_req_t request,
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

void SessionBase::forget(fuse_req_t request,
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

void SessionBase::forget_multi(fuse_req_t request,
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

void SessionBase::fsync(fuse_req_t request,
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

void SessionBase::getattr(fuse_req_t request,
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

void SessionBase::mkdir(fuse_req_t request,
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

void SessionBase::mknod(fuse_req_t request,
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

Mount& SessionBase::mount(fuse_req_t request)
{
    return mount(fuse_req_userdata(request));
}

Mount& SessionBase::mount(void* context)
{
    assert(context);

    return *static_cast<Mount*>(context);
}

void SessionBase::open(fuse_req_t request,
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

void SessionBase::opendir(fuse_req_t request,
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

const fuse_lowlevel_ops& SessionBase::operations()
{
    // Make sure all operations have been populated.
    std::call_once(mOperationsInitialized, [this]() {
        populateOperations(mOperations);
    });

    // Return operations to caller.
    return mOperations;
}

void SessionBase::populateOperations(fuse_lowlevel_ops& operations)
{
    operations.access       = &SessionBase::access;
    operations.flush        = &SessionBase::flush;
    operations.forget       = &SessionBase::forget;
    operations.forget_multi = &SessionBase::forget_multi;
    operations.fsync        = &SessionBase::fsync;
    operations.getattr      = &SessionBase::getattr;
    operations.lookup       = &SessionBase::lookup;
    operations.mkdir        = &SessionBase::mkdir;
    operations.mknod        = &SessionBase::mknod;
    operations.open         = &SessionBase::open;
    operations.opendir      = &SessionBase::opendir;
    operations.read         = &SessionBase::read;
    operations.readdir      = &SessionBase::readdir;
    operations.release      = &SessionBase::release;
    operations.releasedir   = &SessionBase::releasedir;
    operations.rmdir        = &SessionBase::rmdir;
    operations.setattr      = &SessionBase::setattr;
    operations.statfs       = &SessionBase::statfs;
    operations.unlink       = &SessionBase::unlink;
    operations.write        = &SessionBase::write;
}

void SessionBase::read(fuse_req_t request,
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

void SessionBase::readdir(fuse_req_t request,
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

void SessionBase::release(fuse_req_t request,
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

void SessionBase::releasedir(fuse_req_t request,
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

void SessionBase::rmdir(fuse_req_t request,
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

void SessionBase::setattr(fuse_req_t request,
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

void SessionBase::statfs(fuse_req_t request, fuse_ino_t inode)
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

void SessionBase::unlink(fuse_req_t request,
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

void SessionBase::write(fuse_req_t request,
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

void SessionBase::destroy()
{
    // Sanity.
    assert(exited());

    mMount.destroy();
}

bool SessionBase::exited() const
{
    assert(mSession);

    return fuse_session_exited(mSession);
}

void SessionBase::invalidateAttributes(MountInodeID id)
{
    return invalidateData(id, -1, 0);
}

void SessionBase::invalidateData(MountInodeID id)
{
    return invalidateData(id, 0, 0);
}

} // platform
} // fuse
} // mega

