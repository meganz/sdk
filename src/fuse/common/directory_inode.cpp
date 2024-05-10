#include <cassert>
#include <mutex>

#include <mega/fuse/common/any_lock_set.h>
#include <mega/fuse/common/any_lock.h>
#include <mega/fuse/common/badge.h>
#include <mega/fuse/common/client.h>
#include <mega/fuse/common/constants.h>
#include <mega/fuse/common/directory_inode.h>
#include <mega/fuse/common/error_or.h>
#include <mega/fuse/common/inode_badge.h>
#include <mega/fuse/common/inode_db.h>
#include <mega/fuse/common/inode_info.h>
#include <mega/fuse/common/node_info.h>
#include <mega/fuse/common/ref.h>

namespace mega
{
namespace fuse
{

template<typename Maker>
ErrorOr<MakeInodeResult> DirectoryInode::make(Maker&& maker, const std::string& name)
{
    InodeLock guard(*this);

    // Invalid name.
    if (name.empty())
        return API_EARGS;

    // Name's too long.
    if (name.size() > MaxNameLength)
        return API_FUSE_ENAMETOOLONG;

    auto permissions = this->permissions();

    // Parent doesn't exist.
    if (permissions == ACCESS_UNKNOWN)
        return API_ENOENT;

    // Parent's read only.
    if (permissions != FULL)
        return API_FUSE_EROFS;

    // Parent already has a child with this name.
    if (hasChild(name))
        return API_EEXIST;

    // Try and make the new child.
    return maker(name);
}

void DirectoryInode::remove(RefBadge badge, InodeDBLock lock)
{
    // Remove this directory from the database.
    mInodeDB.remove(*this, std::move(lock));
}

DirectoryInode::DirectoryInode(InodeID id,
                               const NodeInfo& info,
                               InodeDB& inodeDB)
  : Inode(id, info, inodeDB)
{
}

DirectoryInode::~DirectoryInode()
{
}

bool DirectoryInode::cached() const
{
    return false;
}

InodeRefVector DirectoryInode::children() const
{
    InodeLock guard(*this);

    // Ask the Inode DB what children we contain.
    return mInodeDB.children(*this);
}

DirectoryInodeRef DirectoryInode::directory()
{
    return DirectoryInodeRef(this);
}

InodeRef DirectoryInode::get(const std::string& name) const
{
    InodeLock guard(*this);

    // Ask the Inode DB if we have a child with this name.
    return mInodeDB.child(*this, name);
}

NodeHandle DirectoryInode::handle() const
{
    return static_cast<NodeHandle>(mID);
}

bool DirectoryInode::hasChild(const std::string& name) const
{
    InodeLock guard(*this);

    // Ask the Inode DB if we contain a child with this name.
    return mInodeDB.hasChild(*this, name);
}

ErrorOr<bool> DirectoryInode::hasChildren() const
{
    InodeLock guard(*this);

    // Ask the Inode DB if we contain any children.
    return mInodeDB.hasChildren(*this);
}

void DirectoryInode::info(const NodeInfo& info)
{
    InodeDBLock guard(mInodeDB);

    // Sanity.
    assert(!info.mHandle.isUndef());
    assert(info.mIsDirectory);

    // Update this directory's cached description.
    Inode::info(info, guard);
}

InodeInfo DirectoryInode::info() const
{
    // Acquire lock.
    InodeDBLock lock(mInodeDB);

    // No special gymnastics are needed if we've been removed.
    if (removed())
    {
        InodeInfo info;

        // Just return cached information.
        info.mID = mID;
        info.mIsDirectory = true;
        info.mModified = mModified;
        info.mName = mName;
        info.mParentID = InodeID(mParentHandle);
        info.mPermissions = mPermissions;
        info.mSize = 4096;

        return info;
    }

    // Retrieve info and update cached state.
    while (true)
    {
        // Latch cached state.
        auto lastModified = mModified;
        auto lastName = mName;
        auto lastParentHandle = mParentHandle;
        auto lastPermissions = mPermissions;

        // Release lock so we can call the client.
        lock.unlock();

        // Try and retrieve the latest info on ourselves.
        auto info = mInodeDB.client().get(handle());

        // Reacquire lock.
        lock.lock();

        // Another thread's updated our cached state.
        if (lastModified != mModified
            || lastParentHandle != mParentHandle
            || lastPermissions != mPermissions
            || lastName != mName)
            continue;

        // We no longer exist.
        if (!info)
        {
            InodeInfo info_;

            // Populate info based on last known state.
            info_.mID = mID;
            info_.mIsDirectory = true;
            info_.mModified = mModified;
            info_.mName = std::move(lastName);
            info_.mParentID = InodeID(mParentHandle);
            info_.mPermissions = mPermissions;
            info_.mSize = 4096;

            // Mark ourselves as removed.
            removed(true);

            // Return our description to the caller.
            return info_;
        }

        // Update cached state.
        const_cast<DirectoryInode*>(this)->Inode::info(*info, lock);

        // Return info to caller.
        return InodeInfo(mID, std::move(*info));
    }
}

ErrorOr<MakeInodeResult> DirectoryInode::makeDirectory(const platform::Mount& mount,
                                                       const std::string &name)
{
    return make([&](const std::string& name) {
        return mInodeDB.makeDirectory(mount, name, DirectoryInodeRef(this));
    }, name);
}

ErrorOr<MakeInodeResult> DirectoryInode::makeFile(const platform::Mount& mount,
                                                  const std::string& name)
{
    return make([&](const std::string& name) {
        return mInodeDB.makeFile(mount,
                                 name,
                                 DirectoryInodeRef(this));
    }, name);
}

Error DirectoryInode::move(const std::string& name,
                           const std::string& newName,
                           DirectoryInodeRef newParent)
{
    // Sanity.
    assert(newParent);

    // Invalid names.
    if (name.size() > MaxNameLength
        || newName.size() > MaxNameLength)
        return API_FUSE_ENAMETOOLONG;

    // Easy management of multiple locks.
    AnyLockSet locks;

    // Lock parents.
    locks.emplace(*this);
    locks.emplace(*newParent);
    locks.lock();

    // Does the child we're moving exist?
    auto source = get(name);

    // Child doesn't exist.
    if (!source)
        return API_ENOENT;

    // Does our new parent already have a child with our desired name?
    auto target = newParent->get(newName);

    // Release parent locks.
    locks.unlock();

    locks.emplace(*source);

    // New parent contains a child with the desired name.
    if (target)
    {
        locks.emplace(*target);

        // Reacquire locks.
        locks.lock();

        // Perform replace.
        return source->replace(InodeBadge(),
                               std::move(target),
                               newName,
                               newParent);
    }

    // New parent doesn't have a child with our desired name.
    locks.lock();

    // Perform move.
    return source->move(InodeBadge(),
                        newName,
                        std::move(newParent));
}

Error DirectoryInode::move(InodeBadge badge,
                           const std::string& name,
                           DirectoryInodeRef parent)
{
    // Sanity.
    assert(parent);

    return mInodeDB.move(InodeRef(this), name, std::move(parent));
}

Error DirectoryInode::replace(InodeBadge badge,
                              InodeRef other,
                              const std::string& otherName,
                              DirectoryInodeRef otherParent)
{
    // Sanity.
    assert(other);
    assert(otherParent);

    // Are we replacing a directory?
    auto otherDirectory = other->directory();

    // Directories can't replace files.
    if (!otherDirectory)
        return API_FUSE_ENOTDIR;

    // Is the directory we're replacing empty?
    auto result = otherDirectory->hasChildren();

    // Other directory has been removed.
    if (!result)
        return API_ENOENT;

    // Other directory isn't empty.
    if (*result)
        return API_FUSE_ENOTEMPTY;

    // Perform the replacement.
    return mInodeDB.replace(DirectoryInodeRef(this),
                            std::move(otherDirectory),
                            otherName,
                            std::move(otherParent));
}

Error DirectoryInode::unlink(const std::string& name,
                             std::function<Error(InodeRef)> predicate)
{
    // Invalid name.
    if (name.size() > MaxNameLength)
        return API_FUSE_ENAMETOOLONG;

    // Do we have a child with this name?
    auto child = get(name);

    // No child with this name.
    if (!child)
        return API_ENOENT;

    // Lock the child.
    InodeLock childLock(*child);

    auto permissions = child->permissions();

    // Child no longer exists.
    if (permissions == ACCESS_UNKNOWN)
        return API_ENOENT;

    // Child's read only.
    if (permissions != FULL)
        return API_FUSE_EROFS;

    auto result = API_OK;

    // Perform extra checks, if necessary.
    if (predicate)
        result = predicate(child);

    // Unlink the child.
    if (result == API_OK)
        result = child->unlink(InodeBadge());

    // Return result to caller.
    return result;
}

Error DirectoryInode::unlink(InodeBadge)
{
    auto result = hasChildren();

    // Can't unlink a directory that doesn't exist.
    if (!result)
        return API_ENOENT;

    // Can't unlink a directory that isn't empty.
    if (*result)
        return API_FUSE_ENOTEMPTY;

    // Unlink the directory.
    return mInodeDB.unlink(InodeRef(this));
}

void doRef(RefBadge badge, DirectoryInode& inode)
{
    doRef(badge, static_cast<Inode&>(inode));
}

void doUnref(RefBadge badge, DirectoryInode& inode)
{
    doUnref(badge, static_cast<Inode&>(inode));
}

} // fuse
} // mega

