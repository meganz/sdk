#include <chrono>

#include <cassert>

#include <mega/common/error_or.h>
#include <mega/fuse/common/client.h>
#include <mega/fuse/common/directory_inode.h>
#include <mega/fuse/common/file_inode.h>
#include <mega/fuse/common/file_io_context.h>
#include <mega/fuse/common/file_move_flag.h>
#include <mega/fuse/common/file_open_flag.h>
#include <mega/fuse/common/inode.h>
#include <mega/fuse/common/inode_id.h>
#include <mega/fuse/common/inode_info.h>
#include <mega/fuse/common/logging.h>
#include <mega/fuse/common/mount_event.h>
#include <mega/fuse/common/mount_event_type.h>
#include <mega/fuse/common/mount_inode_id.h>
#include <mega/fuse/common/mount_result.h>
#include <mega/fuse/common/service.h>
#include <mega/fuse/platform/constants.h>
#include <mega/fuse/platform/directory_context.h>
#include <mega/fuse/platform/file_context.h>
#include <mega/fuse/platform/library.h>
#include <mega/fuse/platform/mount.h>
#include <mega/fuse/platform/mount_db.h>
#include <mega/fuse/platform/platform.h>
#include <mega/fuse/platform/request.h>
#include <mega/fuse/platform/service_context.h>
#include <mega/fuse/platform/utility.h>

