#include <cassert>

#include <mega/fuse/common/badge.h>
#include <mega/fuse/common/client.h>
#include <mega/fuse/common/directory_inode.h>
#include <mega/fuse/common/error_or.h>
#include <mega/fuse/common/file_cache.h>
#include <mega/fuse/common/file_info.h>
#include <mega/fuse/common/file_inode.h>
#include <mega/fuse/common/file_io_context.h>
#include <mega/fuse/common/file_open_flag.h>
#include <mega/fuse/common/inode_badge.h>
#include <mega/fuse/common/inode_db.h>
#include <mega/fuse/common/inode_info.h>
#include <mega/fuse/common/logging.h>
#include <mega/fuse/common/node_info.h>
#include <mega/fuse/common/ref.h>
#include <mega/fuse/platform/file_context.h>

namespace mega
{
namespace fuse
{

void FileInode::remove(RefBadge, InodeDBLock lock)
{
    // Remove this file from the inode database.
    mInodeDB.remove(*this, std::move(lock));
}

FileInode::FileInode(InodeID id,
                     const NodeInfo& info,
                     InodeDB& inodeDB)
  : Inode(id, info, inodeDB)
  , mHandle(info.mHandle)
  , mInfo()
  , mInfoLock()
{
}

FileInode::~FileInode()
{
}

bool FileInode::cached() const
{
    return fileInfo();
}

FileExtension FileInode::extension() const
{
    // Retrieve extension from file info, if present.
    {
        std::lock_guard<std::mutex> guard(mInfoLock);

        if (mInfo)
            return mInfo->extension();
    }

    // Compute extension based on current file name.
    return mInodeDB.fileExtensionDB().getFromPath(name());
}

FileInodeRef FileInode::file()
{
    return FileInodeRef(this);
}

void FileInode::fileInfo(FileInfoRef info)
{
    // Sanity.
    assert(info);

    std::lock_guard<std::mutex> guard(mInfoLock);

    // A file's info should only be injected once.
    assert(!mInfo);

    mInfo = std::move(info);
}

FileInfoRef FileInode::fileInfo() const
{
    std::lock_guard<std::mutex> guard(mInfoLock);

    return mInfo;
}

void FileInode::handle(NodeHandle handle)
{
    InodeDBLock guard(mInodeDB);

    // Sanity.
    assert(!handle.isUndef());

    // Update the database.
    mInodeDB.handle(*this, mHandle, handle);
}

NodeHandle FileInode::handle() const
{
    InodeDBLock guard(mInodeDB);

    return mHandle;
}

void FileInode::info(const NodeInfo& info)
{
    InodeDBLock guard(mInodeDB);

    // Sanity.
    assert(!info.mHandle.isUndef());
    assert(!info.mIsDirectory);

    // Update this inode's cached description.
    Inode::info(info, guard);

    // Update this inode's handle.
    mInodeDB.handle(*this, mHandle, info.mHandle);
}

InodeInfo FileInode::info() const
{
    // Retrieve a reference to this inode's local state, if any.
    auto fileInfo = this->fileInfo();

    // Acquire lock.
    InodeDBLock lock(mInodeDB);

    // Return cached information if we've been removed.
    if (removed())
    {
        InodeInfo info;

        info.mID = mID;
        info.mIsDirectory = false;
        info.mName = mName;
        info.mModified = mModified;
        info.mParentID = InodeID(mParentHandle);
        info.mPermissions = mPermissions;

        // Latch local state, if any.
        if (fileInfo)
            fileInfo->get(info.mModified, info.mSize);

        return info;
    }

    // Convenience.
    auto& client = mInodeDB.client();

    while (true)
    {
        // Where we will store our description.
        auto info = NodeInfo();
        
        // Latch cached attributes.
        info.mHandle = mHandle;
        info.mModified = mModified;
        info.mName = mName;
        info.mParentHandle = mParentHandle;
        info.mPermissions = mPermissions;
        info.mIsDirectory = false;

        // We existed in the cloud.
        if (!info.mHandle.isUndef())
        {
            // Release the lock so we can call the client.
            lock.unlock();

            // Retrieve this inode's local state, if any.
            if (!fileInfo)
                fileInfo = this->fileInfo();

            // Try and retrieve our description.
            auto info_ = client.get(info.mHandle);

            // Reacquire lock.
            lock.lock();

            // Another thread's altered our attributes.
            if (info.mHandle != mHandle
                || info.mModified != mModified
                || info.mParentHandle != mParentHandle
                || info.mPermissions != mPermissions
                || info.mName != mName)
                continue;

            // We no longer exist.
            if (!info_)
            {
                // Latch local state, if any.
                if (fileInfo)
                    fileInfo->get(info.mModified, info.mSize);

                // Mark ourselves as removed.
                removed(true);

                // Return our description to the caller.
                return InodeInfo(mID, std::move(info));
            }

            // Latch latest description from cloud.
            info = std::move(*info_);

            // Latch local state, if any.
            if (fileInfo)
                fileInfo->get(info.mModified, info.mSize);

            // Update our cached attributes.
            const_cast<FileInode*>(this)->Inode::info(info, lock);

            // Return description to caller.
            return InodeInfo(mID, std::move(info));
        }

        // We don't exist in the cloud.

        // Release the lock so we can call the client.
        lock.unlock();

        // Try and retrieve our parent's permissions.
        auto permissions = client.permissions(info.mParentHandle);

        // Reacquire lock.
        lock.lock();

        // We've been uploaded by another thread.
        if (info.mHandle != mHandle)
            continue;

        // Another thread's changed our attributes.
        if (info.mModified != mModified
            || info.mParentHandle != mParentHandle
            || info.mPermissions != mPermissions
            || info.mName != mName)
            continue;

        // Sanity.
        assert(fileInfo);

        // Latch local state.
        fileInfo->get(info.mModified, info.mSize);

        // Latch current permissions.
        if (permissions != ACCESS_UNKNOWN)
            info.mPermissions = permissions;

        // Update cached attributes.
        mModified = info.mModified;
        mPermissions = info.mPermissions;

        // Return description to caller.
        return InodeInfo(mID, std::move(info));
    }
}

void FileInode::modified(bool modified)
{
    mInodeDB.modified(id(), modified);
}

Error FileInode::move(InodeBadge, const std::string& name, DirectoryInodeRef parent)
{
    // Sanity.
    assert(parent);

    // Ask the Inode DB to move us into parent.
    return mInodeDB.move(FileInodeRef(this),
                         name,
                         std::move(parent));
}

ErrorOr<platform::FileContextPtr> FileInode::open(Mount& mount,
                                                  FileOpenFlags flags)
{
    // Are we opening the file for writing?
    auto writable = (flags & FOF_WRITABLE) > 0;

    // File's read-only.
    if (writable && permissions() != FULL)
        return API_FUSE_EROFS;

    // Get a reference to ourself.
    auto ref = FileInodeRef(this);
    
    // Get a reference to our file context.
    auto context = mInodeDB.fileCache().context(std::move(ref));

    // Does the user want to truncate the file?
    auto truncate = (flags & FOF_TRUNCATE) > 0;

    // Sanity.
    assert(!truncate || writable);

    // Try and open the file for IO.
    auto result = context->open(mount, truncate);

    // Couldn't open the file.
    if (result != API_OK)
        return result;

    // File's open.
    return std::make_unique<platform::FileContext>(std::move(context),
                                              mount,
                                              flags);
}

Error FileInode::replace(InodeBadge,
                         InodeRef other,
                         const std::string& otherName,
                         DirectoryInodeRef otherParent)
{
    assert(other);
    assert(otherParent);

    // Are we replacing a file?
    auto otherFile = other->file();

    // A file can't replace a directory.
    if (!otherFile)
        return API_FUSE_EISDIR;

    // Ask the Inode DB to replace the other file with us.
    return mInodeDB.replace(FileInodeRef(this),
                            std::move(otherFile),
                            otherName,
                            std::move(otherParent));
}

Error FileInode::touch(const Mount& mount,
                       m_time_t modified)
{
    // Lock the file so no one else can alter us.
    InodeLock lock(*this);

    // Get our hands on our context.
    auto context = mInodeDB.fileCache().context(FileInodeRef(this));

    // Try and open the file for IO.
    auto result = context->open(mount, false);

    // Try and update the file's modification time.
    if (result == API_OK)
        result = context->touch(mount, modified);

    // Return result to caller.
    return result;
}

Error FileInode::truncate(const Mount& mount,
                          m_off_t size,
                          bool dontGrow)
{
    // Lock the file so no one else can alter us.
    InodeLock lock(*this);

    // Get our hands on our context.
    auto context = mInodeDB.fileCache().context(FileInodeRef(this));

    // Open the file for IO.
    auto result = context->open(mount, !size);

    // Couldn't open the file for IO.
    if (result != API_OK)
        return result;

    // Truncate the file if necessary.
    if (size)
        result = context->truncate(mount, size, dontGrow);

    // Return result to caller.
    return result;
}

Error FileInode::unlink(InodeBadge)
{
    return mInodeDB.unlink(FileInodeRef(this));
}

bool FileInode::wasModified() const
{
    return mInodeDB.modified(id());
}

void doRef(RefBadge badge, FileInode& inode)
{
    doRef(badge, static_cast<Inode&>(inode));
}

void doUnref(RefBadge badge, FileInode& inode)
{
    doUnref(badge, static_cast<Inode&>(inode));
}

} // fuse
} // mega