namespace mega
{
namespace fuse
{
namespace platform
{

// For compatibility with libfuse2.
#ifndef HAS_RENAME_FLAGS
#define RENAME_EXCHANGE  0
#define RENAME_NOREPLACE 0
#endif // !HAS_RENAME_FLAGS

using namespace common;

void Mount::access(Request request,
                   MountInodeID inode,
                   int mask)
{
    // Reject if the originating process is self
    if (isSelf(request))
        return request.replyError(EPERM);

    // Mask is invalid.
    if (mask != F_OK && mask > (R_OK | W_OK | X_OK))
        return request.replyError(EINVAL);

    // Try and locate the specified inode.
    auto ref = get(inode);

    // Inode doesn't exist.
    if (!ref)
        return request.replyError(ENOENT);

    // Existence check has been satisfied.
    if (mask == F_OK)
        return request.replyOk();

    // What are we allowed to do with this inode?
    auto permissions = ref->permissions();

    // Only directories are executable.
    if ((mask & X_OK) && ref->file())
        return request.replyError(EACCES);

    // Inodes are always readable.
    if (!(mask & W_OK))
        return request.replyOk();

    // Inode's writable.
    if (permissions == FULL && writable())
        return request.replyOk();

    // Inode or mount is read-only.
    request.replyError(EROFS);
}

void Mount::destroy()
{
    // Destroy the mount.
    auto destroy = [&](Activity& activity, const Task&) {
        // Remove the mount from the database.
        auto ptr = ([&](Activity) {
            return mMountDB.remove(*this);
        })(std::move(activity));

        // Destroy the mount.
        ptr.reset();
    }; // destroy

    // Schedule the mount for destruction.
    mMountDB.mContext.mExecutor.execute(std::bind(std::move(destroy),
                                                  mActivities.begin(),
                                                  std::placeholders::_1),
                                        true);
}

void Mount::doUnlink(Request request,
                     MountInodeID parent,
                     std::function<Error(InodeRef)> predicate,
                     const std::string& name)
{
    // Reject if the originating process is self
    if (isSelf(request))
        return request.replyError(EPERM);

    // Get our hands on the parent.
    auto ref = get(parent);

    // Parent doesn't exist.
    if (!ref)
        return request.replyError(ENOENT);

    auto directoryRef = ref->directory();

    // Parent's not a directory.
    if (!directoryRef)
        return request.replyError(ENOTDIR);

    // Mount's read-only.
    if (!writable())
        return request.replyError(EROFS);

    // Try and unlink the specified child.
    auto result = directoryRef->unlink(name, std::move(predicate));

    // Reply to FUSE.
    request.replyError(translate(result));
}

void Mount::lookup(Request request,
                   MountInodeID parent,
                   const std::string& name)
{
    // Reject if the originating process is self
    if (isSelf(request))
        return request.replyError(EPERM);

    // Look up the parent.
    auto ref = get(parent);

    // Parent doesn't exist.
    if (!ref)
        return request.replyError(ENOENT);

    auto directoryRef = ref->directory();

    // Parent isn't a directory.
    if (!directoryRef)
        return request.replyError(ENOTDIR);

    // Specified name is way too long.
    if (name.size() > MaxNameLength)
        return request.replyError(ENAMETOOLONG);

    auto childRef = directoryRef->get(name);

    // Child doesn't exist.
    if (!childRef)
        return request.replyError(ENOENT);

    auto info = childRef->info();

    // Mount's not writable.
    if (!writable())
        info.mPermissions = RDONLY;

    pin(childRef, info);

    auto entry = fuse_entry_param();

    std::memset(&entry, 0, sizeof(entry));

    entry.attr_timeout = AttributeTimeout;
    entry.entry_timeout = EntryTimeout;

    translate(entry, map(info.mID), info);

    request.replyEntry(entry);
}

void Mount::flush(Request request, MountInodeID, fuse_file_info&)
{
    request.replyOk();
}

void Mount::forget(Request request,
                   MountInodeID inode,
                   std::size_t num)
{
    // Reject if the originating process is self
    if (isSelf(request))
        return request.replyError(EPERM);

    // Locate the specified inode.
    auto ref = get(inode, true);

    // Pinned inodes are *always* in memory.
    assert(ref);

    // Unpin the inode.
    unpin(std::move(ref), num);

    request.replyNone();
}

void Mount::forget_multi(Request request,
                         const std::vector<fuse_forget_data>& forgets)
{
    assert(!forgets.empty());

    // Reject if the originating process is self
    if (isSelf(request))
        return request.replyError(EPERM);

    for (auto& forget : forgets)
    {
        // Lookup the specified inode.
        auto ref = get(MountInodeID(forget.ino), true);

        // Pinned inodes are always in memory.
        assert(ref);

        // Unpin the inode.
        unpin(std::move(ref), forget.nlookup);
    }

    request.replyNone();
}

void Mount::fsync(Request request, MountInodeID, bool, fuse_file_info& info)
{
    // Reject if the originating process is self
    if (isSelf(request))
        return request.replyError(EPERM);

    // Get our hands on the file's context.
    auto* context = reinterpret_cast<FileContext*>(info.fh);

    // Sanity.
    assert(context);

    // Try and flush any modifications made to the file.
    auto result = context->flush();

    // Let FUSE know if the file was flushed.
    request.replyError(translate(result));
}

void Mount::getattr(Request request,
                    MountInodeID inode)
{
    // Reject if the originating process is self
    if (isSelf(request))
        return request.replyError(EPERM);

    // Try and get our hands on the inode.
    auto ref = get(inode);

    // Inode doesn't exist.
    if (!ref)
        return request.replyError(ENOENT);

    // Retrieve a description of this inode.
    auto info = ref->info();

    // Mount's not writable.
    if (!writable())
        info.mPermissions = RDONLY;

    struct stat attributes;

    translate(attributes, inode, info);

    request.replyAttributes(attributes, AttributeTimeout);
}

void Mount::mkdir(Request request,
                  MountInodeID parent,
                  const std::string& name,
                  mode_t mode)
{
    mknod(request, parent, std::move(name), mode | S_IFDIR);
}

void Mount::mknod(Request request,
                  MountInodeID parent,
                  const std::string& name,
                  mode_t mode)
{
    // Reject if the originating process is self
    if (isSelf(request))
        return request.replyError(EPERM);

    // Try and locate the parent.
    auto ref = get(parent);

    // Parent doesn't exist.
    if (!ref)
        return request.replyError(ENOENT);

    auto directoryRef = ref->directory();

    // Parent isn't a directory.
    if (!directoryRef)
        return request.replyError(ENOTDIR);
    
    // Mount isn't writable.
    if (!writable())
        return request.replyError(EROFS);

    // Only directories and regular files are supported.
    if (!S_ISDIR(mode) && !S_ISREG(mode))
        return request.replyError(EPERM);

    // Try and create the new inode.
    auto result =
      S_ISDIR(mode) ? directoryRef->makeDirectory(*this, name)
                    : directoryRef->makeFile(*this, name);

    // Couldn't create the inode.
    if (!result)
        return request.replyError(translate(result.error()));

    // Extract description of new inode.
    auto info = std::move(std::get<1>(*result));

    // Translate description into something meaningful to FUSE.
    auto entry = fuse_entry_param();

    translate(entry, MountInodeID(info.mID), info);

    // Pin inode in memory.
    pin(std::move(std::get<0>(*result)), info);

    // Respond to FUSE.
    request.replyEntry(entry);
}

void Mount::open(Request request,
                 MountInodeID inode,
                 fuse_file_info& info)
{
    // Reject if the originating process is self
    if (isSelf(request))
        return request.replyError(EPERM);

    // Check for invalid flags.
    if (info.direct_io)
        return request.replyError(EINVAL);

    // Try and get a reference to the specified inode.
    auto ref = get(inode);

    // Inode doesn't exist.
    if (!ref)
        return request.replyError(ENOENT);

    // Does the inode represent a file?
    auto fileRef = ref->file();

    // Inode represents a directory.
    if (!fileRef)
        return request.replyError(EISDIR);

    // Compute flags.
    FileOpenFlags flags = 0;

    // User wants to open the file for writing.
    if ((info.flags & (O_RDWR | O_WRONLY)))
    {
        // Mount is read only.
        if (!writable())
            return request.replyError(EROFS);

        // File's read only.
        if (fileRef->permissions() != FULL)
            return request.replyError(EROFS);

        flags |= FOF_WRITABLE;

        // User wants to append data to the file.
        if ((info.flags & O_APPEND))
            flags |= FOF_APPEND;

        // User wants to truncate existing content.
        if ((info.flags & O_TRUNC))
            flags |= FOF_TRUNCATE;
    }

    // Try and open the file.
    auto result = fileRef->open(*this, flags);

    // Couldn't open the file.
    if (!result)
        return request.replyError(translate(result.error()));

    // Convenience.
    auto context = std::move(*result);

    // Populate FUSE's file context.
    info.direct_io = false;
    info.fh = reinterpret_cast<std::uint64_t>(context.get());
    info.keep_cache = !(flags & FOF_TRUNCATE);
    info.nonseekable = false;

    // Let FUSE know the file's open.
    request.replyOpen(info);

    // FUSE now owns the file context.
    static_cast<void>(context.release());
}

void Mount::opendir(Request request,
                    MountInodeID inode,
                    fuse_file_info& info)
{
    // Reject if the originating process is self
    if (isSelf(request))
        return request.replyError(EPERM);

    // Try and get a reference to the specified inode.
    auto ref = get(inode);

    // Inode doesn't exist.
    if (!ref)
        return request.replyError(ENOENT);

    // Does the inode represent a directory?
    auto directoryRef = ref->directory();

    // Inode isn't a directory.
    if (!directoryRef)
        return request.replyError(ENOTDIR);

    // Instantiate directory iterator context.
    auto context =
      std::make_unique<DirectoryContext>(std::move(directoryRef), *this);

    // Pass context to FUSE.
    info.fh = reinterpret_cast<std::uint64_t>(context.get());

    request.replyOpen(info);

    // Context is in FUSE's hands now.
    static_cast<void>(context.release());
}

void Mount::read(Request request,
                 MountInodeID,
                 std::size_t size,
                 off_t offset,
                 fuse_file_info& info)
{
    // Reject if the originating process is self
    if (isSelf(request))
        return request.replyError(EPERM);

    // Get our hands on the file's context.
    auto* context = reinterpret_cast<FileContext*>(info.fh);

    // Sanity.
    assert(context);

    // Try and read the file.
    auto result = context->read(offset, static_cast<unsigned int>(size));

    // Couldn't read the file.
    if (!result)
        return request.replyError(translate(result.error()));

    // Pass read data to FUSE.
    request.replyBuffer(std::move(*result));
}

void Mount::readdir(Request request,
                    MountInodeID,
                    std::size_t size,
                    off_t offset,
                    fuse_file_info& info)
{
    // Reject if the originating process is self
    if (isSelf(request))
        return request.replyError(EPERM);

    // Retrieve directory context.
    auto* context = reinterpret_cast<DirectoryContext*>(info.fh);

    // Sanity.
    assert(context);
    assert(offset >= 0);

    // Where we'll be storing directory entries.
    std::string buffer;

    // Type safety.
    auto m = static_cast<std::size_t>(offset);
    auto n = context->size();

    // Collect directory entries.
    //
    // NOTE: The first two directory entries are always symlinks to the
    // directory itself (.) and to its immediate parent (..).
    while (m < n)
    {
        // Get information about the current child.
        auto info = context->get(m);

        // Child no longer exists.
        if (!info.mID)
        {
            // Either we or our parent no longer exist.
            if (m++ < 2)
                return request.replyBuffer(std::string());

            // Process the next child.
            continue;
        }

        struct stat attributes;

        // Translate info into something meaningful.
        translate(attributes, map(info.mID), info);

        // Try and add the entry to our buffer.
        if (!request.addDirEntry(attributes,
                                 buffer,
                                 info.mName,
                                 ++m,
                                 size - buffer.size()))
            break;
    }

    // Report directory entries to FUSE.
    request.replyBuffer(std::move(buffer));
}

void Mount::release(Request request, MountInodeID, fuse_file_info& info)
{
    // Reject if the originating process is self
    if (isSelf(request))
        return request.replyError(EPERM);

    // Get our hands on the context.
    auto* context = reinterpret_cast<FileContext*>(info.fh);

    // Make sure the context is properly released.
    delete context;

    // Let FUSE know that the context's been released.
    request.replyOk();
}

void Mount::releasedir(Request request,
                       MountInodeID,
                       fuse_file_info& info)
{
    // Reject if the originating process is self
    if (isSelf(request))
        return request.replyError(EPERM);

    // Retrieve directory context.
    auto* context = reinterpret_cast<DirectoryContext*>(info.fh);

    // Sanity.
    assert(context);

    // Release context.
    delete context;

    // Let FUSE know we're done.
    request.replyOk();
}

void Mount::rename(Request request,
                   MountInodeID sourceParent,
                   const std::string& sourceName,
                   MountInodeID targetParent,
                   const std::string& targetName,
                   unsigned int flags)
{
    // Reject if the originating process is self
    if (isSelf(request))
        return request.replyError(EPERM);

    // Get our hands on the parents.
    auto sourceRef = get(sourceParent);
    auto targetRef = get(targetParent);

    // Either parent doesn't exist.
    if (!sourceRef || !targetRef)
        return request.replyError(ENOENT);

    auto sourceDirectoryRef = sourceRef->directory();
    auto targetDirectoryRef = targetRef->directory();

    // Either parent isn't a directory.
    if (!sourceDirectoryRef || !targetDirectoryRef)
        return request.replyError(ENOTDIR);

    // Mount isn't writable.
    if (!writable())
        return request.replyError(EROFS);

    FileMoveFlags moveFlags = 0;

    // Caller doesn't want to replace any existing file.
    if ((flags & RENAME_NOREPLACE))
        moveFlags |= FILE_MOVE_NO_REPLACE;

    // Caller wants to atomically exchange two files.
    if ((flags & RENAME_EXCHANGE))
        moveFlags |= FILE_MOVE_EXCHANGE;

    // Make sure only a single flag has been set.
    if (!valid(moveFlags))
        return request.replyError(EINVAL);

    // Perform the move.
    auto result =
      sourceDirectoryRef->move(sourceName,
                               targetName,
                               std::move(targetDirectoryRef),
                               moveFlags);

    // Reply to FUSE.
    request.replyError(translate(result));
}

void Mount::rmdir(Request request,
                  MountInodeID parent,
                  const std::string& name)
{
    auto predicate = [](InodeRef ref) {
        return ref->file() ? API_FUSE_ENOTDIR : API_OK;
    }; // predicate

    doUnlink(request, parent, std::move(predicate), name);
}

void Mount::setattr(Request request,
                    MountInodeID inode,
                    struct stat& attributes,
                    int changes)
{
    // Reject if the originating process is self
    if (isSelf(request))
        return request.replyError(EPERM);

    // Get our hands on the inode.
    auto ref = get(inode);

    // Inode doesn't exist.
    if (!ref)
        return request.replyError(ENOENT);

    // Mount's read-only.
    if (!writable())
        return request.replyError(EROFS);

    // Inode's read-only.
    if (ref->permissions() != FULL)
        return request.replyError(EROFS);

    // Clarity;
    constexpr auto EOK = 0;

    auto ownership = [&]() {
        auto group = changes & FUSE_SET_ATTR_GID;
        auto owner = changes & FUSE_SET_ATTR_UID;

        // User's not changing ownership.
        if (!(group | owner))
            return EOK;

        // Can't transfer ownership to another group.
        if (group && attributes.st_gid != getgid())
            return EPERM;

        // Can't transfer ownership to another user.
        if (owner && attributes.st_uid != geteuid())
            return EPERM;

        // It's okay to transfer ownership to yourself.
        return EOK;
    }; // ownership

    auto size = [&]() {
        // User's not changing the file's size.
        if (!(changes & FUSE_SET_ATTR_SIZE))
            return EOK;

        auto fileRef = ref->file();

        // Can't change the size of a directory.
        if (!fileRef)
            return EISDIR;

        // Try and truncate the file.
        auto result = fileRef->truncate(*this,
                                        attributes.st_size,
                                        false);

        // Translate result.
        return translate(result);
    }; // size

    auto time = [&]() {
        // Convenience.
        using std::chrono::system_clock;

        // Not changing modification time.
        //
        // NOTE: If you're wondering why we don't handle ATIME, the reasons
        // are pretty straight forward. First, MEGA itself doesn't have any
        // concept of a file access time so if we did want to implement it,
        // we'd have to record extra data in FUSE's database.
        //
        // Second, we don't want to store any information in FUSE's database
        // unless we really have to. That is, if a user's doing nothing but
        // listing files, we shouldn't have to add anything to our database
        // since we aren't recording any local changes.
        //
        // That explains why we don't implement ATIME but it doesn't explain
        // why we don't just return an error to userspace, to let the system
        // know that we don't support it. The reason we pretend as if the
        // ATIME succeeded is that if we don't, many tools will issue
        // warnings to the user and QA felt this was scary, even though the
        // warnings themselves are benign.
        //
        // So for now, we just pretend that we've set the ATIME.
        if (!(changes & FUSE_SET_ATTR_MTIME))
            return EOK;

        auto fileRef = ref->file();

        // Directories don't have a modification time.
        if (!fileRef)
            return EOK;

        // Assume the user has a specific time in mind.
        m_time_t modified = attributes.st_mtime;

        // User wants to set the modification time to now.
        if ((changes & FUSE_SET_ATTR_MTIME_NOW))
            modified = system_clock::to_time_t(system_clock::now());

        // Try and set the file's modification time.
        auto result = fileRef->touch(*this, modified);

        // Translate result.
        return translate(result);
    }; // time

    // Update ownership if necessary.
    if (auto result = ownership())
        return request.replyError(result);

    // Update size if necessary.
    if (auto result = size())
        return request.replyError(result);

    // Update time if necessary.
    if (auto result = time())
        return request.replyError(result);

    // Retrieve a current description of this node.
    translate(attributes, inode, ref->info());

    // Forward description to userspace.
    request.replyAttributes(attributes, AttributeTimeout);
}

void Mount::statfs(Request request, MountInodeID inode)
{
    // Reject if the originating process is self
    if (isSelf(request))
        return request.replyError(EPERM);

    // Get our hands on the inode.
    auto ref = get(inode);

    // Inode doesn't exist.
    if (!ref)
        return request.replyError(ENOENT);

    // Try and retrieve our storage statistics.
    auto info = mMountDB.client().storageInfo();

    // Couldn't retrieve our storage statistics.
    if (!info)
        return request.replyError(translate(info.error()));

    struct statvfs attributes;

    // Make sure unset members have a well defined state.
    std::memset(&attributes, 0, sizeof(attributes));

    // Convenience.
    auto available = static_cast<fsblkcnt_t>(info->mAvailable);
    auto capacity = static_cast<fsblkcnt_t>(info->mCapacity);

    // Populate filesystem statistics.
    //
    // NOTE: We only really set the attributes that make sense to us here.
    //
    // For instance, we don't set f_files or f_free because there isn't any
    // kind of limit on how many inodes the user can create unlike a real
    // filesystem such as ext2 which must explicitly reserve space for such
    // structures.
    //
    // This behavior is permitted by the statvfs(...) documentation.
    attributes.f_bavail = available / BlockSize;
    attributes.f_bfree = attributes.f_bavail;
    attributes.f_blocks = capacity / BlockSize;
    attributes.f_bsize = BlockSize;
    attributes.f_frsize = BlockSize;
    attributes.f_fsid = FilesystemID;
    attributes.f_namemax = MaxNameLength;

    // Forward statistics to FUSE.
    request.replyAttributes(attributes);
}

void Mount::unlink(Request request,
                   MountInodeID parent,
                   const std::string& name)
{
    auto predicate = [](InodeRef ref) {
        if (ref->directory())
            return LINUX_OR_POSIX(API_FUSE_EISDIR, API_FUSE_EPERM);

        return API_OK;
    }; // predicate

    doUnlink(request, parent, std::move(predicate), name);
}

void Mount::write(Request request,
                  MountInodeID,
                  const string& data,
                  off_t offset,
                  fuse_file_info& info)
{
    // Reject if the originating process is self
    if (isSelf(request))
        return request.replyError(EPERM);

    // Get our hands on the context.
    auto* context = reinterpret_cast<FileContext*>(info.fh);

    // Sanity.
    assert(context);
    assert(offset >= 0);

    // Try and write the file.
    auto result = context->write(data.c_str(),
                                 static_cast<m_off_t>(data.length()),
                                 offset,
                                 false);

    // Couldn't write the file.
    if (!result)
        return request.replyError(translate(result.error()));

    // Let FUSE know whether the data was written.
    request.replyWritten(*result);
}

// Check if the request's originating process is this process.
// Don't allow SDK to access the mount if the request is from itself as it will have deadlock issues
// due to single-threaded execution loop of the SDK.
//
bool Mount::isSelf(const Request& request) const
{
    const auto originatingPid = request.process();

    return originatingPid != 0 && originatingPid == getpid();
}

Mount::Mount(const MountInfo& info, MountDB& mountDB)
  : fuse::Mount(info, mountDB)
  , mActivities()
  , mExecutor(mountDB.executorFlags(), logger())
  , mPath(info.mPath)
  , mSession(*this)
  , mInvalidator(mSession)
{
    // Let the database know a new session has been added.
    mMountDB.sessionAdded(mSession);

    FUSEDebugF("Mount constructed: %s",
               path().toPath(false).c_str());
}

Mount::~Mount()
{
    // Let the database know that a session is being removed.
    mMountDB.sessionRemoved(mSession);

    // Wait for all outstanding requests to complete.
    mActivities.waitUntilIdle();

    // It's safe for us to be destroyed.
    FUSEDebugF("Mount destroyed: %s", path().toPath(false).c_str());
}

void Mount::executorFlags(const TaskExecutorFlags& flags)
{
    // Updates this mount's executor flags.
    auto update = [flags, this](Activity&, const Task&) {
        mExecutor.flags(flags);
    }; // update

    mExecutor.execute(std::bind(std::move(update),
                                mActivities.begin(),
                                std::placeholders::_1),
                      true);
}

void Mount::invalidateAttributes(InodeID id)
{
    mInvalidator.invalidateAttributes(mActivities, map(id));
}

void Mount::invalidateData(InodeID id, m_off_t offset, m_off_t size)
{
    mInvalidator.invalidateData(mActivities, map(id), offset, size);
}

void Mount::invalidateData(InodeID id)
{
    invalidateData(id, 0, 0);
}

void Mount::invalidateEntry(const std::string& name,
                            InodeID child,
                            InodeID parent)
{
    assert(child);
    assert(parent);

    mInvalidator.invalidateEntry(mActivities,
                                 map(child),
                                 name,
                                 map(parent));
}

void Mount::invalidateEntry(const std::string& name, InodeID parent)
{
    assert(parent);

    mInvalidator.invalidateEntry(mActivities, map(parent), name);
}

InodeID Mount::map(MountInodeID id) const
{
    if (id.get() != FUSE_ROOT_ID)
        return InodeID(id);

    return InodeID(handle());
}

MountInodeID Mount::map(InodeID id) const
{
    if (id == handle())
        return MountInodeID(FUSE_ROOT_ID);

    return MountInodeID(id);
}

NormalizedPath Mount::path() const
{
    return mPath;
}

} // platform
} // fuse
} // mega

